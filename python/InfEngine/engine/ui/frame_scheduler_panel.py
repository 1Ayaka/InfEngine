"""Per-frame scheduler renderable (no window, no UI)."""

from typing import TYPE_CHECKING
from InfEngine.lib import InfGUIRenderable, InfGUIContext

if TYPE_CHECKING:
    from InfEngine.engine import Engine


class FrameSchedulerPanel(InfGUIRenderable):
    """Runs engine-wide per-frame tasks exactly once per frame."""

    def __init__(self, engine: 'Engine' = None):
        super().__init__()
        self._engine = engine

    def set_engine(self, engine: 'Engine'):
        self._engine = engine

    def on_render(self, ctx: InfGUIContext):
        if self._engine:
            self._engine.tick_play_mode()
