"""Type stubs for InfEngine.renderstack.render_pass."""

from __future__ import annotations

from typing import ClassVar, List, Set, TYPE_CHECKING

if TYPE_CHECKING:
    from InfEngine.rendergraph.graph import RenderGraph
    from InfEngine.renderstack.resource_bus import ResourceBus


class RenderPass:
    """可挂载到 RenderStack 的渲染步骤基类。"""

    name: str
    injection_point: str
    default_order: int
    requires: ClassVar[Set[str]]
    modifies: ClassVar[Set[str]]
    creates: ClassVar[Set[str]]
    enabled: bool

    def __init__(self, enabled: bool = True) -> None: ...
    def inject(self, graph: RenderGraph, bus: ResourceBus) -> None: ...
    def validate(self, available_resources: Set[str]) -> List[str]: ...
