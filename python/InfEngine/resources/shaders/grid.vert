#version 450

@shader_id: InfEngine/Grid
@type: unlit
@cull: none
@hidden

// Scene UBO (VP matrices)
layout(std140, binding = 0) uniform UniformBufferObject {
    mat4 model;   // unused
    mat4 view;
    mat4 proj;
} ubo;

// Per-object push constants
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMat;
} pc;

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec2 inTexCoord;

// Output to fragment shader
layout(location = 0) out vec3 fragWorldPos;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    fragWorldPos = worldPos.xyz;
}
