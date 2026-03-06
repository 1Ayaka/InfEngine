"""Type stubs for InfEngine.rendering.render_pipeline."""

from __future__ import annotations

from typing import List

from InfEngine.lib._InfEngine import (
    Camera,
    RenderPipelineCallback,
    ScriptableRenderContext,
)

from InfEngine.rendergraph.graph import RenderGraph


class RenderPipelineAsset:
    """Factory for creating RenderPipeline instances.

    Override ``create_pipeline()`` to return your custom RenderPipeline.

    Example::

        class MyPipelineAsset(RenderPipelineAsset):
            def create_pipeline(self):
                return MyPipeline()

        engine.set_render_pipeline(MyPipelineAsset())
    """
    def create_pipeline(self) -> RenderPipeline: ...


class RenderPipeline(RenderPipelineCallback):
    """Base class for scriptable render pipelines.

    The minimal subclass only needs ``define_topology()``.
    Override ``render_camera()`` for per-camera custom logic.
    Override ``render()`` only for fully custom rendering.
    """

    name: str

    # Graph is built once from define_topology() and cached here
    _standalone_desc: object

    # Standalone entry point — base class handles graph build + camera loop
    def render(self, context: ScriptableRenderContext, cameras: List[Camera]) -> None: ...
    def should_render_camera(self, camera: Camera) -> bool: ...
    def render_camera(self, context: ScriptableRenderContext, camera: Camera, culling: object) -> None: ...
    def dispose(self) -> None: ...

    # RenderStack integration
    def define_topology(self, graph: RenderGraph) -> None: ...
