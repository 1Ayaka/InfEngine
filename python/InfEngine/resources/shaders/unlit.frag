#version 450

@shader_id: unlit
@property: baseColor, Float4, [1.0, 1.0, 1.0, 1.0]

// Import shared math utilities
@import: math

// Texture sampler
layout(binding = 1) uniform sampler2D texSampler;

// Input from vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // Sample texture
    vec4 texColor = texture(texSampler, fragTexCoord);
    
    // Apply material base color and vertex color
    outColor = texColor * material.baseColor * vec4(fragColor, 1.0);
}
