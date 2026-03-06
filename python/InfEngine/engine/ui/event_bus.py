"""
EditorEventBus — lightweight publish/subscribe event system for editor panels.

Replaces ad-hoc callback wiring between panels with a centralized event bus.
Panels subscribe to events they care about and emit events when state changes.

Usage::

    from InfEngine.engine.ui.event_bus import EditorEventBus, EditorEvent

    # Subscribe
    bus = EditorEventBus.instance()
    bus.subscribe(EditorEvent.SELECTION_CHANGED, self._on_selection)

    # Emit
    bus.emit(EditorEvent.SELECTION_CHANGED, game_object)

    # Unsubscribe (e.g. in on_disable)
    bus.unsubscribe(EditorEvent.SELECTION_CHANGED, self._on_selection)
"""

from __future__ import annotations

from collections import defaultdict
from enum import Enum
from typing import Any, Callable, Dict, List, Optional

from InfEngine.debug import Debug


class EditorEvent(Enum):
    """Well-known editor events.

    Each event documents the expected callback signature after the event name.
    Custom events can extend this enum or use string keys via
    :meth:`EditorEventBus.subscribe_custom`.
    """

    # Selection ---------------------------------------------------------------
    # callback(game_object: Optional[GameObject])
    SELECTION_CHANGED = "selection_changed"

    # callback(file_path: Optional[str])
    FILE_SELECTED = "file_selected"

    # Scene -------------------------------------------------------------------
    # callback(scene)
    SCENE_LOADED = "scene_loaded"

    # callback()
    SCENE_SAVED = "scene_saved"

    # Play mode ---------------------------------------------------------------
    # callback(old_state: PlayModeState, new_state: PlayModeState)
    PLAY_MODE_CHANGED = "play_mode_changed"

    # Assets ------------------------------------------------------------------
    # callback(file_path: str, event_type: str)  event_type: modified|deleted|moved
    ASSET_CHANGED = "asset_changed"

    # callback(file_path: str)
    SCRIPT_RELOADED = "script_reloaded"

    # Undo/Redo ---------------------------------------------------------------
    # callback()
    UNDO_STATE_CHANGED = "undo_state_changed"

    # UI focus ----------------------------------------------------------------
    # callback(panel_id: str)
    PANEL_FOCUSED = "panel_focused"


class EditorEventBus:
    """Singleton event bus for editor-wide communication.

    Thread-safety note: all subscriptions and emissions are expected to
    happen on the main thread (the ImGui render loop).
    """

    _instance: Optional[EditorEventBus] = None

    def __init__(self) -> None:
        # EditorEvent -> [callbacks]
        self._subscribers: Dict[Any, List[Callable]] = defaultdict(list)
        EditorEventBus._instance = self

    # ------------------------------------------------------------------
    # Singleton
    # ------------------------------------------------------------------

    @classmethod
    def instance(cls) -> EditorEventBus:
        """Return the singleton, creating one if necessary."""
        if cls._instance is None:
            cls()
        return cls._instance  # type: ignore[return-value]

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def subscribe(self, event: Any, callback: Callable) -> None:
        """Subscribe *callback* to *event*.

        *event* can be an :class:`EditorEvent` member or any hashable key
        for custom events.
        """
        subs = self._subscribers[event]
        if callback not in subs:
            subs.append(callback)

    def unsubscribe(self, event: Any, callback: Callable) -> None:
        """Remove *callback* from *event* subscribers.

        Silently ignores if *callback* was not subscribed.
        """
        subs = self._subscribers.get(event)
        if subs and callback in subs:
            subs.remove(callback)

    def emit(self, event: Any, *args: Any, **kwargs: Any) -> None:
        """Emit *event*, calling all subscribers with the given arguments.

        Exceptions in individual callbacks are caught and logged so that
        one broken subscriber cannot break other listeners.
        """
        for cb in list(self._subscribers.get(event, [])):
            try:
                cb(*args, **kwargs)
            except Exception as e:
                Debug.log_error(f"[EventBus] Error in handler for {event}: {e}")

    def clear(self) -> None:
        """Remove all subscriptions (used during teardown)."""
        self._subscribers.clear()

    def subscriber_count(self, event: Any) -> int:
        """Return the number of subscribers for *event*."""
        return len(self._subscribers.get(event, []))
