"""
Base class for closable editor panels.
"""

from InfEngine.lib import InfGUIRenderable, InfGUIContext
from typing import Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from .window_manager import WindowManager


class ClosablePanel(InfGUIRenderable):
    """
    Base class for panels that can be closed via the window close button.
    """
    
    # Class-level registration info
    WINDOW_TYPE_ID: Optional[str] = None
    WINDOW_DISPLAY_NAME: Optional[str] = None
    
    def __init__(self, title: str, window_id: Optional[str] = None):
        super().__init__()
        self._title = title
        self._window_id = window_id or self.__class__.__name__
        self._is_open = True
        self._window_manager: Optional['WindowManager'] = None
    
    @property
    def window_id(self) -> str:
        return self._window_id
    
    @property
    def is_open(self) -> bool:
        return self._is_open
    
    def set_window_manager(self, window_manager: 'WindowManager'):
        """Set the window manager reference."""
        self._window_manager = window_manager

    def open(self):
        """Ensure this panel is visible."""
        self._is_open = True
    
    def _begin_closable_window(self, ctx: InfGUIContext, flags: int = 0) -> bool:
        """
        Begin a closable window. Returns True if window content should be rendered.
        Handles close button automatically.
        """
        safe_title = str(self._title).replace('\x00', '�').encode('utf-8', errors='replace').decode('utf-8', errors='replace')
        visible, self._is_open = ctx.begin_window_closable(safe_title, self._is_open, flags)
        
        # If window was closed, notify window manager
        if not self._is_open and self._window_manager:
            self._window_manager.set_window_open(self._window_id, False)
        
        return visible and self._is_open
