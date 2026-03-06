#version 450

@shader_id: shadow
@type: unlit
@hidden

// ShadowCaster fragment shader — depth-only pass.
// No color output needed; the GPU writes depth automatically.
// This shader is intentionally minimal/empty.
void main() {
}
