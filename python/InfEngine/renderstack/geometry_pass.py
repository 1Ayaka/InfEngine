"""
GeometryPass — Scene geometry drawing pass.

Used for passes that need to draw scene objects (e.g. Outline mask, Decal).
Subclasses configure ``queue_range`` and ``sort_mode``.

**Important**: The base ``inject()`` raises ``NotImplementedError``.
GeometryPass subclasses **must** override ``inject()`` because MRT slot
assignment depends on the specific shader layout and cannot be handled
generically.

Example::

    class OutlineMaskPass(GeometryPass):
        name = "OutlineMask"
        injection_point = "after_opaque"
        default_order = 50
        requires = {"depth"}
        creates = {"outline_mask"}
        queue_range = (0, 2500)

        def inject(self, graph, bus):
            if not self.enabled:
                return
            depth = bus.get("depth")
            mask = graph.create_texture("_OutlineMask", format=Format.R8_UNORM)
            p = graph.add_pass(self.name)
            p.read(depth)
            p.write_color(mask)
            p.set_clear(color=(0, 0, 0, 0))
            p.draw_renderers(queue_range=self.queue_range, sort_mode="none")
            bus.set("outline_mask", mask)
"""

from __future__ import annotations

from typing import Tuple, TYPE_CHECKING

from InfEngine.renderstack.render_pass import RenderPass

if TYPE_CHECKING:
    from InfEngine.rendergraph.graph import RenderGraph
    from InfEngine.renderstack.resource_bus import ResourceBus


class GeometryPass(RenderPass):
    """场景几何绘制 Pass。

    用于需要进行场景物体绘制的 Pass（如 Outline mask、Decal）。

    **重要**: 基类 ``inject()`` 抛出 ``NotImplementedError``。
    子类必须重写 ``inject()`` 并显式控制每个 ``write_color()`` 的槽位，
    因为 MRT 槽位分配依赖具体 shader 布局。
    """

    queue_range: Tuple[int, int] = (0, 5000)
    sort_mode: str = "none"

    def inject(self, graph: "RenderGraph", bus: "ResourceBus") -> None:
        """GeometryPass 子类必须重写此方法。

        基类不提供默认实现，因为 MRT 槽位分配依赖具体 shader 布局。

        Raises:
            NotImplementedError: Always. Subclasses must override.
        """
        raise NotImplementedError(
            f"{type(self).__name__} must override inject(). "
            f"GeometryPass base class does not provide a default "
            f"implementation because MRT slot assignment must be "
            f"explicit. See design doc Section 9 for examples."
        )
