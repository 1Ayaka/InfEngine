#version 450

@shader_id: InfEngine/Skybox-Procedural
@type: unlit
@cull: back
@depth_write: false
@depth_test: less_equal
@hidden
@property: skyTopColor, Float4, [0.02, 0.08, 0.28, 1.0]
@property: skyHorizonColor, Float4, [0.18, 0.25, 0.38, 1.0]
@property: groundColor, Float4, [0.08, 0.07, 0.12, 1.0]
@property: sunSize, Float, 0.04
@property: sunIntensity, Float, 1.2
@property: sunDirection, Float4, [0.5, 0.7, 0.3, 0.0]
@property: exposure, Float, 0.8

// Input from vertex shader
layout(location = 0) in vec3 fragWorldDir;

// Output
layout(location = 0) out vec4 outColor;

// ============================================================================
// Procedural Sky
// ============================================================================

void main() {
    vec3 dir = normalize(fragWorldDir);

    // ---- Sky gradient ----
    // Y component: +1 = zenith, 0 = horizon, -1 = nadir
    float y = dir.y;

    // Upper hemisphere: interpolate from horizon to zenith
    // Use smoothstep for softer horizon transition
    vec3 skyColor;
    if (y >= 0.0) {
        float t = smoothstep(0.0, 0.4, y); // 0 at horizon, 1 at ~23 degrees up
        skyColor = mix(material.skyHorizonColor.rgb, material.skyTopColor.rgb, t);
    } else {
        // Lower hemisphere: ground color with soft transition
        float t = smoothstep(0.0, -0.1, y);
        skyColor = mix(material.skyHorizonColor.rgb, material.groundColor.rgb, t);
    }

    // ---- Horizon haze ----
    // Add subtle brightness boost near the horizon
    float horizonGlow = 1.0 - abs(y);
    horizonGlow = pow(horizonGlow, 8.0) * 0.15;
    skyColor += vec3(horizonGlow);

    // ---- Sun disc ----
    // Sun direction from material property (can be synced to directional light via Python)
    vec3 sunDir = normalize(material.sunDirection.xyz);

    float sunDot = dot(dir, sunDir);
    float sunRadius = material.sunSize;

    // Sharp sun disc
    float sunDisc = smoothstep(1.0 - sunRadius, 1.0 - sunRadius * 0.5, sunDot);
    vec3 sunColor = vec3(1.0, 0.95, 0.85) * material.sunIntensity * sunDisc;

    // Soft sun glow (corona)
    float sunGlow = pow(max(sunDot, 0.0), 128.0) * 0.4;
    sunColor += vec3(1.0, 0.9, 0.7) * sunGlow;

    // Only show sun above horizon
    sunColor *= smoothstep(-0.02, 0.02, y);

    // ---- Final composition ----
    vec3 color = skyColor + sunColor;

    // Exposure
    color *= material.exposure;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
