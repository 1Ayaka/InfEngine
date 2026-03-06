# Material

<div class="class-info">
类位于 <b>InfEngine.core</b>
</div>

## 描述

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

## 构造函数

| 签名 | 描述 |
|------|------|
| `Material.__init__(native: InfMaterial) → None` |  |

<!-- USER CONTENT START --> constructors

<!-- USER CONTENT END -->

## 属性

| 名称 | 类型 | 描述 |
|------|------|------|
| native | `InfMaterial` |  *(只读)* |
| name | `str` |  |
| guid | `str` |  *(只读)* |
| render_queue | `int` |  |
| vertex_shader_path | `str` |  |
| fragment_shader_path | `str` |  |
| is_builtin | `bool` |  *(只读)* |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## 公共方法

| 方法 | 描述 |
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

## 静态方法

| 方法 | 描述 |
|------|------|
| `static Material.create_lit(name: str = ...) → Material` | Create a new PBR lit material. |
| `static Material.create_unlit(name: str = ...) → Material` | Create a new unlit material. |
| `static Material.from_native(native: InfMaterial) → Material` | Wrap an existing C++ InfMaterial. |
| `static Material.load(file_path: str) → Optional[Material]` | Load a material from a ``.mat`` file. |
| `static Material.get(name: str) → Optional[Material]` | Look up a material by name in the global MaterialManager. |

<!-- USER CONTENT START --> static_methods

<!-- USER CONTENT END -->

## 运算符

| 方法 | 返回值 |
|------|------|
| `__repr__() → str` | `str` |
| `__eq__(other: object) → bool` | `bool` |
| `__hash__() → int` | `int` |

<!-- USER CONTENT START --> operators

<!-- USER CONTENT END -->

## 示例

```python
# TODO: Add example for Material
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## 另请参阅

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
