#version 450
@shader_id: outline_mask
@hidden

// Minimal vertex shader for outline mask generation.
// Renders the selected object as a white silhouette to a mask texture.
// Uses scene UBO (VP matrices) and push constants (model matrix) — same as scene shaders.

layout(std140, binding = 0) uniform UniformBufferObject {
    mat4 model;  // unused — kept for UBO layout compatibility
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMat;
} pc;

// Vertex attributes — must match engine Vertex struct layout
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec2 inTexCoord;

void main() {
    gl_Position = ubo.proj * ubo.view * pc.model * vec4(inPosition, 1.0);
}
