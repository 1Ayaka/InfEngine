# Light

<div class="class-info">
class in <b>InfEngine.components.builtin</b>
</div>

**Inherits from:** [BuiltinComponent](BuiltinComponent.md)

## Description

Python wrapper for the C++ Light component.

Properties delegate to the C++ ``Light`` object via CppProperty.
All changes are immediately reflected in the renderer.

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## Public Methods

| Method | Description |
|------|------|
| `get_light_view_matrix()` | Get the light's view matrix for shadow mapping. |
| `get_light_projection_matrix(shadow_extent: float = 20.0, near_plane: float = 0.1, far_plane: float = 100.0)` | Get the light's projection matrix for shadow mapping. |
| `serialize() → str` | Serialize Light to JSON string (delegates to C++). |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## Example

```python
# TODO: Add example for Light
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## See Also

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
