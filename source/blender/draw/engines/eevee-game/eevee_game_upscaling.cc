/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "eevee_game_upscaling.hh"
#include "eevee_game_instance.hh"

#include "GPU_context.hh"  /* GPU_backend_get_type() */

namespace blender::eevee_game {

/* ================================================================
 * Destructor
 * ================================================================ */

UpscaleModule::~UpscaleModule()
{
#ifdef WITH_AMD_FSR3
  if (is_initialized_) {
    /* GPU must be idle before destroying: all in-flight frames using FSR
     * resources must have completed. GameInstance handles the GPU sync. */
    ffxFsr3ContextDestroy(&fsr3_context_);
    is_initialized_ = false;
  }
#endif
}

/* ================================================================
 * init()
 * ================================================================ */

void UpscaleModule::init(int2 render_res, int2 display_res)
{
#ifdef WITH_AMD_FSR3
  if (is_initialized_) {
    ffxFsr3ContextDestroy(&fsr3_context_);
    is_initialized_ = false;
  }

  render_res_  = render_res;
  display_res_ = display_res;

  /* --- Retrieve Vulkan device handles via the public GPU API ---
   *
   * GPU_vk_device_handles_get() is declared in GPU_texture.hh and implemented
   * in vk_texture_interop.cc (inside the gpu/vulkan module).
   * It returns dispatchable handles as uint64_t so this file needs no Vulkan
   * internal headers. We cast back to the Vulkan types here, where ffx_vk.h
   * is already included. */
  const GPUVKDeviceHandles dev_handles = GPU_vk_device_handles_get();

  const VkPhysicalDevice vk_phys_dev = reinterpret_cast<VkPhysicalDevice>(
      static_cast<uintptr_t>(dev_handles.vk_physical_device));
  const VkDevice vk_device = reinterpret_cast<VkDevice>(
      static_cast<uintptr_t>(dev_handles.vk_device));

  /* --- Scratch memory for the three FfxInterface backends ---
   *
   * FSR 3.1 requires three separate FfxInterface instances:
   *   backendInterfaceSharedResources    — intermediate resource allocation
   *   backendInterfaceUpscaling          — temporal accumulation passes
   *   backendInterfaceFrameInterpolation — optical flow + frame generation
   *
   * Each backend needs its own scratch block. We query the required size with
   * ffxGetScratchMemorySizeVK() and store in Vector<uint8_t> — allocated once,
   * never reallocated per frame. */
  const size_t sz_shared  = ffxGetScratchMemorySizeVK(vk_phys_dev,
                                                       FFX_FSR3_CONTEXT_COUNT);
  const size_t sz_upscale = ffxGetScratchMemorySizeVK(vk_phys_dev,
                                                       FFX_FSR3UPSCALER_CONTEXT_COUNT);
  const size_t sz_fi      = ffxGetScratchMemorySizeVK(vk_phys_dev,
                                                       FFX_FRAMEINTERPOLATION_CONTEXT_COUNT);

  scratch_shared_.resize(sz_shared,   0);
  scratch_upscale_.resize(sz_upscale, 0);
  scratch_fi_.resize(sz_fi,           0);

  const FfxDevice ffx_device = ffxGetDeviceVK(vk_device);
  FfxErrorCode err;

  err = ffxGetInterfaceVK(&context_desc_.backendInterfaceSharedResources,
                           ffx_device,
                           scratch_shared_.data(),  scratch_shared_.size(),
                           FFX_FSR3_CONTEXT_COUNT);
  BLI_assert_msg(err == FFX_OK, "FSR3: failed to init shared resources backend");

  err = ffxGetInterfaceVK(&context_desc_.backendInterfaceUpscaling,
                           ffx_device,
                           scratch_upscale_.data(), scratch_upscale_.size(),
                           FFX_FSR3UPSCALER_CONTEXT_COUNT);
  BLI_assert_msg(err == FFX_OK, "FSR3: failed to init upscaling backend");

  err = ffxGetInterfaceVK(&context_desc_.backendInterfaceFrameInterpolation,
                           ffx_device,
                           scratch_fi_.data(),      scratch_fi_.size(),
                           FFX_FRAMEINTERPOLATION_CONTEXT_COUNT);
  BLI_assert_msg(err == FFX_OK, "FSR3: failed to init frame interpolation backend");

  /* --- Context description ---
   *
   * FFX_FSR3_ENABLE_UPSCALING_ONLY: frame generation disabled until UPBGE has
   *   a swapchain-level present hook (needed to synthesise intermediate frames).
   * FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE: combined_tx is SFLOAT_16_16_16_16 (linear HDR).
   * NOT FFX_FSR3_ENABLE_DEPTH_INVERTED: Blender uses standard depth [near..far]. */
  context_desc_.flags = FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE |
                        FFX_FSR3_ENABLE_UPSCALING_ONLY;

  context_desc_.maxRenderSize  = {uint32_t(render_res.x),  uint32_t(render_res.y)};
  context_desc_.maxUpscaleSize = {uint32_t(display_res.x), uint32_t(display_res.y)};
  context_desc_.displaySize    = {uint32_t(display_res.x), uint32_t(display_res.y)};

  /* Output texture in apply_fsr3() is SFLOAT_16_16_16_16 */
  context_desc_.backBufferFormat = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;

#ifndef NDEBUG
  context_desc_.fpMessage = [](FfxMsgType type, const wchar_t *msg) {
    if (type == FFX_MESSAGE_TYPE_ERROR) {
      fprintf(stderr, "[FSR3 ERROR] %ls\n", msg);
    }
  };
#endif

  err = ffxFsr3ContextCreate(&fsr3_context_, &context_desc_);
  BLI_assert_msg(err == FFX_OK, "FSR3: ffxFsr3ContextCreate failed");
  is_initialized_ = (err == FFX_OK);

  if (!is_initialized_) {
    return;
  }

  /* Jitter phase count is resolution-dependent.
   * Quality (1.5x) = 18 samples; Balanced (1.7x) = 23; Performance (2.0x) = 32. */
  jitter_phase_count_ = ffxFsr3GetJitterPhaseCount(render_res.x, display_res.x);
  jitter_frame_index_ = 0;

#else
  (void)render_res;
  (void)display_res;
#endif /* WITH_AMD_FSR3 */
}

/* ================================================================
 * apply_fsr3()
 * ================================================================ */

void UpscaleModule::apply_fsr3(gpu::Texture *src, gpu::Texture *dst, gpu::Texture *ui_tx)
{
#ifdef WITH_AMD_FSR3
  if (!is_initialized_) {
    return;
  }

  /* --- Jitter via the SDK's own Halton sequence ---
   *
   * ffxFsr3GetJitterOffset() returns pixel-space offsets in [-0.5, +0.5].
   * We convert to NDC and write into uniform_data.jitter so that
   * ShadingView::update_view() applies them to the projection matrix:
   *
   *   ndcX =  2.0 * jitterX / renderWidth
   *   ndcY = -2.0 * jitterY / renderHeight   (Y flipped: NDC up == screen up)
   *
   * The raw pixel-space values go into the dispatch description so FSR can
   * reconstruct the offset internally. */
  float jitter_x = 0.0f;
  float jitter_y = 0.0f;
  ffxFsr3GetJitterOffset(&jitter_x, &jitter_y, jitter_frame_index_, jitter_phase_count_);
  jitter_frame_index_ = (jitter_frame_index_ + 1) % jitter_phase_count_;

  const int2 render_res = inst_->film.render_extent_get();
  inst_->uniform_data.jitter = float2(
       2.0f * jitter_x / float(render_res.x),
      -2.0f * jitter_y / float(render_res.y));

  /* FfxFsr3DispatchUpscaleDescription is the correct struct when
   * FFX_FSR3_ENABLE_UPSCALING_ONLY is set (FSR 3.1). */
  FfxFsr3DispatchUpscaleDescription dispatch = {};
  dispatch.commandList = get_command_list();

  dispatch.color = bridge_texture(src,
      L"FSR3_Color",        FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
  dispatch.depth = bridge_texture(&inst_->render_buffers.depth_tx,
      L"FSR3_Depth",        FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
  dispatch.motionVectors = bridge_texture(inst_->render_buffers.vector_tx.get(),
      L"FSR3_MotionVectors", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

  /* Masks generated by generate_masks() earlier this frame */
  dispatch.reactive = bridge_texture(inst_->render_buffers.reactive_mask_tx.get(),
      L"FSR3_Reactive",     FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
  dispatch.transparencyAndComposition = bridge_texture(
      inst_->render_buffers.transp_mask_tx.get(),
      L"FSR3_TransComp",    FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

  dispatch.upscaleOutput = bridge_texture(dst,
      L"FSR3_Output",       FFX_RESOURCE_STATE_UNORDERED_ACCESS);

  /* Raw pixel-space jitter — FSR uses this to invert the offset internally */
  dispatch.jitterOffset    = {jitter_x, jitter_y};
  /* Motion vectors are in pixel units at render resolution */
  dispatch.motionVectorScale = {float(render_res.x), float(render_res.y)};
  dispatch.renderSize      = {uint32_t(render_res.x),   uint32_t(render_res.y)};
  dispatch.upscaleSize     = {uint32_t(display_res_.x),  uint32_t(display_res_.y)};

  dispatch.frameTimeDelta  = inst_->delta_time_ms;
  dispatch.preExposure     = 1.0f;

  const CameraData &cam = inst_->camera.data_get();
  dispatch.cameraNear             = cam.clip_near;
  dispatch.cameraFar              = cam.clip_far;
  dispatch.cameraFovAngleVertical = 2.0f * atanf(
      (cam.sensor_width * 0.5f) / cam.focal_length);
  dispatch.viewSpaceToMetersFactor = 1.0f; /* Blender scene unit = 1 metre */

  /* reset=true discards FSR temporal history on camera cuts / scene loads.
   * notify_camera_cut() sets camera_cut_pending_ from the game logic side. */
  dispatch.reset = camera_cut_pending_;
  camera_cut_pending_ = false;
  dispatch.enableSharpening = false;
  dispatch.sharpness        = 0.0f;

  const FfxErrorCode err = ffxFsr3ContextDispatchUpscale(&fsr3_context_, &dispatch);
  BLI_assert_msg(err == FFX_OK, "FSR3: ffxFsr3ContextDispatchUpscale failed");

  /* Composite the UI buffer at full display resolution after the upscale.
   * GPU_texture_copy(dst, src). */
  if (ui_tx != nullptr) {
    GPU_texture_copy(dst, ui_tx);
  }

#else
  (void)src;
  (void)dst;
  (void)ui_tx;
#endif /* WITH_AMD_FSR3 */
}

/* ================================================================
 * generate_masks()
 * ================================================================ */

void UpscaleModule::generate_masks(gpu::Texture *opaque_tx,
                                   gpu::Texture *combined_tx,
                                   gpu::Texture *reactive_tx,
                                   gpu::Texture *transp_tx)
{
#ifdef WITH_AMD_FSR3
  if (!is_initialized_) {
    return;
  }

  const int2 render_res = inst_->film.render_extent_get();
  const FfxDimensions2D ffx_res = {uint32_t(render_res.x), uint32_t(render_res.y)};

  /* --- Reactive mask ---
   * Marks pixels that changed significantly between the opaque-only frame and
   * the full frame. Excluded from temporal accumulation to prevent ghosting on
   * fire, smoke, and alpha-tested geometry.
   *
   * scale = 1.0          : linear mapping of luminance delta to [0,1].
   * cutoffThreshold = 0.1: pixels with delta < 10% luma are stable.
   * binaryValue = 1.0    : above threshold → full reactivity. */
  {
    FfxFsr3GenerateReactiveDescription desc = {};
    desc.commandList     = get_command_list();
    desc.colorOpaqueOnly = bridge_texture(opaque_tx,
        L"FSR3_OpaqueOnly",  FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    desc.colorPreUpscale = bridge_texture(combined_tx,
        L"FSR3_Combined",    FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    desc.outReactive     = bridge_texture(reactive_tx,
        L"FSR3_Reactive",    FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.renderSize      = ffx_res;
    desc.scale           = 1.0f;
    desc.cutoffThreshold = 0.1f;
    desc.binaryValue     = 1.0f;
    desc.flags           = 0;

    const FfxErrorCode err = ffxFsr3ContextGenerateReactiveMask(&fsr3_context_, &desc);
    BLI_assert_msg(err == FFX_OK, "FSR3: GenerateReactiveMask failed");
  }

  /* --- Transparency / composition mask ---
   * Marks refractive, glass, and alpha-composited pixels with a softer weight.
   * Lower cutoff makes it more sensitive to subtle refractions.
   * binaryValue = 0.5 gives a soft mask so FSR partially accumulates these
   * pixels rather than rejecting them entirely. */
  {
    FfxFsr3GenerateReactiveDescription desc = {};
    desc.commandList     = get_command_list();
    desc.colorOpaqueOnly = bridge_texture(opaque_tx,
        L"FSR3_OpaqueOnly2", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    desc.colorPreUpscale = bridge_texture(combined_tx,
        L"FSR3_Combined2",   FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    desc.outReactive     = bridge_texture(transp_tx,
        L"FSR3_TransComp",   FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    desc.renderSize      = ffx_res;
    desc.scale           = 0.5f;
    desc.cutoffThreshold = 0.05f;
    desc.binaryValue     = 0.5f;
    desc.flags           = 0;

    const FfxErrorCode err = ffxFsr3ContextGenerateReactiveMask(&fsr3_context_, &desc);
    BLI_assert_msg(err == FFX_OK, "FSR3: GenerateReactiveMask (transp) failed");
  }

#else
  (void)opaque_tx;
  (void)combined_tx;
  (void)reactive_tx;
  (void)transp_tx;
#endif /* WITH_AMD_FSR3 */
}

/* ================================================================
 * calculate_render_res() — static
 * ================================================================ */

/* static */
int2 UpscaleModule::calculate_render_res(int2 display_res, UpscaleMode mode)
{
#ifdef WITH_AMD_FSR3
  if (mode == UpscaleMode::OFF) {
    return display_res;
  }
  /* Delegate to the SDK so ratios are always in sync with the official spec */
  uint32_t render_w = 0, render_h = 0;
  const FfxErrorCode err = ffxFsr3GetRenderResolutionFromQualityMode(
      &render_w, &render_h,
      uint32_t(display_res.x), uint32_t(display_res.y),
      to_ffx_quality(mode));

  if (err != FFX_OK) {
    return display_res; /* Fallback: no downscale */
  }
  return int2(int(render_w), int(render_h));
#else
  (void)mode;
  return display_res;
#endif
}

/* ================================================================
 * Private helpers
 * ================================================================ */

#ifdef WITH_AMD_FSR3

FfxResource UpscaleModule::bridge_texture(gpu::Texture *tx,
                                           const wchar_t *debug_name,
                                           FfxResourceStates initial_state)
{
  BLI_assert(tx != nullptr);

  /* GPU_texture_vk_handles_get() is declared in GPU_texture.hh and implemented
   * in vk_texture_interop.cc (inside the gpu/vulkan module).
   * It performs the VKTexture cast there; this file stays free of vk_texture.hh. */
  const GPUTextureVKHandles h = GPU_texture_vk_handles_get(tx);

  BLI_assert_msg(h.vk_image != 0, "FSR3 bridge_texture: texture not yet allocated on GPU");

  FfxResourceDescription res_desc = {};
  res_desc.type  = FFX_RESOURCE_TYPE_TEXTURE2D;
  /* h.vk_format carries a VkFormat value as uint32_t.
   * ffxGetSurfaceFormatVK expects VkFormat (int32_t enum) — safe cast. */
  res_desc.format   = ffxGetSurfaceFormatVK(static_cast<VkFormat>(h.vk_format));
  res_desc.width    = uint32_t(GPU_texture_width(tx));
  res_desc.height   = uint32_t(GPU_texture_height(tx));
  res_desc.depth    = 1;
  res_desc.mipCount = uint32_t(GPU_texture_mip_count(tx));
  res_desc.flags    = FFX_RESOURCE_FLAGS_NONE;

  /* VkImage / VkImageView are non-dispatchable handles (uint64_t on all platforms).
   * The static_cast<VkImage> cast is lossless and defined by the Vulkan spec. */
  return ffxGetResourceVK(
      static_cast<VkImage>(h.vk_image),
      static_cast<VkImageView>(h.vk_image_view),
      res_desc,
      debug_name,
      initial_state);
}

FfxCommandList UpscaleModule::get_command_list()
{
  /* GPU_vk_command_buffer_get() declared in GPU_texture.hh,
   * implemented in vk_texture_interop.cc.
   * VkCommandBuffer is a dispatchable handle (a pointer). */
  const uint64_t raw_cmd = GPU_vk_command_buffer_get();
  const VkCommandBuffer vk_cmd = reinterpret_cast<VkCommandBuffer>(
      static_cast<uintptr_t>(raw_cmd));
  return ffxGetCommandListVK(vk_cmd);
}

/* static */
FfxFsr3QualityMode UpscaleModule::to_ffx_quality(UpscaleMode mode)
{
  switch (mode) {
    /* FSR 3.1 removed Ultra Performance as a distinct preset.
     * Map both our ultra-quality tiers to the Quality preset (1.5x). */
    case UpscaleMode::FSR2_ULTRA_QUALITY:
    case UpscaleMode::FSR2_QUALITY:     return FFX_FSR3_QUALITY_MODE_QUALITY;
    case UpscaleMode::FSR2_BALANCED:    return FFX_FSR3_QUALITY_MODE_BALANCED;
    case UpscaleMode::FSR2_PERFORMANCE: return FFX_FSR3_QUALITY_MODE_PERFORMANCE;
    default:
      BLI_assert_unreachable();
      return FFX_FSR3_QUALITY_MODE_QUALITY;
  }
}

#endif /* WITH_AMD_FSR3 */

} // namespace blender::eevee_game
