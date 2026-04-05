/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "eevee_game_culling.hh"
#include "eevee_game_instance.hh"

namespace blender::eevee_game {

CullingModule::CullingModule(GameInstance &inst) : inst_(&inst) {}

void CullingModule::init() {
    // Allocation of large SSBOs for object data
    // 64,000 instances is a standard AAA limit
    instance_data_sb_ = std::make_unique<gpu::StorageBuffer>(64000 * sizeof(GPUInstanceData), GPU_USAGE_DYNAMIC);
    visible_indices_sb_ = std::make_unique<gpu::StorageBuffer>(64000 * sizeof(uint32_t), GPU_USAGE_DEVICE_ONLY);
    indirect_draw_sb_ = std::make_unique<gpu::StorageBuffer>(1000 * sizeof(DrawCommand), GPU_USAGE_DEVICE_ONLY);
}

void CullingModule::begin_sync() {
    cpu_instance_cache_.clear();
}

void CullingModule::add_instance(Object *ob, uint32_t resource_id) {
    // Fill the CPU cache with object data from UPBGE
    GPUInstanceData data;
    data.model_matrix = ob->object_to_world();
    
    // Get Bounding Box from Blender DNA
    BoundBox *bb = static_cast<BoundBox*>(ob->data); 
    data.bb_min = float3(bb->vec[0]);
    data.bb_max = float3(bb->vec[6]);
    
    data.object_id = resource_id;
    data.flags = 0; // Logic for shadows/materials
    
    cpu_instance_cache_.push_back(data);
}

void CullingModule::execute_culling(View &view) {
    if (cpu_instance_cache_.empty()) return;

    // 1. Upload all scene transforms to GPU
    instance_data_sb_->update(cpu_instance_cache_.data());

    // 2. Setup Compute Culling Shader
    culling_ps_.init();
    culling_ps_.shader_set(inst_->shaders.static_shader_get(SH_CULLING_COMPUTE));
    
    // Bind the Scene data
    culling_ps_.bind_ssbo("instance_data_buf", instance_data_sb_.get());
    culling_ps_.bind_ssbo("visible_indices_buf", visible_indices_sb_.get());
    culling_ps_.bind_ssbo("indirect_draw_buf", indirect_draw_sb_.get());
    
    // Bind Hi-Z for Occlusion Culling (The AAA part)
    culling_ps_.bind_texture("hiz_tx", &inst_->hiz_buffer.front.ref_tx_);
    
    culling_ps_.bind_resources(inst_->uniform_data);
    
    // 3. Dispatch the culling
    uint32_t num_instances = cpu_instance_cache_.size();
    culling_ps_.dispatch(math::divide_ceil(num_instances, 64));

    // Barrier: Ensure culling is done before the drawing starts
    culling_ps_.barrier(GPU_BARRIER_SHADER_STORAGE | GPU_BARRIER_COMMAND_BUFFER);
}

} // namespace blender::eevee_game