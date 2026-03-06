"""Type stubs for InfEngine.renderstack.geometry_pass."""

from __future__ import annotations

from typing import Tuple, TYPE_CHECKING

from InfEngine.renderstack.render_pass import RenderPass

if TYPE_CHECKING:
    from InfEngine.rendergraph.graph import RenderGraph
    from InfEngine.renderstack.resource_bus import ResourceBus


class GeometryPass(RenderPass):
    """场景几何绘制 Pass。"""

    queue_range: Tuple[int, int]
    sort_mode: str

    def inject(self, graph: RenderGraph, bus: ResourceBus) -> None: ...
