# Material

<div class="class-info">
class in <b>InfEngine.core</b>
</div>

## Description

Pythonic wrapper around C++ InfMaterial.

Provides context manager support, clean property setters/getters,
and factory methods matching Unity's Material API.

Example::

    mat = Material.create_lit("MyPBR")
    mat.set_color("_BaseColor", 1.0, 0.5, 0.0)
    mat.set_float("_Metallic", 0.9)

    with Material.create_lit("Temp") as mat:
        mat.set_float("_Roughness", 0.5)

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## Constructors

| Signature | Description |
|------|------|
| `Material.__init__(native: InfMaterial) → None` |  |

<!-- USER CONTENT START --> constructors

<!-- USER CONTENT END -->

## Properties

| Name | Type | Description |
|------|------|------|
| native | `InfMaterial` |  *(read-only)* |
| name | `str` |  |
| guid | `str` |  *(read-only)* |
| render_queue | `int` |  |
| vertex_shader_path | `str` |  |
| fragment_shader_path | `str` |  |
| is_builtin | `bool` |  *(read-only)* |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## Public Methods

| Method | Description |
|------|------|
| `dispose() → None` | Release this material from the MaterialManager. |
| `set_float(name: str, value: float) → None` |  |
| `set_int(name: str, value: int) → None` |  |
| `set_color(name: str, r: float, g: float, b: float, a: float = ...) → None` |  |
| `set_vector2(name: str, x: float, y: float) → None` |  |
| `set_vector3(name: str, x: float, y: float, z: float) → None` |  |
| `set_vector4(name: str, x: float, y: float, z: float, w: float) → None` |  |
| `set_texture(name: str, texture_path: str) → None` |  |
| `get_float(name: str, default: float = ...) → float` |  |
| `get_int(name: str, default: int = ...) → int` |  |
| `get_color(name: str) → Tuple[float, float, float, float]` |  |
| `get_vector3(name: str) → Tuple[float, float, float]` |  |
| `get_texture(name: str) → Optional[str]` |  |
| `to_dict() → dict` |  |
| `save(file_path: str) → bool` | Save material to a ``.mat`` file. |
| `register(engine: Optional[object] = ...) → bool` | Register this material with the global MaterialManager. |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## Static Methods

| Method | Description |
|------|------|
| `static Material.create_lit(name: str = ...) → Material` | Create a new PBR lit material. |
| `static Material.create_unlit(name: str = ...) → Material` | Create a new unlit material. |
| `static Material.from_native(native: InfMaterial) → Material` | Wrap an existing C++ InfMaterial. |
| `static Material.load(file_path: str) → Optional[Material]` | Load a material from a ``.mat`` file. |
| `static Material.get(name: str) → Optional[Material]` | Look up a material by name in the global MaterialManager. |

<!-- USER CONTENT START --> static_methods

<!-- USER CONTENT END -->

## Operators

| Method | Returns |
|------|------|
| `__repr__() → str` | `str` |
| `__eq__(other: object) → bool` | `bool` |
| `__hash__() → int` | `int` |

<!-- USER CONTENT START --> operators

<!-- USER CONTENT END -->

## Example

```python
# TODO: Add example for Material
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## See Also

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
