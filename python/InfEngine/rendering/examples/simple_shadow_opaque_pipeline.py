"""
Example: Simple Shadow + Opaque Pipeline

A minimal render pipeline demonstrating the RenderStack programmable
rendering system. It renders only **opaque** objects (render queue 0–10)
with **directional shadow mapping**.

Pipeline topology::

    ShadowCasterPass  →  OpaquePass  →  output

Features:
    - Shadow map: 4096×4096 depth-only pass for directional light
    - Opaque pass: renders queue 0–10 with shadow map bound
    - No skybox, no transparent, no post-processing — deliberately minimal

Usage::

    from InfEngine.rendering.examples.simple_shadow_opaque_pipeline import (
        SimpleShadowOpaquePipeline,
    )
    from InfEngine.renderstack import RenderStackPipeline, RenderStack

    # Option 1: Use directly as a standalone pipeline
    engine.set_render_pipeline(SimpleShadowOpaquePipeline())

    # Option 2: Use via RenderStack (supports injection points)
    stack = game_object.add_component(RenderStack)
    stack.set_pipeline("Simple Shadow Opaque")
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from InfEngine.rendering.render_pipeline import RenderPipeline

if TYPE_CHECKING:
    from InfEngine.rendergraph.graph import RenderGraph


class SimpleShadowOpaquePipeline(RenderPipeline):
    """最简阴影 + 不透明管线。

    仅渲染 render queue 0–10 的不透明物体，附带方向光阴影。
    用于演示 RenderStack 可编程渲染管线系统的最小工作流。

    Topology::

        ShadowCasterPass (depth-only, 4096×4096)
            ↓ shadow_map
        OpaquePass (queue 0–10, front-to-back)
            → color output

    Injection points::

        after_opaque  — 不透明渲染完成后（可在此注入自定义效果）
    """

    name: str = "Simple Shadow Opaque"

    def define_topology(self, graph: "RenderGraph") -> None:
        """定义最小阴影 + 不透明渲染拓扑。"""
        from InfEngine.rendergraph.graph import Format

        shadow_resolution = 4096

        # ---- 创建资源 ----
        graph.create_texture("color", camera_target=True)
        graph.create_texture("depth", format=Format.D32_SFLOAT)
        graph.create_texture(
            "shadow_map",
            format=Format.D32_SFLOAT,
            size=(shadow_resolution, shadow_resolution),
        )

        # Pass 0: Shadow caster pass (depth-only, custom resolution)
        # Renders all shadow-casting objects in queue 0–10 into depth buffer
        with graph.add_pass("ShadowCasterPass") as p:
            p.write_depth("shadow_map")
            p.set_clear(depth=1.0)
            p.draw_shadow_casters(
                queue_range=(0, 10),
                light_index=0,
                shadow_type="hard",
            )

        # Pass 1: Opaque objects only (queue 0–10, front-to-back for early-z)
        # Shadow map is bound as input so the lit shader can sample it
        with graph.add_pass("OpaquePass") as p:
            p.write_color("color")
            p.write_depth("depth")
            p.set_clear(color=(0.1, 0.1, 0.1, 1.0), depth=1.0)
            p.set_input("shadowMap", "shadow_map")
            p.draw_renderers(queue_range=(0, 10), sort_mode="front_to_back")

        # Injection point: after_opaque
        # User passes can hook here to add custom effects (e.g. outline, decal)
        graph.injection_point("after_opaque", resources={"color", "depth"})

        # Final output
        graph.set_output("color")
