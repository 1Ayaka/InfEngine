"""
EditorPanel — enhanced base class for editor panels.

Extends :class:`ClosablePanel` with automatic access to
:class:`EditorServices`, :class:`EditorEventBus`, and lifecycle hooks.

To create a custom panel, users write::

    from InfEngine.engine.ui import EditorPanel, editor_panel, EditorEvent

    @editor_panel("My Debug Panel")
    class MyDebugPanel(EditorPanel):
        def on_enable(self):
            # Called when the panel is created — subscribe to events here
            self.events.subscribe(EditorEvent.SELECTION_CHANGED, self._on_sel)

        def on_disable(self):
            # Called when the panel is closed — unsubscribe here
            self.events.unsubscribe(EditorEvent.SELECTION_CHANGED, self._on_sel)

        def on_render_content(self, ctx):
            ctx.text("Hello from my custom panel!")

            if ctx.button("Log"):
                from InfEngine.debug import Debug
                Debug.log("Custom panel button pressed!")

        def _on_sel(self, obj):
            # React to selection changes from any source
            pass
"""

from __future__ import annotations

from typing import Optional, TYPE_CHECKING

from .closable_panel import ClosablePanel

if TYPE_CHECKING:
    from InfEngine.lib import InfGUIContext
    from .editor_services import EditorServices
    from .event_bus import EditorEventBus


class EditorPanel(ClosablePanel):
    """Enhanced base class for editor panels.

    Provides:
    - ``self.services`` — access to :class:`EditorServices` (engine, undo, etc.)
    - ``self.events``   — access to :class:`EditorEventBus`
    - ``on_enable()``   — lifecycle hook when panel is created / reopened
    - ``on_disable()``  — lifecycle hook when panel is closed
    - ``on_render_content(ctx)`` — override this to render panel content
    """

    def __init__(self, title: str, window_id: Optional[str] = None):
        super().__init__(title, window_id)
        self._enable_called = False

    # ------------------------------------------------------------------
    # Service / event access
    # ------------------------------------------------------------------

    @property
    def services(self) -> EditorServices:
        """Access to all editor subsystems."""
        from .editor_services import EditorServices
        return EditorServices.instance()

    @property
    def events(self) -> EditorEventBus:
        """Access to the editor event bus."""
        from .event_bus import EditorEventBus
        return EditorEventBus.instance()

    # ------------------------------------------------------------------
    # Lifecycle hooks (override in subclasses)
    # ------------------------------------------------------------------

    def on_enable(self) -> None:
        """Called once when the panel is first rendered.

        Subscribe to events here.
        """
        pass

    def on_disable(self) -> None:
        """Called when the panel is closed.

        Unsubscribe from events here to avoid dangling references.
        """
        pass

    # ------------------------------------------------------------------
    # Content rendering (override in subclasses)
    # ------------------------------------------------------------------

    def on_render_content(self, ctx: InfGUIContext) -> None:
        """Render the panel's content.

        Override this method instead of ``on_render``.  The base
        implementation of ``on_render`` handles the closable window
        frame and calls this method when visible.
        """
        pass

    # ------------------------------------------------------------------
    # Default on_render  (ClosablePanel-aware)
    # ------------------------------------------------------------------

    def on_render(self, ctx) -> None:
        """Render the closable window frame and delegate to :meth:`on_render_content`.

        Subclasses that need full control can still override ``on_render``
        directly, but most panels should override ``on_render_content``.
        """
        if not self._is_open:
            return

        # Trigger on_enable on first render
        if not self._enable_called:
            self._enable_called = True
            self.on_enable()

        visible = self._begin_closable_window(ctx)
        if visible:
            self.on_render_content(ctx)
        ctx.end_window()

        # Detect close
        if not self._is_open:
            self.on_disable()
