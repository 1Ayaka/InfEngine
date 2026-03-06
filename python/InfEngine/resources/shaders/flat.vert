#version 450
@shader_id: flat_white
@hidden

layout(std140, binding = 0) uniform UniformBufferObject {
    mat4 model;  // unused — kept for UBO layout compatibility
    mat4 view;
    mat4 proj;
} ubo;

// Per-object model matrix + normal matrix via push constant
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMat;  // only used by lit shader; declared for uniform layout
} pc;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = ubo.proj * ubo.view * pc.model * vec4(inPosition, 1.0);
}
