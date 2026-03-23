"""Global engine-status indicator.

Any part of the engine can call::

    EngineStatus.set("保存场景...", 0.5)              # persistent until cleared
    EngineStatus.flash("保存完成", 1.0, 1.5)        # auto-clear after 1.5s
    EngineStatus.clear()                              # immediate clear

The StatusBarPanel reads these values every frame to render a progress bar
and label in the right portion of the bar.

For fast synchronous operations (save, play-start) that complete within one
frame, use ``flash()`` so the message stays visible for a minimum duration.
"""

from __future__ import annotations

import time


class EngineStatus:
    """Singleton-style engine activity indicator (pure class-level state)."""

    _text: str = ""
    _progress: float = -1.0   # <0 = indeterminate, 0..1 = determinate
    _expire_at: float = 0.0   # monotonic timestamp; 0 = never expire

    @classmethod
    def set(cls, text: str, progress: float = -1.0) -> None:
        """Set a persistent status (stays until ``clear()`` is called)."""
        cls._text = text
        cls._progress = progress
        cls._expire_at = 0.0

    @classmethod
    def flash(cls, text: str, progress: float = -1.0,
              duration: float = 1.5) -> None:
        """Set a status that auto-clears after *duration* seconds."""
        cls._text = text
        cls._progress = progress
        cls._expire_at = time.monotonic() + duration

    @classmethod
    def clear(cls) -> None:
        """Clear the status immediately (engine is idle)."""
        cls._text = ""
        cls._progress = -1.0
        cls._expire_at = 0.0

    @classmethod
    def get(cls) -> tuple[str, float]:
        """Return ``(text, progress)``, auto-clearing expired flash messages."""
        if cls._expire_at > 0.0 and time.monotonic() >= cls._expire_at:
            cls._text = ""
            cls._progress = -1.0
            cls._expire_at = 0.0
        return cls._text, cls._progress

    @classmethod
    def is_active(cls) -> bool:
        if cls._expire_at > 0.0 and time.monotonic() >= cls._expire_at:
            cls._text = ""
            cls._progress = -1.0
            cls._expire_at = 0.0
        return bool(cls._text)
