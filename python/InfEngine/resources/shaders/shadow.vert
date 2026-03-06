#version 450

@shader_id: shadow
@type: unlit
@hidden

// Uniform buffer for VP matrices (shared scene UBO)
layout(std140, binding = 0) uniform UniformBufferObject {
    mat4 model;  // unused — kept for UBO layout compatibility
    mat4 view;
    mat4 proj;
} ubo;

// Per-object model matrix via push constant
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMat;
} pc;

// Vertex attributes (must match Vertex struct)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec2 inTexCoord;

void main() {
    // ShadowCaster pass: only need clip-space position for depth writing.
    // Uses the light's VP matrix (stored in ubo.view/proj when rendering shadow pass)
    // and the object's model matrix from push constants.
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
}
