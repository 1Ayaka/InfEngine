#version 450

@shader_id: gizmo
@hidden

// Uniform buffer for VP matrices
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

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec2 inTexCoord;

// Output to fragment shader
layout(location = 0) out vec3 fragColor;

void main() {
    // Transform vertex position (gizmos push identity model matrix)
    gl_Position = ubo.proj * ubo.view * pc.model * vec4(inPosition, 1.0);
    
    // Pass through vertex color (gizmos use vertex color for line colors)
    fragColor = inColor;
}
