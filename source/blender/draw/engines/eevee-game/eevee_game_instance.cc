/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "eevee_game_instance.hh"
#include "eevee_game_engine.h"
#include "eevee_game_material.hh"
#include "eevee_game_sync.hh"

#include "BKE_scene.hh"
#include "BKE_object.hh"
#include "DEG_depsgraph_query.hh"
#include "DRW_render.hh"
#include "RE_pipeline.h"

#include "DNA_camera_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

namespace blender::eevee_game {

/* ============================================================
 * HiZBuffer
 * ============================================================ */

void HiZBuffer::ensure(int2 render_res)
{
  /* Full mip pyramid down to 1x1.
   * log2(max_dim) + 1 gives the correct mip count so the ray marcher can
   * step 2^mip pixels per iteration at the coarsest level. */
  const int mip_count = int(math::ceil(
      math::log2(float(math::reduce_max(render_res))))) + 1;

  /* R32F: single-channel min-depth per texel, mipmapped.
   * GPU_TEXTURE_USAGE_SHADER_WRITE needed for the compute-shader downsample. */
  ref_tx_.ensure_2d(
      gpu::TextureFormat::SFLOAT_32,
      render_res,
      GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE,
      nullptr,
      mip_count);
}

/* ============================================================
 * Camera
 * ============================================================ */

void Camera::sync(const Object *cam_ob)
{
  if (!cam_ob || cam_ob->type != OB_CAMERA) {
    return;
  }

  /* object_to_world() returns the evaluated world matrix from the Depsgraph */
  object_to_world = cam_ob->object_to_world();

  /* Cast ob->data to the DNA Camera struct (NOT blender::Camera which is the BKE type) */
  const ::Camera *cam = static_cast<const ::Camera *>(cam_ob->data);
  data_.clip_near    = cam->clip_start;
  data_.clip_far     = cam->clip_end;
  data_.focal_length = cam->lens;
  data_.sensor_width = cam->sensor_x;
}

/* ============================================================
 * LightModule
 * ============================================================ */

void LightModule::init()
{
  /* Pre-allocate the full MAX_LIGHTS capacity once so end_sync() never
   * reallocates mid-frame. 512 lights * 64 bytes = 32 KB - fits in L2.
   * GPU_USAGE_DYNAMIC allows CPU->GPU upload via StorageBuffer::update(). */
  light_buf_ = std::make_unique<gpu::StorageBuffer>(
      MAX_LIGHTS * sizeof(LightData), GPU_USAGE_DYNAMIC);
}

void LightModule::begin_sync()
{
  light_map_.clear();
  active_light_count = 0;
}

void LightModule::add(int id, const LightEntry &entry)
{
  if (active_light_count >= MAX_LIGHTS) {
    /* Exceed limit: silently discard.
     * The GLSL lighting shader clamps its loop to active_light_count anyway. */
    return;
  }
  light_map_.add_overwrite(id, entry);
  active_light_count++;
}

void LightModule::end_sync()
{
  if (active_light_count == 0) {
    return;
  }

  /* Pack all LightData structs into a contiguous array for a single DMA upload.
   * Iteration order is arbitrary (hash map); the shader accesses by index, not order. */
  Vector<LightData> packed;
  packed.reserve(active_light_count);
  for (const LightEntry &entry : light_map_.values()) {
    packed.append(entry.data);
  }

  light_buf_->update(packed.data());
}

void LightModule::bind_resources(PassSimple &ps)
{
  /* SLOT_LIGHT_DATA = 1: matches 'layout(binding = 1)' in the GLSL lighting shader */
  ps.bind_ssbo("lights_buf", light_buf_.get());
  ps.push_constant("light_count", active_light_count);
}

/* ============================================================
 * GameInstance
 * ============================================================ */

GameInstance::GameInstance()
    : film(*this),
      render_buffers(*this),
      materials(*this),
      /* ShadowModule takes a ShadowData& - we give it a standalone member.
       * It cannot use uniform_data because UniformData (a GPU UBO struct) does
       * not embed ShadowData; they are separate GPU buffers. */
      shadows(*this, shadow_data_),
      culling(*this),
      sync(*this),
      bloom(*this),
      dof(*this),
      gtao(*this),
      ssgi(*this),
      /* RayTraceModule constructor takes GameInstance* (not ShaderModule*) */
      raytrace(this),
      volume(*this),
      /* PipelineModule takes (ShaderModule*, GameInstance*) per its updated .hh */
      pipelines(&shaders, this),
      upscale(*this),
      main_view_(*this)
{
}

void GameInstance::init(const int2 &output_res, const rcti *output_rect)
{
  (void)output_rect;

  /* Film computes render_extent_ from display_extent_ using the FSR scale factor */
  film.init(output_res);

  const int2 render_res  = film.render_extent_get();
  const int2 display_res = film.display_extent_get();

  /* Allocate all intermediate GPU render targets for the resolved render resolution */
  render_buffers.init();
  render_buffers.acquire(render_res);

  /* Allocate the Hi-Z mip chains (front = readable, back = being written) */
  hiz_buffer.front.ensure(render_res);
  hiz_buffer.back.ensure(render_res);

  /* Compile the Hi-Z rebuild compute shader once; it is re-submitted every frame */
  hiz_update_ps_.init();
  hiz_update_ps_.shader_set(shaders.static_shader_get(SH_HIZ_UPDATE));

  /* The G-Buffer descriptor is kept in sync by PipelineModule::sync(),
   * but we set it here too so it is valid before the first sync() call. */
  gbuffer.normal_tx  = &render_buffers.rp_color_tx;
  gbuffer.closure_tx = &render_buffers.rp_color_tx;
  gbuffer.header_tx  = &render_buffers.rp_value_tx;

  /* Initialise the FSR upscaler if a quality mode is selected */
  if (upscale_settings.mode != UpscaleMode::OFF) {
    upscale.init(render_res, display_res);
  }

  /* Initialise all sub-modules in dependency order */
  lights.init();
  shadows.init();
  culling.init();
  bloom.init();
  dof.init();
  gtao.init();
  ssgi.init();
  raytrace.init();
  volume.init();
  pipelines.aa.init();

  /* Initialise the main shading view last (it queries render/display extents) */
  main_view_.init("main", display_res);
}

void GameInstance::begin_sync()
{
  /* Pull DRW context pointers; these are only valid inside a DRW draw callback */
  update_eval_members();

  /* Reset per-frame accumulation lists */
  lights.begin_sync();
  culling.begin_sync();
  shadows.begin_sync();
  velocity.begin_sync();
  materials.begin_sync();

  /* Advance the Halton jitter sequence for FSR temporal reconstruction */
  film.sync();

  /* Sync camera matrices from the evaluated Depsgraph Object */
  camera.sync(camera_eval_object);

  /* Upload per-frame uniform data.
   * These fields map directly to the GLSL 'uniform_data' UBO. */
  uniform_data.viewmat     = math::invert(camera.object_to_world);
  uniform_data.viewinv     = camera.object_to_world;
  uniform_data.camera_pos  = camera.position();
  uniform_data.jitter      = film.get_pixel_jitter();
  uniform_data.z_near      = camera.data_get().clip_near;
  uniform_data.z_far       = camera.data_get().clip_far;
  uniform_data.delta_time  = delta_time_ms * 0.001f;
  uniform_data.frame_count = uint32_t(film.frame_index_get());

  /* Projection matrix - built from camera lens / sensor data.
   * film.render_extent_get() gives the render resolution, not the display resolution,
   * so the aspect ratio accounts for any FSR downscale. */
  const float2 render_res = float2(film.render_extent_get());
  const float  aspect     = render_res.x / render_res.y;
  const float  fov_y      = 2.0f * atanf((camera.data_get().sensor_width * 0.5f) /
                                          camera.data_get().focal_length) / aspect;
  uniform_data.projectionmat = math::projection::perspective(
      fov_y, aspect,
      camera.data_get().clip_near,
      camera.data_get().clip_far);

  uniform_data.viewprojmat   = uniform_data.projectionmat * uniform_data.viewmat;
  uniform_data.screen_res    = render_res;
  uniform_data.screen_res_inv = float2(1.0f) / render_res;

  /* Traverse the Depsgraph and populate culling, lights, velocity, shadows.
   * This must happen after uniform_data is filled (culling uses camera matrices)
   * and after per-frame begin_sync() resets on all sub-modules. */
  sync_scene();
}

void GameInstance::end_sync()
{
  /* Upload the packed light array after the scene traversal has added all lights */
  lights.end_sync();

  /* Compute tight cascade view-projections from the final light list */
  shadows.end_sync();

  /* Finalise velocity: upload indirection buffer and swap current->previous. */
  velocity.geometry_steps_fill();
  velocity.end_sync();

  sync.end_sync();

  /* Finish texture loading for all materials synced this frame */
  materials.end_sync();

  /* Sync all per-frame effect state (texture allocation, pass configuration) */
  bloom.sync();
  dof.sync();
  gtao.sync();
  ssgi.sync();
  raytrace.sync();
  volume.sync();
  pipelines.sync();
}

void GameInstance::render_sample()
{
  main_view_.render();
}

void GameInstance::render_frame(RenderEngine *engine, RenderLayer *layer)
{
  /* Execute one full game-quality render sample */
  render_sample();

  /* Read the final pixels from GPU back to CPU and push them into the RenderLayer.
   *
   * The final output lives in main_view_'s display_res_tx_ (after FSR upscale) or
   * postfx_tx_ (when FSR is OFF). Both are accessible via the viewport backbuffer
   * which Film::present() already blitted to. We read from the default framebuffer. */
  RenderResult *rr = RE_engine_get_result(engine);
  if (rr == nullptr) {
    RE_engine_end_result(engine, layer, false, false, false);
    return;
  }

  /* Find the Combined render pass in the layer */
  RenderPass *rp = RE_pass_find_by_name(layer, RE_PASSNAME_COMBINED, "");
  if (rp == nullptr) {
    RE_engine_end_result(engine, layer, false, false, false);
    return;
  }

  /* Read RGBA float pixels from the viewport default framebuffer.
   * The framebuffer was already bound and populated by Film::present(). */
  const int2 display_res = film.display_extent_get();
  const int pixel_count  = display_res.x * display_res.y;

  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();
  GPU_framebuffer_bind(dfbl->default_fb);
  GPU_framebuffer_read_color(dfbl->default_fb,
                              0, 0,
                              display_res.x, display_res.y,
                              4,             /* RGBA */
                              0,             /* slot */
                              GPU_DATA_FLOAT,
                              rp->ibuf->float_buffer.data);

  /* Flip Y: OpenGL framebuffer origin is bottom-left, Blender render is top-left */
  const int row_bytes = display_res.x * 4 * sizeof(float);
  Vector<uint8_t> tmp_row(row_bytes);
  float *pixels = rp->ibuf->float_buffer.data;
  for (int y = 0; y < display_res.y / 2; y++) {
    float *top    = pixels + (display_res.y - 1 - y) * display_res.x * 4;
    float *bottom = pixels + y * display_res.x * 4;
    memcpy(tmp_row.data(), top,    row_bytes);
    memcpy(top,    bottom, row_bytes);
    memcpy(bottom, tmp_row.data(), row_bytes);
  }

  RE_engine_end_result(engine, layer, false, false, false);
}

void GameInstance::update_eval_members()
{
  /* DRW_context_get() is only valid inside a DRW draw/render callback.
   * Calling it outside will crash; update_eval_members() must only be called
   * from begin_sync(), which is itself called from within the DRW callback. */
  draw_ctx      = DRW_context_get();
  scene         = draw_ctx ? draw_ctx->scene      : nullptr;
  view_layer    = draw_ctx ? draw_ctx->view_layer : nullptr;
  depsgraph     = draw_ctx ? draw_ctx->depsgraph  : nullptr;
  manager       = draw_ctx ? draw_ctx->manager    : nullptr;
  render_engine_ = draw_ctx ? draw_ctx->render_engine : nullptr;

  /* Resolve the active camera from the scene via the Depsgraph so we get
   * the evaluated (animated) transform, not the original RNA data. */
  camera_eval_object = nullptr;
  if (scene && scene->camera) {
    camera_eval_object = DEG_get_evaluated_object(depsgraph, scene->camera);
  }
}

} // namespace blender::eevee_game
