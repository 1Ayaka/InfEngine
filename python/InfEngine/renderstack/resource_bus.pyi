"""Type stubs for InfEngine.renderstack.resource_bus."""

from __future__ import annotations

from typing import Dict, Optional, Set, TYPE_CHECKING

if TYPE_CHECKING:
    from InfEngine.rendergraph.graph import TextureHandle


class ResourceBus:
    """在 graph 构建过程中传递资源句柄的字典。"""

    def __init__(self, initial: Optional[Dict[str, TextureHandle]] = None) -> None: ...
    def get(self, name: str) -> Optional[TextureHandle]: ...
    def set(self, name: str, handle: TextureHandle) -> None: ...
    def has(self, name: str) -> bool: ...
    @property
    def available_resources(self) -> Set[str]: ...
    def snapshot(self) -> Dict[str, TextureHandle]: ...
