"""InfUIScreenComponent — base for 2D screen-space UI elements.

Provides a rectangular region (x, y, width, height) in canvas-space pixels.
The Inspector automatically hides the Transform section for GameObjects that
carry a screen-space UI component, since positioning is handled by the
canvas coordinate system rather than world-space Transform.

Hierarchy:
    InfComponent → InfUIComponent → InfUIScreenComponent
"""

from InfEngine.components import serialized_field
from .inf_ui_component import InfUIComponent


class InfUIScreenComponent(InfUIComponent):
    """2D screen-space UI element with a canvas-pixel rectangle.

    Attributes:
        x: Horizontal position in canvas pixels (from canvas left edge).
        y: Vertical position in canvas pixels (from canvas top edge).
        width: Width in canvas pixels.
        height: Height in canvas pixels.

    The ``_hide_transform_`` class flag tells the Inspector to skip the
    Transform header for GameObjects owning this component.
    """

    _hide_transform_: bool = True

    x: float = serialized_field(default=0.0, tooltip="X position in canvas pixels", group="布局 Layout")
    y: float = serialized_field(default=0.0, tooltip="Y position in canvas pixels", group="布局 Layout")
    width: float = serialized_field(default=160.0, tooltip="Width in canvas pixels", group="布局 Layout")
    height: float = serialized_field(default=40.0, tooltip="Height in canvas pixels", group="布局 Layout")

    def get_rect(self):
        """Return (x, y, w, h) in canvas-space."""
        return (self.x, self.y, self.width, self.height)
