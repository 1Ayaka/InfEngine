"""
Built-in Render Passes

Registry for pre-configured pass definitions that ship with the engine.
Currently empty -- no shader back-ends are implemented yet.

When a built-in pass is ready (shader + C++ support in place), add it
here following this pattern::

    class MyCustomPass(GeometryPass):
        name = "MyCustom"
        injection_point = "after_opaque"
        default_order = 100
        requires = {"depth"}
        creates = {"custom_target"}

        def inject(self, graph, bus):
            ...

    BUILTIN_PASSES["MyCustom"] = MyCustomPass
"""

from __future__ import annotations

from typing import Dict, Type

from InfEngine.renderstack.render_pass import RenderPass
from InfEngine.renderstack.bloom_effect import BloomEffect


# =====================================================================
# Registry -- populated when concrete passes are implemented
# =====================================================================

BUILTIN_PASSES: Dict[str, Type[RenderPass]] = {
    "Bloom": BloomEffect,
}
