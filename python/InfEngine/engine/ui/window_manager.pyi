"""WindowManager — manages dockable editor windows.

Handles window registration, opening/closing, and ImGui layout persistence.

Example::

    wm = WindowManager(engine)
    wm.register_window_type("my_panel", "My Panel", factory=MyPanel)
    wm.open_window("my_panel")
"""

from __future__ import annotations

from typing import Callable, Dict, Optional

from InfEngine.lib import InfGUIRenderable


class WindowInfo:
    """Metadata for a registered window type."""

    type_id: str
    factory: Callable

    def __init__(
        self,
        type_id: str,
        display_name: str,
        factory: Callable,
        menu_path: Optional[str] = None,
    ) -> None: ...

    @property
    def display_name(self) -> str: ...


class WindowManager:
    """Manages dockable editor windows and their lifecycle."""

    @classmethod
    def instance(cls) -> Optional[WindowManager]:
        """Return the singleton, or ``None``."""
        ...

    def __init__(self, engine: object) -> None: ...

    def register_window_type(
        self,
        type_id: str,
        display_name: str,
        factory: Optional[Callable] = None,
        menu_path: Optional[str] = None,
    ) -> None:
        """Register a window type that can be opened from the Window menu.

        Args:
            type_id: Unique identifier.
            display_name: Name shown in the UI.
            factory: Zero-arg callable returning an ``InfGUIRenderable``.
            menu_path: Optional ``"Window/…"`` menu path.
        """
        ...

    def open_window(self, type_id: str, instance_id: Optional[str] = None) -> Optional[InfGUIRenderable]:
        """Open a window of *type_id* (creating it if needed).

        Returns:
            The window instance, or ``None`` on failure.
        """
        ...

    def close_window(self, window_id: str) -> None: ...

    def is_window_open(self, window_id: str) -> bool: ...

    def set_window_open(self, window_id: str, is_open: bool) -> None: ...

    def get_registered_types(self) -> Dict[str, WindowInfo]: ...

    def get_open_windows(self) -> Dict[str, bool]: ...

    def register_existing_window(
        self,
        window_id: str,
        instance: InfGUIRenderable,
        type_id: Optional[str] = None,
    ) -> None:
        """Register an already-created window instance."""
        ...

    def set_imgui_ini_path(self, path: str) -> None:
        """Set the path for ImGui layout persistence."""
        ...

    def reset_layout(self) -> None:
        """Reset the ImGui docking layout to defaults."""
        ...

    def process_pending_actions(self) -> None:
        """Apply queued open/close/layout mutations before the next GUI frame."""
        ...
