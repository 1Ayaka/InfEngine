"""
InjectionPoint — Named injection slot in a RenderPipeline topology.

Pipeline authors declare injection points inline via
``graph.injection_point()``.  RenderStack uses them to determine where
user-mounted Passes are injected into the render graph.

Each injection point has:
- A unique ``name`` (e.g. "after_opaque")
- A ``resource_state`` describing the **minimum guaranteed** resources
  available from the ResourceBus at that point in the topology

Example::

    graph.injection_point(
        "after_opaque",
        display_name="After Opaque",
        resources={"color", "depth"},
    )
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Set


@dataclass
class InjectionPoint:
    """Pipeline 中的一个命名注入位置。

    由 Pipeline 作者通过 ``graph.injection_point()`` 声明。

    Attributes:
        name: 唯一标识符（如 ``"after_opaque"``）。
        display_name: Editor 显示名（如 ``"After Opaque"``）。
            默认由 *name* 自动生成。
        description: 用途描述文本。
        resource_state: 此注入点处 **最低保证** 可用的资源名集合。
            运行时 bus 中的实际资源可能是此集合的超集——因为前序 Pass
            的 ``creates`` 声明可能已向 bus 中添加了额外资源。
            RenderStack 验证阶段检查 ``Pass.requires ⊆ bus.keys()``，
            而非 ``Pass.requires ⊆ resource_state``。
        removable: 是否可在 Editor 中移除。
    """

    name: str
    display_name: str = ""
    description: str = ""
    resource_state: Set[str] = field(
        default_factory=lambda: {"color", "depth"}
    )
    removable: bool = True

    def __post_init__(self) -> None:
        if not self.display_name:
            self.display_name = self.name.replace("_", " ").title()
