#version 450

@shader_id: lit
@type: lit

// Uniform buffer for VP matrices
layout(std140, binding = 0) uniform UniformBufferObject {
    mat4 model;  // unused — kept for UBO layout compatibility
    mat4 view;
    mat4 proj;
} ubo;

// Per-object model matrix + normal matrix via push constant
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMat;  // mat3 packed in mat4 for alignment (only upper-left 3x3 used)
} pc;

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec2 inTexCoord;

// Output to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragTangent;
layout(location = 3) out vec3 fragColor;
layout(location = 4) out vec2 fragTexCoord;

void main() {
    // Transform vertex position to world space using per-object model matrix
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space (precomputed normal matrix from CPU)
    mat3 normalMatrix = mat3(pc.normalMat);
    fragNormal = normalize(normalMatrix * inNormal);
    
    // Transform tangent to world space
    fragTangent = vec4(normalize(normalMatrix * inTangent.xyz), inTangent.w);
    
    // Pass through color and texture coordinates
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    
    // Final clip space position
    gl_Position = ubo.proj * ubo.view * worldPos;
}
