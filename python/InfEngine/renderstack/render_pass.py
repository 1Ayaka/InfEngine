"""
RenderPass — Base class for all mountable render passes.

A RenderPass represents a single rendering step that can be mounted to
a RenderStack at a named injection point. Subclasses declare their
resource requirements via class-level sets and implement ``inject()``
to add passes to the RenderGraph.

Subclass hierarchy::

    RenderPass          (abstract base)
    └── GeometryPass    (scene geometry drawing)

Resource declaration rules:
    - ``requires``: read-only — resource passes through unchanged
    - ``modifies``: read + write — modified handle replaces bus entry
    - ``creates``:  new resource — added to bus for subsequent passes
    - ``modifies`` implicitly includes ``requires`` (no need to declare both)
    - Undeclared resources auto-pass-through via ResourceBus
"""

from __future__ import annotations

from typing import ClassVar, List, Set, TYPE_CHECKING

if TYPE_CHECKING:
    from InfEngine.rendergraph.graph import RenderGraph
    from InfEngine.renderstack.resource_bus import ResourceBus


class RenderPass:
    """可挂载到 RenderStack 的渲染步骤基类。

    子类必须定义:
        - ``name``: 唯一名称（用于标识和序列化）
        - ``injection_point``: 要挂载到的注入点名称
        - ``default_order``: 同注入点内的默认排序值

    子类通过类属性声明资源需求:
        - ``requires``: ``Set[str]`` — 只读的资源（如 ``"depth"``）
        - ``modifies``: ``Set[str]`` — 读+写的资源（如 ``"color"``）
        - ``creates``:  ``Set[str]`` — 创建的新资源（如 ``"ao_texture"``）
    """

    # ---- 子类必须定义 ----
    name: str = ""
    injection_point: str = ""
    default_order: int = 0

    # ---- 资源声明 ----
    requires: ClassVar[Set[str]] = set()
    modifies: ClassVar[Set[str]] = set()
    creates: ClassVar[Set[str]] = set()

    # ---- 运行时 ----
    enabled: bool = True

    def __init__(self, enabled: bool = True) -> None:
        if not self.name:
            raise ValueError(
                f"{type(self).__name__} must define 'name'"
            )
        if not self.injection_point:
            raise ValueError(
                f"{type(self).__name__} must define 'injection_point'"
            )
        self.enabled = enabled

    # ------------------------------------------------------------------
    # Core interface
    # ------------------------------------------------------------------

    def inject(self, graph: "RenderGraph", bus: "ResourceBus") -> None:
        """向 RenderGraph 注入此 Pass 的渲染步骤。

        子类实现此方法:
        1. 从 bus 中获取需要的资源句柄
        2. 向 graph 添加渲染 pass
        3. 将修改/创建的资源写回 bus

        Args:
            graph: 当前构建中的 RenderGraph。
            bus: 资源总线，获取输入、写回输出。
        """
        raise NotImplementedError

    def validate(self, available_resources: Set[str]) -> List[str]:
        """验证此 Pass 的资源需求是否满足。

        ``modifies`` 被视为 ``requires`` 的超集——即
        ``effective_requires = requires ∪ modifies``。

        Args:
            available_resources: 当前注入点处 ResourceBus 上可用的资源名。

        Returns:
            错误信息列表。空列表表示验证通过。
        """
        errors: List[str] = []
        for r in self.requires | self.modifies:
            if r not in available_resources:
                errors.append(
                    f"Pass '{self.name}' requires resource '{r}' "
                    f"but it is not available at injection point "
                    f"'{self.injection_point}'"
                )
        return errors

    # ------------------------------------------------------------------
    # Dunder
    # ------------------------------------------------------------------

    def __repr__(self) -> str:
        return (
            f"<{type(self).__name__} name='{self.name}' "
            f"point='{self.injection_point}' "
            f"enabled={self.enabled}>"
        )
