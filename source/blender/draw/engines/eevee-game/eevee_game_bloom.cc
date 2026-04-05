/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "eevee_game_bloom.cc"
#include "eevee_game_instance.hh"

namespace blender::eevee_game {

BloomModule::BloomModule(GameInstance &inst) : inst_(&inst) {}

void BloomModule::init() {
    // Standard AAA Defaults
    settings_.threshold = 1.0f;
    settings_.intensity = 0.04f;
}

void BloomModule::sync() {
    int2 res = inst_->film.render_extent_get();
    
    // Allocate the pyramid
    for (int i = 0; i < PYRAMID_LEVELS; ++i) {
        res = math::max(int2(1), res / 2);
        bloom_pyramid_[i] = std::make_unique<gpu::Texture>();
        // RGBA16F is required to store HDR brightness without clipping
        bloom_pyramid_[i]->ensure_2d(gpu::TextureFormat::RGBA16F, res, 
                                     GPU_TEXTURE_USAGE_SHADER_WRITE | GPU_TEXTURE_USAGE_SHADER_READ);
    }

    bloom_downsample_ps_.init();
    bloom_downsample_ps_.shader_set(inst_->shaders.static_shader_get(SH_BLOOM_DOWNSAMPLE));

    bloom_upsample_ps_.init();
    bloom_upsample_ps_.shader_set(inst_->shaders.static_shader_get(SH_BLOOM_UPSAMPLE));
}

void BloomModule::render(gpu::Texture *input_color_tx) {
    if (!settings_.enabled) return;

    GPU_debug_group_begin("Bloom");

    // --- Phase 1: Downsampling (Building the Pyramid) ---
    gpu::Texture *current_src = input_color_tx;
    for (int i = 0; i < PYRAMID_LEVELS; ++i) {
        bloom_downsample_ps_.bind_texture("in_color_tx", current_src);
        bloom_downsample_ps_.bind_image("out_color_img", bloom_pyramid_[i].get());
        
        // Pass threshold only to the first level
        float4 params = (i == 0) ? float4(settings_.threshold, settings_.knee, 0, 0) : float4(-1.0f);
        bloom_downsample_ps_.push_constant("params", params);

        bloom_downsample_ps_.dispatch(math::divide_ceil(bloom_pyramid_[i]->size().xy(), int2(8)));
        bloom_downsample_ps_.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
        
        current_src = bloom_pyramid_[i].get();
    }

    // --- Phase 2: Upsampling (Blurring and Blending) ---
    for (int i = PYRAMID_LEVELS - 1; i > 0; --i) {
        // We blend the small blurry level into the larger level above it
        bloom_upsample_ps_.bind_texture("in_blur_tx", bloom_pyramid_[i].get());
        bloom_upsample_ps_.bind_image("out_color_img", bloom_pyramid_[i - 1].get());
        bloom_upsample_ps_.push_constant("radius", settings_.radius);

        bloom_upsample_ps_.dispatch(math::divide_ceil(bloom_pyramid_[i - 1]->size().xy(), int2(8)));
        bloom_upsample_ps_.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
    }

    GPU_debug_group_end();
}

} // namespace blender::eevee_game