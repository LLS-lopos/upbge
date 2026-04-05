// FSR2 Mask Generation Shader
void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    vec3 opaque = texelFetch(opaque_color_tx, pos, 0).rgb;
    vec3 final  = texelFetch(final_color_tx, pos, 0).rgb;

    // Reactive Mask: How much did the transparents change the opaque pixel?
    // Formula: Reactive = max(0.0, 1.0 - (Luma(Opaque) / Luma(Final)))
    float lumaOpaque = dot(opaque, vec3(0.299, 0.587, 0.114));
    float lumaFinal  = dot(final, vec3(0.299, 0.587, 0.114));
    
    float reactive = max(0.0, 1.0 - (lumaOpaque / max(lumaFinal, 0.001)));
    
    // Manual refinement for AAA: boost reaction if it's a particle
    // (You can pass a specific 'particle_flag' from the forward pass via stencil)
    
    imageStore(out_reactive_img, pos, vec4(reactive));
}