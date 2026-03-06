#version 450
@shader_id: outline_mask
@hidden

// Flat white output for outline mask generation.
// The mask texture stores 1.0 where the selected object is visible.

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0, 1.0, 1.0, 1.0);
}
