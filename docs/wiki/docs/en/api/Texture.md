# Texture

<div class="class-info">
class in <b>InfEngine.core</b>
</div>

## Description

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

## Constructors

| Signature | Description |
|------|------|
| `Texture.__init__(native: TextureData) → None` |  |

<!-- USER CONTENT START --> constructors

<!-- USER CONTENT END -->

## Properties

| Name | Type | Description |
|------|------|------|
| native | `TextureData` |  *(read-only)* |
| width | `int` |  *(read-only)* |
| height | `int` |  *(read-only)* |
| channels | `int` |  *(read-only)* |
| name | `str` |  *(read-only)* |
| source_path | `str` |  *(read-only)* |
| size | `Tuple[int, int]` | ``(width, height)`` tuple. *(read-only)* |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## Public Methods

| Method | Description |
|------|------|
| `pixels_as_bytes() → bytes` | Get raw pixel data as bytes (row-major, RGBA or RGB). |
| `pixels_as_list() → list` | Get pixel data as a flat list of integers ``[0-255]``. |
| `to_numpy() → 'numpy.ndarray'` | Convert pixel data to a NumPy array ``(H, W, C)``, dtype ``uint8``. |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## Static Methods

| Method | Description |
|------|------|
| `static Texture.load(file_path: str) → Optional[Texture]` | Load a texture from an image file (PNG, JPG, BMP, TGA). |
| `static Texture.from_memory(data: bytes, width: int, height: int, channels: int = ..., name: str = ...) → Optional[Texture]` | Create a texture from raw pixel data in memory. |
| `static Texture.solid_color(width: int, height: int, r: int = ..., g: int = ..., b: int = ..., a: int = ...) → Optional[Texture]` | Create a solid color texture. |
| `static Texture.checkerboard(width: int, height: int, cell_size: int = ...) → Optional[Texture]` | Create a checkerboard pattern texture. |
| `static Texture.from_native(native: TextureData) → Texture` | Wrap an existing C++ TextureData. |

<!-- USER CONTENT START --> static_methods

<!-- USER CONTENT END -->

## Operators

| Method | Returns |
|------|------|
| `__repr__() → str` | `str` |

<!-- USER CONTENT START --> operators

<!-- USER CONTENT END -->

## Example

```python
# TODO: Add example for Texture
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## See Also

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
