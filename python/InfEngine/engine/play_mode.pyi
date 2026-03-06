"""Type stubs for InfEngine.engine.play_mode."""

from __future__ import annotations

from enum import Enum, auto
from typing import Callable, List, Optional
from dataclasses import dataclass


class PlayModeState(Enum):
    """Play mode states."""
    EDIT = auto()
    PLAYING = auto()
    PAUSED = auto()


@dataclass
class PlayModeEvent:
    """Event data for play mode state changes."""
    old_state: PlayModeState
    new_state: PlayModeState
    timestamp: float


class PlayModeManager:
    """Manages the runtime/editor play mode (Unity-style scene isolation).

    Example::

        play_mode = PlayModeManager()
        play_mode.enter_play_mode()
        play_mode.tick(delta_time)
        play_mode.exit_play_mode()
    """

    _instance: Optional[PlayModeManager]

    def __init__(self) -> None: ...

    @classmethod
    def get_instance(cls) -> Optional[PlayModeManager]: ...

    def set_asset_database(self, asset_database: object) -> None:
        """Set AssetDatabase for GUID-based script resolution."""
        ...

    # Properties
    @property
    def state(self) -> PlayModeState: ...
    @property
    def is_playing(self) -> bool:
        """True if in play or paused mode."""
        ...
    @property
    def is_paused(self) -> bool: ...
    @property
    def is_edit_mode(self) -> bool: ...
    @property
    def delta_time(self) -> float:
        """Time since last frame in seconds."""
        ...
    @property
    def time_scale(self) -> float: ...
    @time_scale.setter
    def time_scale(self, value: float) -> None: ...
    @property
    def total_play_time(self) -> float:
        """Total time elapsed since entering play mode."""
        ...

    # State transitions
    def enter_play_mode(self) -> bool:
        """Enter play mode from edit mode. Saves scene state."""
        ...
    def exit_play_mode(self) -> bool:
        """Exit play mode; restores scene state."""
        ...
    def pause(self) -> bool:
        """Pause play mode."""
        ...
    def resume(self) -> bool:
        """Resume from pause."""
        ...
    def toggle_pause(self) -> None:
        """Toggle pause/resume."""
        ...
    def step_frame(self) -> None:
        """Advance a single frame while paused."""
        ...

    def tick(self, delta_time: float) -> None:
        """Called each frame to update play mode timing."""
        ...

    # Event listeners
    def add_state_change_listener(
        self, callback: Callable[[PlayModeEvent], None]
    ) -> None: ...
    def remove_state_change_listener(
        self, callback: Callable[[PlayModeEvent], None]
    ) -> None: ...
