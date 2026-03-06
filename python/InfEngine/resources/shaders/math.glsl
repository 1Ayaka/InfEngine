@shader_id: math

// ============================================================================
// math.glsl — Shared math constants and utility functions
// ============================================================================

const float PI = 3.14159265359;
const float INV_PI = 0.31830988618;
const float HALF_PI = 1.57079632679;
const float TWO_PI = 6.28318530718;
const float EPSILON = 0.0001;

float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

vec3 saturateVec3(vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}
