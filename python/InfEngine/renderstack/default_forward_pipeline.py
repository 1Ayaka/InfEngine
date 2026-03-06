"""
DefaultForwardPipeline — Standard 3-pass forward rendering pipeline.

This is the default pipeline used when no custom pipeline is selected.
It defines a standard forward rendering topology:

    OpaquePass → after_opaque → SkyboxPass → after_sky
    → TransparentPass → after_transparent

ScreenUI passes and post-process injection points are auto-generated
when the pipeline explicitly calls ``graph.screen_ui_section()``.

All injection points are exposed for user passes to hook into.

Usage::

    # Automatic — RenderStack uses this when pipeline_class_name is empty
    stack = game_object.add_component(RenderStack)
    # stack.pipeline is DefaultForwardPipeline by default

    # Manual — can also be selected explicitly
    stack.set_pipeline("Default Forward")
"""

from __future__ import annotations

from enum import IntEnum
from typing import List, TYPE_CHECKING

from InfEngine.rendering.render_pipeline import RenderPipeline
from InfEngine.components.serialized_field import serialized_field

if TYPE_CHECKING:
    from InfEngine.rendergraph.graph import RenderGraph


class MSAASamples(IntEnum):
    """Anti-aliasing sample count."""
    OFF = 1
    X2 = 2
    X4 = 4
    X8 = 8


class DefaultForwardPipeline(RenderPipeline):
    """标准前向渲染管线。

    定义 3 个注入点：

    =============================  ==================================
    注入点                          时机
    =============================  ==================================
    ``after_opaque``               不透明物体渲染完成后、天空盒之前
    ``after_sky``                  天空盒渲染完成后、透明物体之前
    ``after_transparent``          透明物体渲染完成后
    =============================  ==================================

    ``before_post_process`` / ``after_post_process`` 注入点及
    ScreenUI Camera / Overlay 渲染 pass 由
    ``graph.screen_ui_section()`` 显式插入。
    """

    name: str = "Default Forward"

    # ------------------------------------------------------------------
    # Exposed parameters (shown in RenderStack inspector)
    # ------------------------------------------------------------------
    shadow_resolution: int = serialized_field(
        default=4096,
        range=(256, 8192),
        tooltip="Shadow map resolution (width & height)",
        header="Shadows",
    )

    msaa_samples: MSAASamples = serialized_field(
        default=MSAASamples.X4,
        tooltip="Anti-aliasing sample count (OFF=1x, X2=2x, X4=4x, X8=8x)",
        header="Anti-Aliasing",
    )

    enable_screen_ui: bool = serialized_field(
        default=True,
        tooltip="Enable screen-space UI rendering (Canvas Overlay / Camera)",
        header="Screen UI",
    )

    # ------------------------------------------------------------------
    # RenderPipeline interface
    # ------------------------------------------------------------------

    def define_topology(self, graph: "RenderGraph") -> None:
        """定义前向渲染拓扑骨架。

        Topology::

            ShadowCasterPass → OpaquePass → after_opaque → SkyboxPass → after_sky
            → TransparentPass → after_transparent
        """
        from InfEngine.rendergraph.graph import Format

        # ---- MSAA configuration (from exposed parameter) ----
        graph.set_msaa_samples(int(self.msaa_samples))

        # ---- Shadow map configuration (from exposed parameters) ----
        shadow_res = self.shadow_resolution

        # ---- 创建资源 ----
        graph.create_texture("color", camera_target=True)
        graph.create_texture("depth", format=Format.D32_SFLOAT)
        graph.create_texture(
            "shadow_map",
            format=Format.D32_SFLOAT,
            size=(shadow_res, shadow_res),
        )

        # Pass 0: Shadow caster pass (depth-only, custom resolution)
        with graph.add_pass("ShadowCasterPass") as p:
            p.write_depth("shadow_map")
            p.set_clear(depth=1.0)
            p.draw_shadow_casters(
                queue_range=(0, 2999),
                light_index=0,
                shadow_type="hard",
            )

        # Pass 1: Opaque objects (front-to-back for early-z)
        with graph.add_pass("OpaquePass") as p:
            p.write_color("color")
            p.write_depth("depth")
            p.set_clear(color=(0.1, 0.1, 0.1, 1.0), depth=1.0)
            p.set_input("shadowMap", "shadow_map")
            p.draw_renderers(queue_range=(0, 2500), sort_mode="front_to_back")

        graph.injection_point("after_opaque", resources={"color", "depth"})

        # Pass 2: Skybox (renders after opaque, depth-tested)
        with graph.add_pass("SkyboxPass") as p:
            p.read("depth")
            p.write_color("color")
            p.draw_skybox()

        graph.injection_point("after_sky", resources={"color", "depth"})

        # Pass 3: Transparent objects (back-to-front for blending)
        with graph.add_pass("TransparentPass") as p:
            p.read("depth")
            p.write_color("color")
            p.set_input("shadowMap", "shadow_map")
            p.draw_renderers(
                queue_range=(2501, 5000),
                sort_mode="back_to_front",
            )

        graph.injection_point("after_transparent", resources={"color", "depth"})

        # ---- ScreenUI + post-process injection points ----
        if self.enable_screen_ui:
            graph.screen_ui_section()

        graph.set_output("color")
