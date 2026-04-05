/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "eevee_game_raytrace.hh"
#include "eevee_game_instance.hh"

namespace blender::eevee_game {

RayTraceModule::RayTraceModule(GameInstance *inst) : inst_(inst) {}

RayTraceModule::~RayTraceModule() {}

void RayTraceModule::init() {
    // Initialize settings from the game's config
    settings_.max_roughness = 0.5f;
    settings_.max_steps = 64;
    settings_.thickness = 0.1f;
    settings_.edge_fade = 0.1f;
}

void RayTraceModule::sync() {
    ssr_ps_.init();
    ssr_ps_.shader_set(inst_->shaders.static_shader_get(SH_SSR_TRACE));
    
    // Bind the G-Buffer for roughness and normals
    ssr_ps_.bind_resources(inst_->gbuffer);
    
    // Bind the HIZ buffer for accelerated ray-marching
    ssr_ps_.bind_texture("hiz_tx", &inst_->hiz_buffer.front.ref_tx_);
    
    // Bind global uniforms (ViewProj matrices, etc.)
    ssr_ps_.bind_resources(inst_->uniform_data);
    ssr_ps_.bind_resources(inst_->sampling);
    
    // Push SSR settings as constants for fast shader access
    ssr_ps_.push_constant("ssr_settings", &settings_);
}

void RayTraceModule::render(gpu::Texture *radiance_tx, gpu::Texture *out_radiance) {
    if (!settings_.enabled) return;

    // The SSR pass is a full-screen compute/fragment dispatch
    // It takes the current radiance (color) and the depth (HIZ)
    // and outputs the reflected color to the out_radiance buffer.
    
    ssr_ps_.bind_texture("in_radiance_tx", radiance_tx);
    ssr_ps_.bind_image("out_radiance_img", out_radiance);
    
    // Dispatch the trace
    int2 extent = inst_->film.render_extent_get();
    ssr_ps_.dispatch(math::divide_ceil(extent, int2(RAYTRACE_GROUP_SIZE)));
    
    // Ensure the reflections are written before the final combine pass
    ssr_ps_.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
}

} // namespace blender::eevee_game