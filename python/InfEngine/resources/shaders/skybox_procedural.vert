#version 450

@shader_id: InfEngine/Skybox-Procedural
@type: unlit
@cull: back
@depth_write: false
@depth_test: less_equal
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

// Vertex attributes (cube mesh)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec2 inTexCoord;

// Output to fragment shader
layout(location = 0) out vec3 fragWorldDir;

void main() {
    // Use cube vertex position as the viewing direction
    fragWorldDir = inPosition;

    // Strip translation from view matrix (skybox is always centered on camera)
    mat4 viewNoTranslation = mat4(mat3(ubo.view));

    // Transform the skybox cube
    vec4 clipPos = ubo.proj * viewNoTranslation * vec4(inPosition, 1.0);

    // Set z = w so depth is always at the far plane (1.0 after perspective divide)
    // Combined with depth test <= and no depth write, skybox renders behind everything
    gl_Position = clipPos.xyww;
}
