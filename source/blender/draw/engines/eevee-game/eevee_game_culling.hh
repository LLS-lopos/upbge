/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "eevee_game_defines.hh"
#include "GPU_buffer.hh"

namespace blender::eevee_game {

// Data for a single object instance on the GPU
struct GPUInstanceData {
    float4x4 model_matrix;
    float3 bb_min;
    uint32_t object_id;
    float3 bb_max;
    uint32_t flags; // e.g., Shadow caster, Transparent, etc.
};

// Arguments for the Indirect Draw call (Matches OpenGL/Vulkan specs)
struct DrawCommand {
    uint32_t count;         // Indices count
    uint32_t instanceCount; // To be filled by Compute Shader
    uint32_t firstIndex;
    uint32_t baseVertex;
    uint32_t baseInstance;
};

class CullingModule {
public:
    CullingModule(class GameInstance &inst);
    ~CullingModule();

    void init();
    
    // Reset instance counters for the new frame
    void begin_sync();
    
    // Add an object to the GPU buffer instead of drawing it immediately
    void add_instance(Object *ob, uint32_t resource_id);
    
    // Run the Compute Shader to filter visible instances
    void execute_culling(View &view);

    // Buffers for Shaders
    gpu::StorageBuffer *get_instance_buffer() { return instance_data_sb_.get(); }
    gpu::StorageBuffer *get_visible_idx_buffer() { return visible_indices_sb_.get(); }

private:
    GameInstance *inst_;
    
    // Source data: All objects in the scene
    std::unique_ptr<gpu::StorageBuffer> instance_data_sb_;
    // Output data: Indices of objects that passed culling
    std::unique_ptr<gpu::StorageBuffer> visible_indices_sb_;
    // Output data: Arguments for DrawIndirect
    std::unique_ptr<gpu::StorageBuffer> indirect_draw_sb_;

    std::vector<GPUInstanceData> cpu_instance_cache_;
    PassSimple culling_ps_{"Culling.Execute"};
};

} // namespace blender::eevee_game