/* SPDX-FileCopyrightText: 2023 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

// Full-screen triangle UVs
in vec2 uvs;

// Parameters: x = threshold, y = strength
uniform vec4 fxaa_params;
uniform sampler2D color_tx;
layout(rgba16f) uniform writeonly image2D out_img;

#define FXAA_REDUCE_MIN   (1.0/128.0)

/**
 * Standard luminance calculation for edge detection
 */
float get_luma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    ivec2 tex_size = textureSize(color_tx, 0);
    vec2 texel_size = 1.0 / vec2(tex_size);
    ivec2 pixel_pos = ivec2(gl_FragCoord.xy);

    // Sample neighbors to detect contrast
    vec3 rgbNW = textureOffset(color_tx, uvs, ivec2(-1, -1)).rgb;
    vec3 rgbNE = textureOffset(color_tx, uvs, ivec2(1, -1)).rgb;
    vec3 rgbSW = textureOffset(color_tx, uvs, ivec2(-1, 1)).rgb;
    vec3 rgbSE = textureOffset(color_tx, uvs, ivec2(1, 1)).rgb;
    vec3 rgbM  = texture(color_tx, uvs).rgb;

    float lumaNW = get_luma(rgbNW);
    float lumaNE = get_luma(rgbNE);
    float lumaSW = get_luma(rgbSW);
    float lumaSE = get_luma(rgbSE);
    float lumaM  = get_luma(rgbM);

    // Find local contrast range
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Early exit if contrast is too low (saves performance in flat areas)
    if ((lumaMax - lumaMin) < fxaa_params.x) {
        imageStore(out_img, pixel_pos, vec4(rgbM, 1.0));
        return;
    }

    // Determine edge direction (gradient)
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * fxaa_params.y), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    // Scale gradient to pixel size
    dir = min(vec2(8.0, 8.0), max(vec2(-8.0, -8.0), dir * rcpDirMin)) * texel_size;

    // Perform two-stage directional blur
    vec3 rgbA = 0.5 * (
        texture(color_tx, uvs + dir * (1.0/3.0 - 0.5)).rgb +
        texture(color_tx, uvs + dir * (2.0/3.0 - 0.5)).rgb);
        
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(color_tx, uvs + dir * -0.5).rgb +
        texture(color_tx, uvs + dir *  0.5).rgb);

    // Validate result to avoid over-blurring narrow lines
    float lumaB = get_luma(rgbB);
    if ((lumaB < lumaMin) || (lumaB > lumaMax)) {
        imageStore(out_img, pixel_pos, vec4(rgbA, 1.0));
    } else {
        imageStore(out_img, pixel_pos, vec4(rgbB, 1.0));
    }
}