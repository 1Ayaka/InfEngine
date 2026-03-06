"""UIText — a text label UI element (Figma-style properties).

Hierarchy:
    InfComponent → InfUIComponent → InfUIScreenComponent → UIText
"""

from InfEngine.components import serialized_field, add_component_menu
from InfEngine.components.serialized_field import FieldType
from .inf_ui_screen_component import InfUIScreenComponent
from .enums import TextAlignH, TextAlignV, TextOverflow


@add_component_menu("UI/Text")
class UIText(InfUIScreenComponent):
    """Figma-style text label rendered with ImGui draw primitives.

    Inherits x, y, width, height from InfUIScreenComponent.
    All fields carry ``group`` metadata so the generic inspector renderer
    displays them in collapsible sections automatically.
    """

    # ── 内容 Content ──
    text: str = serialized_field(
        default="New Text", tooltip="Display text",
        group="内容 Content", multiline=True,
    )

    # ── 排版 Typography ──
    font_size: float = serialized_field(
        default=24.0, tooltip="Font size in canvas pixels",
        group="排版 Typography", range=(4.0, 256.0), slider=False, drag_speed=0.5,
    )
    line_height: float = serialized_field(
        default=1.2, tooltip="Line height multiplier",
        group="排版 Typography", range=(0.5, 5.0), slider=False, drag_speed=0.01,
    )
    letter_spacing: float = serialized_field(
        default=0.0, tooltip="Extra letter spacing in px",
        group="排版 Typography", range=(-20.0, 100.0), slider=False, drag_speed=0.1,
    )

    # ── 对齐 Alignment ──
    text_align_h: TextAlignH = serialized_field(
        default=TextAlignH.Left, tooltip="Horizontal alignment",
        group="对齐 Alignment",
    )
    text_align_v: TextAlignV = serialized_field(
        default=TextAlignV.Top, tooltip="Vertical alignment",
        group="对齐 Alignment",
    )

    # ── 溢出 Overflow ──
    overflow: TextOverflow = serialized_field(
        default=TextOverflow.Visible, tooltip="Text overflow mode",
        group="溢出 Overflow",
    )

    # ── 填充 Fill ──
    color: list = serialized_field(
        default=[1.0, 1.0, 1.0, 1.0],
        field_type=FieldType.COLOR,
        tooltip="Text color (RGBA)",
        group="填充 Fill",
    )
