#version 450

@shader_id: gizmo
@hidden

// Input from vertex shader
layout(location = 0) in vec3 fragColor;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // Output solid gizmo color (unlit, always visible)
    outColor = vec4(fragColor, 1.0);
}
