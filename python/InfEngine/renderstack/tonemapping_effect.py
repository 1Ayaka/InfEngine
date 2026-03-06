"""
ToneMappingEffect — HDR-to-LDR tone mapping post-processing effect.

Maps linear HDR scene color into displayable LDR range with optional
gamma correction.  Should be the **last** effect in the post-process
stack (runs at ``after_post_process``) so that bloom and other HDR
effects are applied first.

Supported operators:
    - None (clamp to [0,1])
    - Reinhard
    - ACES Filmic (default — matches Unity/Unreal look)
"""

from __future__ import annotations

from enum import IntEnum
from typing import List, TYPE_CHECKING

from InfEngine.renderstack.fullscreen_effect import FullScreenEffect
from InfEngine.components.serialized_field import serialized_field

if TYPE_CHECKING:
    from InfEngine.rendergraph.graph import RenderGraph
    from InfEngine.renderstack.resource_bus import ResourceBus


class ToneMappingMode(IntEnum):
    """Tone mapping operator."""
    NONE     = 0
    Reinhard = 1
    ACES     = 2


class ToneMappingEffect(FullScreenEffect):
    """HDR-to-LDR tone mapping post-processing effect.

    Should be the last effect in the post-process chain so that bloom
    and other HDR effects can operate on the full dynamic range.
    """

    name = "Tone Mapping"
    injection_point = "after_post_process"
    default_order = 900          # high order → runs last within its injection point
    menu_path = "Post-processing/Tone Mapping"

    # ---- Serialized parameters (shown in Inspector) ----
    mode: ToneMappingMode = serialized_field(
        default=ToneMappingMode.ACES,
        tooltip="Tone mapping operator (ACES is recommended for realistic look)",
    )
    exposure: float = serialized_field(
        default=1.0,
        range=(0.01, 10.0),
        drag_speed=0.05,
        tooltip="Pre-tonemap exposure multiplier",
    )
    gamma: float = serialized_field(
        default=2.2,
        range=(1.0, 3.0),
        drag_speed=0.01,
        tooltip="Gamma correction exponent (2.2 = standard sRGB)",
    )

    # ------------------------------------------------------------------
    # FullScreenEffect interface
    # ------------------------------------------------------------------

    def get_shader_list(self) -> List[str]:
        return [
            "fullscreen_triangle",
            "tonemapping",
        ]

    def setup_passes(self, graph: "RenderGraph", bus: "ResourceBus") -> None:
        """Inject the tonemapping pass into the render graph.

        Pipeline::

            scene_color → [blit] → scene_copy → [tonemap] → scene_color
        """
        color_handle = bus.get("color")
        if color_handle is None:
            return

        from InfEngine.rendergraph.graph import Format

        # Helper: reuse existing texture or create new one.
        def _tex(name, **kwargs):
            existing = graph.get_texture(name)
            if existing is not None:
                return existing
            return graph.create_texture(name, **kwargs)

        # Copy scene color to avoid read+write hazard on the backbuffer.
        scene_copy = _tex("_tonemap_scene_copy", format=Format.RGBA16_SFLOAT)

        with graph.add_pass("ToneMap_Blit") as p:
            p.read(color_handle)
            p.write_color(scene_copy)
            p.fullscreen_quad("fullscreen_blit")

        with graph.add_pass("ToneMap_Apply") as p:
            p.read(scene_copy)
            p.write_color(color_handle)
            p.fullscreen_quad(
                "tonemapping",
                mode=float(int(self.mode)),
                exposure=self.exposure,
                gamma=self.gamma,
            )
