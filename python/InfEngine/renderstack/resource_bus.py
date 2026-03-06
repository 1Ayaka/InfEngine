"""
ResourceBus — Transient resource handle dictionary for graph construction.

Created by ``RenderStack.build_graph()``, passed into
``Pipeline.define_topology()`` and each ``Pass.inject()``.
Carries TextureHandle references between pipeline stages and user passes.

Lifecycle::

    RenderStack creates bus
        → Pipeline initialises base resources (color, depth)
        → injection point callbacks pass bus to each Pass
        → RenderStack reads final output from bus

Resource name conventions::

    "color"       — scene color
    "depth"       — scene depth
    custom names  — introduced by Pass ``creates`` declarations
"""

from __future__ import annotations

from typing import Dict, Optional, Set, TYPE_CHECKING

if TYPE_CHECKING:
    from InfEngine.rendergraph.graph import TextureHandle


class ResourceBus:
    """在 graph 构建过程中传递资源句柄的字典。

    Pass 通过声明 ``requires`` / ``modifies`` / ``creates`` 与
    ResourceBus 交互。未声明的资源自动透传给后续 Pass。

    .. note::
        ``modifies`` 隐含 ``requires``。一个 Pass 声明
        ``modifies={"color"}`` 意味着它同时 **读取** 并 **写入** color。
    """

    def __init__(
        self, initial: Optional[Dict[str, "TextureHandle"]] = None
    ) -> None:
        self._resources: Dict[str, "TextureHandle"] = dict(initial or {})

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def get(self, name: str) -> Optional["TextureHandle"]:
        """获取资源句柄。未找到返回 ``None``。"""
        return self._resources.get(name)

    def set(self, name: str, handle: "TextureHandle") -> None:
        """设置 / 更新资源句柄。"""
        self._resources[name] = handle

    def has(self, name: str) -> bool:
        """检查资源是否存在。"""
        return name in self._resources

    @property
    def available_resources(self) -> Set[str]:
        """当前可用的所有资源名。"""
        return set(self._resources.keys())

    def snapshot(self) -> Dict[str, "TextureHandle"]:
        """返回当前资源的浅拷贝快照（用于调试）。"""
        return dict(self._resources)

    # ------------------------------------------------------------------
    # Dunder
    # ------------------------------------------------------------------

    def __repr__(self) -> str:
        keys = ", ".join(sorted(self._resources.keys()))
        return f"<ResourceBus [{keys}]>"
