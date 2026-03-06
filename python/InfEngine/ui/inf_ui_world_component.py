"""InfUIWorldComponent — base for 3D world-space UI elements (future).

World-space UI components are positioned via their GameObject's Transform
and rendered in 3D space (e.g. health bars above characters).

Hierarchy:
    InfComponent → InfUIComponent → InfUIWorldComponent
"""

from .inf_ui_component import InfUIComponent


class InfUIWorldComponent(InfUIComponent):
    """3D world-space UI element (placeholder for future implementation).

    Unlike InfUIScreenComponent, world-space elements use the standard
    Transform for positioning, so ``_hide_transform_`` is *not* set.

    TODO: Implement world-space UI rendering pipeline, interaction hit-testing,
    and editor authoring workflow in a future version.
    """

    pass
