"""Type stubs for InfEngine.renderstack.fullscreen_pass."""

from __future__ import annotations

from typing import TYPE_CHECKING

from InfEngine.renderstack.render_pass import RenderPass

if TYPE_CHECKING:
    from InfEngine.rendergraph.graph import RenderGraph, RenderPassBuilder
    from InfEngine.renderstack.resource_bus import ResourceBus


class FullscreenPass(RenderPass):
    """全屏 quad 后处理 Pass。"""

    injection_point: str
    shader_id: str

    def inject(self, graph: RenderGraph, bus: ResourceBus) -> None: ...
    def on_configure(self, builder: RenderPassBuilder, bus: ResourceBus) -> None: ...
