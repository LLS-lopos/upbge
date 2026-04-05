/* Modified for eevee_game */
#include "gpu_shader_smaa_lib.glsl"

void main() {
    // ... (Mantenemos offset0, offset1, offset2 igual que Workbench) ...

#if SMAA_STAGE == 0
    out_edges = SMAALumaEdgeDetectionPS(uvs, offset, color_tx);
    if (dot(out_edges, float2(1.0f)) == 0.0f) {
        gpu_discard_fragment();
        return;
    }

#elif SMAA_STAGE == 1
    out_weights = SMAABlendingWeightCalculationPS(
        uvs, pixcoord, offset, edges_tx, area_tx, search_tx, float4(0.0));

#elif SMAA_STAGE == 2
    // SIMPLIFIED BLENDING FOR EEVEE_GAME (Linear HDR)
    // We don't need log space or TAA weights
    out_color = SMAANeighborhoodBlendingPS(uvs, offset[0], color_tx, blend_tx);
    
    // Ensure alpha is consistent for game overlays
    if (out_color.a > 0.999f) out_color.a = 1.0f;
#endif
}