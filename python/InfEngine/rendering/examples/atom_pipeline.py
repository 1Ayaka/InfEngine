"""
Example: Atom-based forward pipeline using RenderGraph.

Demonstrates how to use RenderGraph builder + render atom concepts to
compose a clean render pipeline. The opaque/transparent passes are defined
via the RenderGraph builder and executed through the unified path.

Usage::

    from InfEngine.rendering.examples.atom_pipeline import AtomPipelineAsset
    engine.set_render_pipeline(AtomPipelineAsset())
"""

from InfEngine.rendergraph.graph import RenderGraph, Format
from InfEngine.rendering import RenderPipeline, RenderPipelineAsset


class AtomPipelineAsset(RenderPipelineAsset):
    """Asset that creates an AtomPipeline."""

    def create_pipeline(self):
        return AtomPipeline()


class AtomPipeline(RenderPipeline):
    """Forward renderer composed from RenderGraph passes.

    Functionally equivalent to DefaultForwardPipeline, demonstrating
    the atom-based composition pattern via RenderGraph builder.
    """

    def __init__(self):
        super().__init__()
        self._graph_desc = None

    def _build_graph(self):
        graph = RenderGraph("AtomPipeline")
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
        return graph.build()

    def render(self, context, cameras):
        for camera in cameras:
            context.setup_camera_properties(camera)
            culling = context.cull(camera)

            if self._graph_desc is None:
                self._graph_desc = self._build_graph()

            context.apply_graph(self._graph_desc)
            context.submit_culling(culling)
