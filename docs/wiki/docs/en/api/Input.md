# Input

<div class="class-info">
class in <b>InfEngine.input</b>
</div>

## Description

Static input query class — mirrors ``UnityEngine.Input`` in snake_case.

All methods are class-level (``@staticmethod``).  No instantiation needed.
Properties like ``Input.mouse_position`` are accessed without parentheses
(powered by the ``_InputMeta`` metaclass).

The class holds an internal ``_game_focused`` flag that, when ``False``,
causes all query methods to return idle values.  This lets the editor
suppress game-object input when the Game View is not focused.

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## Static Methods

| Method | Description |
|------|------|
| `static Input.set_game_focused(focused: bool) → None` | Enable or disable game-input queries. |
| `static Input.set_game_viewport_origin(x: float, y: float) → None` | Store the absolute pixel position of the game image top-left. |
| `static Input.is_game_focused() → bool` | Return whether the Game View is currently focused. |
| `static Input.get_key(key: Union[str, int]) → bool` | ``True`` while *key* is held down. |
| `static Input.get_key_down(key: Union[str, int]) → bool` | ``True`` during the frame *key* was first pressed. |
| `static Input.get_key_up(key: Union[str, int]) → bool` | ``True`` during the frame *key* was released. |
| `static Input.get_mouse_button(button: int) → bool` | ``True`` while *button* is held (0=left, 1=right, 2=middle). |
| `static Input.get_mouse_button_down(button: int) → bool` | ``True`` during the frame *button* was pressed. |
| `static Input.get_mouse_button_up(button: int) → bool` | ``True`` during the frame *button* was released. |
| `static Input.get_mouse_position() → Tuple[float, float]` | Current mouse position as ``(x, y)`` — functional alternative. |
| `static Input.get_mouse_scroll_delta() → Tuple[float, float]` | Scroll delta as ``(x, y)`` — functional alternative. |
| `static Input.get_axis(axis_name: str) → float` | Simple virtual axis query (no smoothing — equivalent to ``GetAxisRaw``). |
| `static Input.get_axis_raw(axis_name: str) → float` | Alias for ``get_axis`` — no smoothing is applied. |
| `static Input.get_input_string() → str` | Characters typed this frame — functional alternative. |
| `static Input.reset_input_axes() → None` | Reset all input state. |

<!-- USER CONTENT START --> static_methods

<!-- USER CONTENT END -->

## Example

```python
# TODO: Add example for Input
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## See Also

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
