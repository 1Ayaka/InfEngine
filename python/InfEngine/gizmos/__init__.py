"""
InfEngine Gizmos System — Unity-style immediate-mode gizmo drawing.

Usage::

    from InfEngine.gizmos import Gizmos

    class MyComponent(InfComponent):
        def on_draw_gizmos(self):
            Gizmos.color = (0, 1, 0)
            Gizmos.draw_wire_sphere(self.transform.position, 2.0)

        def on_draw_gizmos_selected(self):
            Gizmos.color = (1, 1, 0)
            Gizmos.draw_wire_cube(self.transform.position, (1, 1, 1))
"""

from .gizmos import Gizmos
from .collector import GizmosCollector

__all__ = [
    "Gizmos",
    "GizmosCollector",
]
