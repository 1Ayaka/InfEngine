#version 450
@shader_id: outline
@hidden
@property: _OutlineWidth, Float, 0.03

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
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec2 inTexCoord;

void main() {
    // Extrude
    vec3 pos = inPosition + normalize(inNormal) * material._OutlineWidth;
    gl_Position = ubo.proj * ubo.view * pc.model * vec4(pos, 1.0);
}
