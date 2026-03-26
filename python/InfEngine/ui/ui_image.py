"""UIImage — a rectangular image UI element.

Hierarchy:
    InfComponent → InfUIComponent → InfUIScreenComponent → UIImage
"""

from InfEngine.components import serialized_field, add_component_menu
from InfEngine.components.serialized_field import FieldType
from .inf_ui_screen_component import InfUIScreenComponent


@add_component_menu("UI/Image")
class UIImage(InfUIScreenComponent):
    """Screen-space image element rendered from a texture asset.

    Inherits x, y, width, height, opacity, corner_radius, rotation,
    mirror_x, mirror_y from InfUIScreenComponent.
    """

    # ── Fill ──
    texture_path: str = serialized_field(
        default="", tooltip="Path to texture asset (drag from Project panel)",
        group="Fill",
    )
    color: list = serialized_field(
        default=[1.0, 1.0, 1.0, 1.0],
        field_type=FieldType.COLOR,
        hdr=True,
        tooltip="Tint color (RGBA)",
        group="Fill",
    )
