@shader_id: pbr

// ============================================================================
// pbr.glsl — Cook-Torrance GGX BRDF Functions
// Requires: math.glsl (PI)
// ============================================================================

@import: math

// ============================================================================
// PBR Core Functions (Cook-Torrance GGX)
// ============================================================================

// GGX/Trowbridge-Reitz Normal Distribution Function
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom  = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Schlick-GGX Geometry Function (single direction)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's Geometry Function (both V and L directions)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick Approximation
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with roughness (for ambient/environment term)
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// Utility Functions
// ============================================================================

// URP-style smooth distance attenuation
// Guarantees smooth falloff to zero at range boundary
// attenParams.x = range (yz unused)
float calculateAttenuation(vec3 attenParams, float distance) {
    float range = attenParams.x;
    float d2 = distance * distance;
    float r2 = range * range;
    // URP smooth factor: saturate(1 - (d/r)^4)^2 / (d^2 + 1)
    float ratio2 = d2 / r2;
    float factor = saturate(1.0 - ratio2 * ratio2);
    return (factor * factor) / (d2 + 1.0);
}

// Spotlight cone falloff
float calculateSpotFalloff(vec3 lightDir, vec3 spotDir, float innerAngleCos, float outerAngleCos) {
    float theta   = dot(lightDir, normalize(-spotDir));
    float epsilon = innerAngleCos - outerAngleCos;
    return clamp((theta - outerAngleCos) / epsilon, 0.0, 1.0);
}

// ============================================================================
// Cook-Torrance BRDF evaluation for a single light
// ============================================================================
vec3 evaluatePBRLight(vec3 N, vec3 V, vec3 L, vec3 lightRadiance, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3  H     = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    if (NdotL <= 0.0) return vec3(0.0);

    // Cook-Torrance specular BRDF terms
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3  F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  numerator   = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + EPSILON;
    vec3  specular    = numerator / denominator;

    // Energy conservation: specular reflection vs diffuse
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic); // Metals have no diffuse

    return (kD * albedo / PI + specular) * lightRadiance * NdotL;
}
