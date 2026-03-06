"""UI system enumerations."""

from enum import IntEnum


class RenderMode(IntEnum):
    """How a UICanvas renders its content."""
    ScreenOverlay = 0     # Rendered on top of everything (screen-space)
    CameraOverlay = 1     # Rendered on top of a specific camera's output


class TextAlignH(IntEnum):
    """Horizontal text alignment (Figma-style)."""
    Left = 0
    Center = 1
    Right = 2


class TextAlignV(IntEnum):
    """Vertical text alignment (Figma-style)."""
    Top = 0
    Center = 1
    Bottom = 2


class TextOverflow(IntEnum):
    """How text overflows its bounding box."""
    Visible = 0       # Draw beyond box
    Clip = 1          # Clip at box edge (visual only)
    Truncate = 2      # Add '…' when text is too long
