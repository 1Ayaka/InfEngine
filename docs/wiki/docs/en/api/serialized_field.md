# serialized_field

<div class="class-info">
function in <b>InfEngine.components</b>
</div>

```python
serialized_field(default: Any = ..., field_type: Optional[FieldType] = ..., range: Optional[Tuple[float, float]] = ..., tooltip: str = ..., readonly: bool = ..., header: str = ..., space: float = ..., group: str = ..., info_text: str = ..., multiline: bool = ..., slider: bool = ..., drag_speed: Optional[float] = ..., required_component: Optional[str] = ...) → Any
```

## Description

Mark a field as serialized and inspector-visible.

Args:
    default: Default value for the field.
    field_type: Explicit field type (auto-detected if not provided).
    range: ``(min, max)`` tuple for numeric sliders / bounded drag.
    tooltip: Hover text shown in inspector.
    readonly: If ``True``, field is read-only in inspector.
    header: Group header text shown above this field.
    space: Vertical spacing before this field in inspector.
    group: Collapsible group name.
    info_text: Non-editable description line (dimmed) below the field.
    multiline: Use multiline text input for STRING fields.
    slider: Widget style when range is set (True = slider, False = drag).
    drag_speed: Override default drag speed for numeric fields.
    required_component: For GAME_OBJECT fields only. If set, only
        GameObjects with a C++ component of this type name are accepted.

Example::

    class MyComponent(InfComponent):
        speed: float = serialized_field(default=5.0, range=(0, 100))

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## Parameters

| Name | Type | Description |
|------|------|------|
| default | `Any` |  (default: `...`) |
| field_type | `Optional[FieldType]` |  (default: `...`) |
| range | `Optional[Tuple[float, float]]` |  (default: `...`) |
| tooltip | `str` |  (default: `...`) |
| readonly | `bool` |  (default: `...`) |
| header | `str` |  (default: `...`) |
| space | `float` |  (default: `...`) |
| group | `str` |  (default: `...`) |
| info_text | `str` |  (default: `...`) |
| multiline | `bool` |  (default: `...`) |
| slider | `bool` |  (default: `...`) |
| drag_speed | `Optional[float]` |  (default: `...`) |
| required_component | `Optional[str]` |  (default: `...`) |

## Example

```python
# TODO: Add example for serialized_field
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->
