"""
Light — Python InfComponent wrapper for the C++ Light component.

Exposes all Light properties as CppProperty descriptors so they appear
in the InfComponent serialized-field system and Inspector UI.

The underlying rendering is handled entirely by C++.

Example::

    from InfEngine.components.builtin import Light
    from InfEngine.lib import LightType, LightShadows

    class DayNightCycle(InfComponent):
        def start(self):
            self.sun = self.get_component(Light)

        def update(self, dt):
            self.sun.intensity = ...
"""

from __future__ import annotations

from InfEngine.components.builtin_component import BuiltinComponent, CppProperty
from InfEngine.components.serialized_field import FieldType


class Light(BuiltinComponent):
    """Python wrapper for the C++ Light component.

    Properties delegate to the C++ ``Light`` object via CppProperty.
    All changes are immediately reflected in the renderer.
    """

    _cpp_type_name = "Light"
    _component_category_ = "Rendering"

    # Scene icon: yellow diamond shown at light position (Unity-style)
    gizmo_icon_color = (1.0, 0.92, 0.016)

    # ---- Light type ----
    light_type = CppProperty(
        "light_type",
        FieldType.ENUM,
        default=None,
        enum_type="LightType",
        enum_labels=["Directional", "Point", "Spot", "Area"],
        tooltip="Type of light (Directional, Point, Spot, Area)",
    )

    # ---- Color & intensity ----
    color = CppProperty(
        "color",
        FieldType.VEC3,
        default=None,
        header="Appearance",
        tooltip="Light color (linear RGB)",
    )
    intensity = CppProperty(
        "intensity",
        FieldType.FLOAT,
        default=1.0,
        range=(0.0, 10.0),
        tooltip="Light intensity multiplier",
    )

    # ---- Range (Point / Spot) ----
    range = CppProperty(
        "range",
        FieldType.FLOAT,
        default=10.0,
        range=(0.1, 100.0),
        visible_when=lambda comp: int(comp.light_type) in (1, 2),
        tooltip="Light range (Point / Spot lights)",
    )

    # ---- Spot angles ----
    spot_angle = CppProperty(
        "spot_angle",
        FieldType.FLOAT,
        default=30.0,
        range=(1.0, 179.0),
        visible_when=lambda comp: int(comp.light_type) == 2,
        tooltip="Inner spot angle in degrees",
    )
    outer_spot_angle = CppProperty(
        "outer_spot_angle",
        FieldType.FLOAT,
        default=45.0,
        range=(1.0, 179.0),
        visible_when=lambda comp: int(comp.light_type) == 2,
        tooltip="Outer spot angle in degrees",
    )

    # ---- Shadows ----
    shadows = CppProperty(
        "shadows",
        FieldType.ENUM,
        default=None,
        enum_type="LightShadows",
        enum_labels=["No Shadows", "Hard", "Soft"],
        header="Shadows",
        tooltip="Shadow type (None, Hard, Soft)",
    )
    shadow_strength = CppProperty(
        "shadow_strength",
        FieldType.FLOAT,
        default=1.0,
        range=(0.0, 1.0),
        visible_when=lambda comp: int(comp.shadows) > 0,
        tooltip="Shadow strength (0-1)",
    )
    shadow_bias = CppProperty(
        "shadow_bias",
        FieldType.FLOAT,
        default=0.005,
        range=(0.0, 0.1),
        visible_when=lambda comp: int(comp.shadows) > 0,
        tooltip="Shadow depth bias",
    )

    # ------------------------------------------------------------------
    # Methods (delegate to C++ Light)
    # ------------------------------------------------------------------

    def get_light_view_matrix(self):
        """Get the light's view matrix for shadow mapping."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.get_light_view_matrix()
        return None

    def get_light_projection_matrix(
        self,
        shadow_extent: float = 20.0,
        near_plane: float = 0.1,
        far_plane: float = 100.0,
    ):
        """Get the light's projection matrix for shadow mapping."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.get_light_projection_matrix(shadow_extent, near_plane, far_plane)
        return None

    def serialize(self) -> str:
        """Serialize Light to JSON string (delegates to C++)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.serialize()
        return "{}"
