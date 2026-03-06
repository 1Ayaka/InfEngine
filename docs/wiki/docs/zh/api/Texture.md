# Texture

<div class="class-info">
类位于 <b>InfEngine.core</b>
</div>

## 描述

Pythonic wrapper around C++ TextureData.

Example::

    tex = Texture.load("textures/albedo.png")
    print(tex.width, tex.height, tex.channels)
    pixels = tex.pixels_as_bytes()

    import numpy as np
    arr = np.frombuffer(pixels, dtype=np.uint8).reshape(
        tex.height, tex.width, tex.channels
    )

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## 构造函数

| 签名 | 描述 |
|------|------|
| `Texture.__init__(native: TextureData) → None` |  |

<!-- USER CONTENT START --> constructors

<!-- USER CONTENT END -->

## 属性

| 名称 | 类型 | 描述 |
|------|------|------|
| native | `TextureData` |  *(只读)* |
| width | `int` |  *(只读)* |
| height | `int` |  *(只读)* |
| channels | `int` |  *(只读)* |
| name | `str` |  *(只读)* |
| source_path | `str` |  *(只读)* |
| size | `Tuple[int, int]` | ``(width, height)`` tuple. *(只读)* |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## 公共方法

| 方法 | 描述 |
|------|------|
| `pixels_as_bytes() → bytes` | Get raw pixel data as bytes (row-major, RGBA or RGB). |
| `pixels_as_list() → list` | Get pixel data as a flat list of integers ``[0-255]``. |
| `to_numpy() → 'numpy.ndarray'` | Convert pixel data to a NumPy array ``(H, W, C)``, dtype ``uint8``. |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## 静态方法

| 方法 | 描述 |
|------|------|
| `static Texture.load(file_path: str) → Optional[Texture]` | Load a texture from an image file (PNG, JPG, BMP, TGA). |
| `static Texture.from_memory(data: bytes, width: int, height: int, channels: int = ..., name: str = ...) → Optional[Texture]` | Create a texture from raw pixel data in memory. |
| `static Texture.solid_color(width: int, height: int, r: int = ..., g: int = ..., b: int = ..., a: int = ...) → Optional[Texture]` | Create a solid color texture. |
| `static Texture.checkerboard(width: int, height: int, cell_size: int = ...) → Optional[Texture]` | Create a checkerboard pattern texture. |
| `static Texture.from_native(native: TextureData) → Texture` | Wrap an existing C++ TextureData. |

<!-- USER CONTENT START --> static_methods

<!-- USER CONTENT END -->

## 运算符

| 方法 | 返回值 |
|------|------|
| `__repr__() → str` | `str` |

<!-- USER CONTENT START --> operators

<!-- USER CONTENT END -->

## 示例

```python
# TODO: Add example for Texture
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## 另请参阅

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
