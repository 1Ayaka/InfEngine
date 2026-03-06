"""
InfEngine RenderGraph Module

Provides the Python-driven RenderGraph topology definition API.
Python has "definition authority" — it defines the pass topology,
resource connections, and per-pass render actions.
C++ has "compilation authority" — it performs DAG compilation,
barrier insertion, and transient resource allocation.

Architecture:
    Python defines WHAT passes exist and HOW they connect (topology).
    C++ handles WHERE resources live and WHEN barriers are inserted.

Usage::

    from InfEngine.rendergraph import RenderGraph, Format

    graph = RenderGraph("ForwardPipeline")

        graph.create_texture("color", camera_target=True)
    graph.create_texture("depth", format=Format.D32_SFLOAT)

    with graph.add_pass("OpaquePass") as p:
        p.write_color("color")
        p.write_depth("depth")
        p.set_clear(color=(0.1, 0.1, 0.1, 1.0), depth=1.0)
        p.draw_renderers(queue_range=(0, 2500), sort_mode="front_to_back")

    with graph.add_pass("SkyboxPass") as p:
        p.read("depth")
        p.write_color("color")
        p.draw_skybox()

    with graph.add_pass("TransparentPass") as p:
        p.read("depth")
        p.write_color("color")
        p.draw_renderers(queue_range=(2501, 5000), sort_mode="back_to_front")

    graph.set_output("color")

    # Apply to engine's scene render graph
    scene_graph = engine.get_scene_render_graph()
    scene_graph.apply_python_graph(graph.build())
"""

from .graph import RenderGraph, RenderPassBuilder, TextureHandle, Format
from InfEngine.renderstack.default_forward_pipeline import DefaultForwardPipeline

__all__ = [
    "RenderGraph",
    "RenderPassBuilder",
    "TextureHandle",
    "Format",
    "DefaultForwardPipeline",
]
