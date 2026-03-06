"""UICanvas — root container for screen-space UI elements.

A UICanvas is attached to a GameObject in the Hierarchy.
All UI elements (UIText, etc.) are children of the Canvas's GameObject.
The Canvas itself only stores configuration; rendering is handled by
the UI Editor panel & Game View overlay via ImGui draw primitives.

The canvas defines a *design* reference resolution (default 1920×1080).
At runtime the Game View scales from design resolution to actual viewport
size so that all positions, sizes and font sizes adapt proportionally.

Hierarchy:
    InfComponent → InfUIComponent → UICanvas
"""

from InfEngine.components import (
    disallow_multiple,
    add_component_menu,
    serialized_field,
    int_field,
)
from .inf_ui_component import InfUIComponent
from .enums import RenderMode


@disallow_multiple
@add_component_menu("UI/Canvas")
class UICanvas(InfUIComponent):
    """Screen-space UI canvas.

    reference_width / reference_height are the *design* reference resolution.
    They are user-editable and default to 1920×1080.  At runtime the Game
    View overlay scales all element positions, sizes and font sizes
    proportionally from this reference to the actual viewport.

    Attributes:
        render_mode: ScreenOverlay or CameraOverlay.
        sort_order: Rendering order (lower draws first).
        target_camera_id: Camera GameObject ID (CameraOverlay mode only).
    """

    render_mode: RenderMode = serialized_field(default=RenderMode.ScreenOverlay)
    sort_order: int = int_field(0, range=(-1000, 1000), tooltip="Render order (lower = earlier)")
    target_camera_id: int = int_field(0, tooltip="Camera ID for CameraOverlay mode")

    # Design reference resolution (serialized, user-editable)
    reference_width: int = int_field(1920, range=(1, 8192), tooltip="Design reference width", slider=False)
    reference_height: int = int_field(1080, range=(1, 8192), tooltip="Design reference height", slider=False)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def iter_ui_elements(self):
        """Yield all screen-space UI components on child GameObjects (depth-first)."""
        go = self.game_object
        if go is None:
            return
        yield from self._walk_children(go)

    def _walk_children(self, parent):
        from .inf_ui_screen_component import InfUIScreenComponent
        for child in parent.get_children():
            for comp in child.get_py_components():
                if isinstance(comp, InfUIScreenComponent):
                    yield comp
            yield from self._walk_children(child)
