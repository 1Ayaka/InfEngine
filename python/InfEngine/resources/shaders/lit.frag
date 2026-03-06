#version 450

@shader_id: lit
@type: lit
@property: baseColor, Float4, [1.0, 1.0, 1.0, 1.0]
@property: metallic, Float, 0.0
@property: roughness, Float, 0.5
@property: ambientOcclusion, Float, 1.0
@property: emissionColor, Float4, [0.0, 0.0, 0.0, 0.0]
@property: normalScale, Float, 1.0
@property: specularHighlights, Float, 1.0

// Import shared PBR functions (includes math.glsl transitively)
@import: pbr

// Texture samplers
layout(binding = 2) uniform sampler2D texSampler;   // Albedo map
layout(binding = 4) uniform sampler2D normalMap;     // Normal map (tangent-space, default = flat)

// ============================================================================
// Input from vertex shader
// ============================================================================
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragTangent;
layout(location = 3) in vec3 fragColor;
layout(location = 4) in vec2 fragTexCoord;

// ============================================================================
// Output
// ============================================================================
layout(location = 0) out vec4 outColor;

// ============================================================================
// Normal Mapping
// ============================================================================
vec3 getNormal() {
    vec3 N = normalize(fragNormal);

    // If normalScale is zero, skip normal mapping entirely
    if (material.normalScale < EPSILON) {
        return N;
    }

    // Sample and decode normal map: [0, 1] → [-1, 1]
    vec3 tangentNormal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;

    // Detect flat/default normal map (sampled value ≈ (0, 0, 1) in tangent space)
    // If the map is essentially flat, no need for expensive TBN transform
    if (abs(tangentNormal.x) < 0.01 && abs(tangentNormal.y) < 0.01) {
        return N;
    }

    // Apply normalScale: scale XY perturbation, re-normalize
    tangentNormal = normalize(vec3(tangentNormal.xy * material.normalScale, tangentNormal.z));

    // Build TBN matrix from interpolated vertex attributes
    vec3 T = normalize(fragTangent.xyz);
    // Gram-Schmidt re-orthogonalize T w.r.t. N
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * fragTangent.w; // .w = handedness sign (±1)

    return normalize(mat3(T, B, N) * tangentNormal);
}

// ============================================================================
// Shadow Mapping — PCF soft shadows
// ============================================================================

/**
 * Calculate shadow factor for the first directional light.
 *
 * Transforms the world-space fragment position into the light's clip space
 * using lightVP[0], then performs a depth comparison against the shadow map.
 * Supports both hard shadows (1-tap) and soft shadows (PCF).
 *
 * shadowParams.w encoding: 0=disabled, 1=hard, 2=soft
 * Returns: 1.0 = fully lit, 0.0 = fully shadowed
 */
float calculateShadow(vec3 worldPos, vec3 normal) {
    // Check if shadow mapping is enabled (shadowParams.w > 0)
    DirectionalLightData light = lighting.directionalLights[0];
    float shadowType = light.shadowParams.w;
    if (shadowType < 0.5) {
        return 1.0;
    }

    // Also check global shadow enable flag
    if (lighting.shadowMapParams.y < 0.5) {
        return 1.0;
    }

    // Transform world pos to light clip space
    vec4 lightClipPos = lighting.lightVP[0] * vec4(worldPos, 1.0);

    // Perspective divide (ortho: w=1, but safe for perspective too)
    vec3 projCoords = lightClipPos.xyz / lightClipPos.w;

    // Vulkan NDC: x,y ∈ [-1, 1], z ∈ [0, 1]
    // Convert xy to [0, 1] for texture sampling
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Fragments outside the shadow map are not in shadow
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0 || projCoords.z < 0.0) {
        return 1.0;
    }

    float currentDepth = projCoords.z;

    // Minimal shader-side bias (GPU pipeline already applies depth bias via
    // depthBiasConstantFactor/depthBiasSlopeFactor in ShadowMapRenderer)
    vec3 lightDir = normalize(-light.direction.xyz);
    float cosTheta = max(dot(normal, lightDir), 0.0);
    float bias = 0.0005 * (1.0 - cosTheta); // very small normal-dependent bias
    bias = max(bias, 0.0001);

    float shadow = 0.0;
    float texelSize = 1.0 / lighting.shadowMapParams.x;

    // Hard shadow: single tap, no filtering
    if (shadowType < 1.5) {
        float closestDepth = texture(shadowMap, projCoords.xy).r;
        shadow = (currentDepth - bias > closestDepth) ? 0.0 : 1.0;
    }
    // Soft shadow: 5x5 PCF for smoother edges
    else {
        for (int x = -2; x <= 2; ++x) {
            for (int y = -2; y <= 2; ++y) {
                vec2 offset = vec2(float(x), float(y)) * texelSize;
                float closestDepth = texture(shadowMap, projCoords.xy + offset).r;
                shadow += (currentDepth - bias > closestDepth) ? 0.0 : 1.0;
            }
        }
        shadow /= 25.0; // Average of 25 samples
    }

    // Apply shadow strength from light.shadowParams.x
    float shadowStrength = light.shadowParams.x;
    return mix(1.0, shadow, shadowStrength);
}

// ============================================================================
// Main
// ============================================================================
void main() {
    // ---- Sample material properties ----
    vec4  texColor  = texture(texSampler, fragTexCoord);
    vec3  albedo    = texColor.rgb * fragColor * material.baseColor.rgb;
    float metallic  = material.metallic;
    float roughness = max(material.roughness, 0.04); // Prevent div-by-zero
    float ao        = material.ambientOcclusion;

    // ---- Normal (world space, with optional normal mapping) ----
    vec3 N = getNormal();
    vec3 V = normalize(lighting.cameraPos.xyz - fragWorldPos);

    // ---- Base reflectance ----
    // Dielectric: F0 ≈ 0.04; Metal: F0 = albedo
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // =================== Direct Lighting ===================
    vec3 Lo = vec3(0.0);

    // Compute shadow factor for the primary directional light
    float shadow = calculateShadow(fragWorldPos, N);

    // Directional lights
    for (int i = 0; i < lighting.lightCounts.x && i < MAX_DIRECTIONAL_LIGHTS; ++i) {
        DirectionalLightData light = lighting.directionalLights[i];
        vec3 L        = normalize(-light.direction.xyz);
        vec3 radiance = light.color.rgb * light.color.w;
        // Apply shadow only to the first directional light (main light)
        float lightShadow = (i == 0) ? shadow : 1.0;
        Lo += evaluatePBRLight(N, V, L, radiance, albedo, metallic, roughness, F0) * lightShadow;
    }

    // Point lights (URP-style smooth attenuation, no hard cutoff)
    for (int i = 0; i < lighting.lightCounts.y && i < MAX_POINT_LIGHTS; ++i) {
        PointLightData light = lighting.pointLights[i];
        vec3  lightVec = light.position.xyz - fragWorldPos;
        float distance = length(lightVec);
        float range    = light.position.w;

        vec3  L           = normalize(lightVec);
        float attenuation = calculateAttenuation(light.attenuation.xyz, distance);
        // Skip if attenuation is negligible (beyond effective range)
        if (attenuation > 0.001) {
            vec3  radiance    = light.color.rgb * light.color.w * attenuation;
            Lo += evaluatePBRLight(N, V, L, radiance, albedo, metallic, roughness, F0);
        }
    }

    // Spot lights (URP-style smooth attenuation, no hard cutoff)
    for (int i = 0; i < lighting.lightCounts.z && i < MAX_SPOT_LIGHTS; ++i) {
        SpotLightData light = lighting.spotLights[i];
        vec3  lightVec = light.position.xyz - fragWorldPos;
        float distance = length(lightVec);
        float range    = light.position.w;

        vec3 L = normalize(lightVec);
        float spotFalloff = calculateSpotFalloff(L, light.direction.xyz,
                                                  light.spotParams.x, light.spotParams.y);
        if (spotFalloff > 0.0) {
            float attenuation = calculateAttenuation(light.attenuation.xyz, distance);
            if (attenuation > 0.001) {
                vec3  radiance    = light.color.rgb * light.color.w * attenuation * spotFalloff;
                Lo += evaluatePBRLight(N, V, L, radiance, albedo, metallic, roughness, F0);
            }
        }
    }

    // =================== Ambient / Environment Approximation ===================
    vec3 ambientColor = lighting.ambientColor.rgb * lighting.ambientColor.w;
    vec3 kS_env = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD_env = (1.0 - kS_env) * (1.0 - metallic);

    // Specular highlights toggle (0.0 = diffuse only, 1.0 = full specular)
    vec3 specEnv = mix(vec3(0.0), kS_env * ambientColor * 0.3, material.specularHighlights);
    vec3 ambient = (kD_env * albedo * ambientColor + specEnv) * ao;

    // =================== Emission ===================
    vec3 emission = material.emissionColor.rgb * material.emissionColor.a;

    // =================== Final Composition ===================
    vec3 color = ambient + Lo + emission;

    // Output linear HDR — tonemapping and gamma correction are handled
    // by the post-process stack.  Applying them here would cause
    // double-tonemapping when a post-process tonemapping effect is active.

    outColor = vec4(color, texColor.a * material.baseColor.a);
}
