# Camera

<div class="class-info">
class in <b>InfEngine.components.builtin</b>
</div>

**Inherits from:** [BuiltinComponent](BuiltinComponent.md)

## Description

Python wrapper for the C++ Camera component.

Properties delegate to the C++ ``Camera`` via CppProperty.
Draws a Unity-style frustum wireframe gizmo when selected.

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## Properties

| Name | Type | Description |
|------|------|------|
| pixel_width | `int` | Render target width in pixels (read-only). *(read-only)* |
| pixel_height | `int` | Render target height in pixels (read-only). *(read-only)* |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## Public Methods

| Method | Description |
|------|------|
| `screen_to_world_point(x: float, y: float, depth: float = 0.0) → Optional[Tuple[float, float, float]]` | Convert screen coordinates (x, y) + depth [0..1] to world position. |
| `world_to_screen_point(x: float, y: float, z: float) → Optional[Tuple[float, float]]` | Convert world position to screen coordinates (x, y). |
| `screen_point_to_ray(x: float, y: float) → Optional[Tuple[Tuple[float, float, float], Tuple[float, float, float]]]` | Build a ray from viewport-relative screen coordinates. |
| `serialize() → str` | Serialize Camera to JSON string (delegates to C++). |
| `deserialize(json_str: str) → bool` | Deserialize Camera from JSON string (delegates to C++). |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## Lifecycle Methods

| Method | Description |
|------|------|
| `on_draw_gizmos_selected()` | Draw camera frustum wireframe and body icon when selected. |

<!-- USER CONTENT START --> lifecycle_methods

<!-- USER CONTENT END -->

## Example

```python
# TODO: Add example for Camera
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## See Also

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
