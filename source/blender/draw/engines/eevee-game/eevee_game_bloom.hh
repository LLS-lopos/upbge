/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "eevee_game_defines.hh"
#include "GPU_texture.hh"

namespace blender::eevee_game {

struct BloomSettings {
    float threshold = 1.0f;      // Only pixels brighter than this glow
    float knee = 0.1f;           // Softens the threshold transition
    float intensity = 0.05f;     // Overall strength of the glow
    float radius = 0.85f;        // How far the bloom spreads
    bool enabled = true;
};

class BloomModule {
public:
    BloomModule(class GameInstance &inst);
    
    void init();
    void sync();
    
    /**
     * Executes the Bloom pyramid chain.
     * @param input_color_tx The HDR scene color.
     */
    void render(gpu::Texture *input_color_tx);

    gpu::Texture *get_result() { return bloom_pyramid_[0].get(); }

private:
    GameInstance *inst_;
    BloomSettings settings_;
    
    // A chain of textures, each half the size of the previous one
    static constexpr int PYRAMID_LEVELS = 6;
    std::unique_ptr<gpu::Texture> bloom_pyramid_[PYRAMID_LEVELS];

    PassSimple bloom_downsample_ps_{"Bloom.Downsample"};
    PassSimple bloom_upsample_ps_{"Bloom.Upsample"};
};

} // namespace blender::eevee_game