"""InfUIComponent — abstract base for all UI components.

All UI-related components (screen-space, world-space, canvas, etc.) should
inherit from this class instead of InfComponent directly.

Hierarchy:
    InfComponent
        └─ InfUIComponent
             ├─ InfUIScreenComponent   (2D screen-space rect: x, y, w, h)
             └─ InfUIWorldComponent    (3D world-space UI — future)
"""

from InfEngine.components import InfComponent


class InfUIComponent(InfComponent):
    """Base class for every UI component in InfEngine.

    Provides:
    - ``_component_category_ = "UI"`` so that all UI components are grouped
      together in the *Add Component* menu.
    """

    _component_category_ = "UI"
