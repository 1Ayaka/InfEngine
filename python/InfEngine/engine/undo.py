"""
Undo/Redo system for InfEngine editor operations.

Implements a command pattern with:
- Per-property undo with automatic merge for rapid edits (slider dragging)
- Save-point tracking for clean/dirty state synchronisation
- Integration with SceneFileManager for dirty flag management
- Play mode isolation (stack cleared on play/stop)

Usage::

    from InfEngine.engine.undo import UndoManager, SetPropertyCommand

    mgr = UndoManager.instance()
    mgr.execute(SetPropertyCommand(obj, "position", old, new, "Move Object"))
    mgr.undo()   # restores old value
    mgr.redo()   # re-applies new value
"""

from __future__ import annotations

import time
from abc import ABC, abstractmethod
from typing import Any, Optional, List, Callable


# ---------------------------------------------------------------------------
# Base command
# ---------------------------------------------------------------------------

class UndoCommand(ABC):
    """Base class for all undoable editor commands."""

    #: Whether this command supports redo after undo.  Commands that cannot
    #: fully recreate their effect (e.g. *CreateGameObject*) should set this
    #: to ``False`` so they are discarded from the redo stack on undo.
    supports_redo: bool = True

    #: Whether this command represents a scene modification that affects the
    #: dirty/save-point state.  Commands like selection changes set this to
    #: ``False`` so that selecting an object does not mark the scene dirty.
    marks_dirty: bool = True

    def __init__(self, description: str = ""):
        self.description: str = description
        self.timestamp: float = time.time()

    @abstractmethod
    def execute(self) -> None:
        """Perform the action.  Called by :meth:`UndoManager.execute`."""

    @abstractmethod
    def undo(self) -> None:
        """Reverse the action."""

    def redo(self) -> None:
        """Re-apply the action.  Defaults to :meth:`execute`."""
        self.execute()

    # -- Merging (for consecutive rapid edits on the same property) --

    def can_merge(self, other: UndoCommand) -> bool:
        """Return *True* if *other* can be folded into this command."""
        return False

    def merge(self, other: UndoCommand) -> None:
        """Absorb *other* into this command (called only when :meth:`can_merge` returned *True*)."""


# ---------------------------------------------------------------------------
# Concrete commands
# ---------------------------------------------------------------------------

class SetPropertyCommand(UndoCommand):
    """Set a property on a target object via ``setattr``.

    Works uniformly for C++ components (pybind11 properties) and Python
    ``InfComponent`` fields (``SerializedFieldDescriptor``).

    Consecutive rapid edits to the **same target + property** within
    :attr:`MERGE_WINDOW` seconds are merged into a single undo entry
    (e.g. dragging a slider).
    """

    MERGE_WINDOW: float = 0.3  # seconds

    def __init__(self, target: Any, prop_name: str,
                 old_value: Any, new_value: Any,
                 description: str = ""):
        super().__init__(description or f"Set {prop_name}")
        self._target = target
        self._prop_name = prop_name
        self._old_value = old_value
        self._new_value = new_value
        self._target_id: int = self._stable_id(target)

    # -- stable identity for merge comparisons --
    @staticmethod
    def _stable_id(target: Any) -> int:
        for attr in ("component_id", "id"):
            val = getattr(target, attr, None)
            if val is not None and val != 0:
                return int(val)
        return id(target)

    def execute(self) -> None:
        setattr(self._target, self._prop_name, self._new_value)

    def undo(self) -> None:
        setattr(self._target, self._prop_name, self._old_value)

    def redo(self) -> None:
        setattr(self._target, self._prop_name, self._new_value)

    def can_merge(self, other: UndoCommand) -> bool:
        if not isinstance(other, SetPropertyCommand):
            return False
        return (self._target_id == other._target_id
                and self._prop_name == other._prop_name
                and (other.timestamp - self.timestamp) <= self.MERGE_WINDOW)

    def merge(self, other: SetPropertyCommand) -> None:  # type: ignore[override]
        self._new_value = other._new_value
        self.timestamp = other.timestamp


class GenericComponentCommand(UndoCommand):
    """Undo/redo for a C++ component edited via the generic
    *serialize → edit → deserialize* path in the Inspector.
    """

    MERGE_WINDOW: float = 0.3

    def __init__(self, comp: Any, old_json: str, new_json: str,
                 description: str = ""):
        super().__init__(description or f"Edit {getattr(comp, 'type_name', 'Component')}")
        self._comp = comp
        self._old_json = old_json
        self._new_json = new_json
        self._comp_id: int = getattr(comp, "component_id", id(comp))

    def execute(self) -> None:
        self._comp.deserialize(self._new_json)

    def undo(self) -> None:
        self._comp.deserialize(self._old_json)

    def redo(self) -> None:
        self._comp.deserialize(self._new_json)

    def can_merge(self, other: UndoCommand) -> bool:
        if not isinstance(other, GenericComponentCommand):
            return False
        return (self._comp_id == other._comp_id
                and (other.timestamp - self.timestamp) <= self.MERGE_WINDOW)

    def merge(self, other: GenericComponentCommand) -> None:  # type: ignore[override]
        self._new_json = other._new_json
        self.timestamp = other.timestamp


class BuiltinPropertyCommand(UndoCommand):
    """Undo/redo for a C++ component property edited via direct setter.

    Unlike ``GenericComponentCommand`` (which goes through JSON
    serialize/deserialize), this command uses ``setattr(comp, attr, val)``
    which calls the pybind11 property setter → C++ ``SetXXX()`` →
    ``RebuildShape()`` / physics sync.  This is the preferred path for
    BuiltinComponent wrappers (colliders, rigidbody, etc.) because it
    guarantees immediate physics world updates at runtime.
    """

    MERGE_WINDOW: float = 0.3

    def __init__(self, comp: Any, cpp_attr: str, old_value: Any,
                 new_value: Any, description: str = ""):
        super().__init__(description or f"Set {cpp_attr}")
        self._comp = comp
        self._cpp_attr = cpp_attr
        self._old_value = old_value
        self._new_value = new_value
        self._comp_id: int = getattr(comp, "component_id", id(comp))

    def execute(self) -> None:
        setattr(self._comp, self._cpp_attr, self._new_value)

    def undo(self) -> None:
        setattr(self._comp, self._cpp_attr, self._old_value)

    def redo(self) -> None:
        setattr(self._comp, self._cpp_attr, self._new_value)

    def can_merge(self, other: UndoCommand) -> bool:
        if not isinstance(other, BuiltinPropertyCommand):
            return False
        return (self._comp_id == other._comp_id
                and self._cpp_attr == other._cpp_attr
                and (other.timestamp - self.timestamp) <= self.MERGE_WINDOW)

    def merge(self, other: BuiltinPropertyCommand) -> None:  # type: ignore[override]
        self._new_value = other._new_value
        self.timestamp = other.timestamp


class CreateGameObjectCommand(UndoCommand):
    """Record the creation of a GameObject.  Undo destroys it.

    Because the C++ scene API does not expose single-object recreation from
    JSON, redo is **not** supported — the command is discarded from the redo
    stack after undo.
    """
    supports_redo = False

    def __init__(self, object_id: int, description: str = "Create GameObject"):
        super().__init__(description)
        self._object_id = object_id

    def execute(self) -> None:
        pass  # already created before record()

    def undo(self) -> None:
        scene = _get_active_scene()
        if scene:
            obj = scene.find_by_id(self._object_id)
            if obj:
                scene.destroy_game_object(obj)


class ReparentCommand(UndoCommand):
    """Undo/redo reparenting of a GameObject."""

    def __init__(self, object_id: int,
                 old_parent_id: Optional[int],
                 new_parent_id: Optional[int],
                 description: str = "Reparent"):
        super().__init__(description)
        self._object_id = object_id
        self._old_parent_id = old_parent_id
        self._new_parent_id = new_parent_id

    def execute(self) -> None:
        self._apply_parent(self._new_parent_id)

    def undo(self) -> None:
        self._apply_parent(self._old_parent_id)

    def redo(self) -> None:
        self._apply_parent(self._new_parent_id)

    def _apply_parent(self, parent_id: Optional[int]) -> None:
        scene = _get_active_scene()
        if not scene:
            return
        obj = scene.find_by_id(self._object_id)
        if not obj:
            return
        if parent_id is not None:
            parent = scene.find_by_id(parent_id)
            obj.set_parent(parent)
        else:
            obj.set_parent(None)


class AddNativeComponentCommand(UndoCommand):
    """Record adding a C++ component.  Undo removes it; redo re-adds."""

    def __init__(self, object_id: int, type_name: str, comp_ref: Any = None,
                 description: str = ""):
        super().__init__(description or f"Add {type_name}")
        self._object_id = object_id
        self._type_name = type_name
        self._comp_ref = comp_ref

    def execute(self) -> None:
        pass  # already added before record()

    def undo(self) -> None:
        scene = _get_active_scene()
        if scene and self._comp_ref:
            obj = scene.find_by_id(self._object_id)
            if obj and hasattr(obj, "remove_component"):
                obj.remove_component(self._comp_ref)

    def redo(self) -> None:
        scene = _get_active_scene()
        if scene:
            obj = scene.find_by_id(self._object_id)
            if obj:
                result = obj.add_component(self._type_name)
                if result:
                    self._comp_ref = result


class AddPyComponentCommand(UndoCommand):
    """Record adding a Python component.  Undo removes it.

    Redo is not supported because recreating an arbitrary Python component
    instance with the exact same state is not trivial.
    """
    supports_redo = False

    def __init__(self, object_id: int, py_comp_ref: Any,
                 description: str = ""):
        super().__init__(
            description or f"Add {getattr(py_comp_ref, 'type_name', 'Script')}")
        self._object_id = object_id
        self._py_comp_ref = py_comp_ref

    def execute(self) -> None:
        pass  # already added before record()

    def undo(self) -> None:
        scene = _get_active_scene()
        if scene and self._py_comp_ref:
            obj = scene.find_by_id(self._object_id)
            if obj and hasattr(obj, "remove_py_component"):
                obj.remove_py_component(self._py_comp_ref)


class SelectionCommand(UndoCommand):
    """Record a selection change in the scene hierarchy.

    Undo restores the previous selection; redo re-applies the new one.
    Selection is **not** a scene modification — this command does not
    mark the scene dirty or affect the save-point.

    The *apply_fn* callback receives a single ``int`` (object ID, 0 for
    deselect) and is responsible for updating hierarchy, inspector, and
    selection outline.
    """

    MERGE_WINDOW: float = 0.0  # never merge selection changes
    marks_dirty: bool = False    # selection is NOT a scene modification

    def __init__(self, old_id: int, new_id: int,
                 apply_fn: Callable[[int], None],
                 description: str = ""):
        super().__init__(description or "Select")
        self._old_id = old_id
        self._new_id = new_id
        self._apply_fn = apply_fn

    def execute(self) -> None:
        self._apply_fn(self._new_id)

    def undo(self) -> None:
        self._apply_fn(self._old_id)

    def redo(self) -> None:
        self._apply_fn(self._new_id)


class CompoundCommand(UndoCommand):
    """Group multiple commands into a single undo/redo unit.

    TODO: Not yet used — reserved for future batch-operation support (e.g.
    multi-object property edits, bulk reparenting).
    """

    def __init__(self, commands: List[UndoCommand], description: str = ""):
        desc = description or (commands[0].description if commands else "Compound")
        super().__init__(desc)
        self._commands: List[UndoCommand] = list(commands)
        self.supports_redo = all(c.supports_redo for c in commands)

    def execute(self) -> None:
        for cmd in self._commands:
            cmd.execute()

    def undo(self) -> None:
        for cmd in reversed(self._commands):
            cmd.undo()

    def redo(self) -> None:
        for cmd in self._commands:
            cmd.redo()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _get_active_scene():
    """Return the active C++ Scene, or *None* on failure."""
    from InfEngine.lib import SceneManager
    return SceneManager.instance().get_active_scene()


# ---------------------------------------------------------------------------
# UndoManager singleton
# ---------------------------------------------------------------------------

class UndoManager:
    """Central undo/redo manager.

    Maintains an undo and a redo stack of :class:`UndoCommand` objects with
    automatic merge for rapid successive property edits, save-point tracking,
    and integration with :class:`SceneFileManager` for dirty-flag management.
    """

    MAX_STACK_DEPTH: int = 200

    _instance: Optional[UndoManager] = None

    def __init__(self) -> None:
        self._undo_stack: List[UndoCommand] = []
        self._redo_stack: List[UndoCommand] = []
        # ``None`` means the save state has been evicted from the history.
        self._save_point: Optional[int] = 0
        # Dirty baseline for states that intentionally have no usable history.
        self._base_scene_dirty: bool = False
        self._is_executing: bool = False
        self._enabled: bool = True
        self._on_state_changed: Optional[Callable[[], None]] = None
        UndoManager._instance = self

    @staticmethod
    def instance() -> Optional[UndoManager]:
        """Return the singleton, or *None* if not yet created."""
        return UndoManager._instance

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

    @property
    def is_executing(self) -> bool:
        """*True* while a command is being executed / undone / redone.

        Used by :class:`SerializedFieldDescriptor` to skip auto-recording
        when the undo system itself is driving the property change.
        """
        return self._is_executing

    @property
    def can_undo(self) -> bool:
        return len(self._undo_stack) > 0

    @property
    def can_redo(self) -> bool:
        return len(self._redo_stack) > 0

    @property
    def undo_description(self) -> str:
        return self._undo_stack[-1].description if self._undo_stack else ""

    @property
    def redo_description(self) -> str:
        return self._redo_stack[-1].description if self._redo_stack else ""

    @property
    def _dirty_depth(self) -> int:
        """Count of commands in the undo stack that mark the scene dirty."""
        return sum(1 for cmd in self._undo_stack if cmd.marks_dirty)

    @property
    def is_at_save_point(self) -> bool:
        if self._save_point is None:
            return False
        return self._dirty_depth == self._save_point

    @property
    def enabled(self) -> bool:
        return self._enabled

    @enabled.setter
    def enabled(self, value: bool) -> None:
        self._enabled = value

    # ------------------------------------------------------------------
    # Core operations
    # ------------------------------------------------------------------

    def execute(self, cmd: UndoCommand) -> None:
        """Execute *cmd*, push it onto the undo stack, and clear the redo
        stack.  Automatically merges with the stack-top command if possible.
        """
        if not self._enabled:
            # disabled → still execute, just don't record history
            cmd.execute()
            return

        self._is_executing = True
        cmd.execute()
        self._is_executing = False

        self._push(cmd)

    def record(self, cmd: UndoCommand) -> None:
        """Push an **already-executed** command onto the undo stack.

        Use this when the action was performed outside the command's
        :meth:`~UndoCommand.execute` (e.g. complex creation logic in the
        hierarchy panel).
        """
        if not self._enabled:
            return
        self._push(cmd)

    def undo(self) -> None:
        """Undo the most recent command."""
        if not self._undo_stack:
            return
        cmd = self._undo_stack.pop()
        self._is_executing = True
        cmd.undo()
        self._is_executing = False

        if cmd.supports_redo:
            self._redo_stack.append(cmd)

        self._sync_dirty()
        self._fire_state_changed()

    def redo(self) -> None:
        """Redo the most recently undone command."""
        if not self._redo_stack:
            return
        cmd = self._redo_stack.pop()
        self._is_executing = True
        cmd.redo()
        self._is_executing = False

        self._undo_stack.append(cmd)

        self._sync_dirty()
        self._fire_state_changed()

    def clear(self, scene_is_dirty: bool = False) -> None:
        """Clear both stacks and reset the dirty baseline."""
        self._undo_stack.clear()
        self._redo_stack.clear()
        self._save_point = 0
        self._base_scene_dirty = bool(scene_is_dirty)
        self._fire_state_changed()

    def mark_save_point(self) -> None:
        """Record the current undo depth as the *clean* state.

        Called by :class:`SceneFileManager` after a successful save.
        """
        self._save_point = self._dirty_depth
        self._base_scene_dirty = False

    def sync_dirty_state(self) -> None:
        """Re-apply dirty state to SceneFileManager from current history."""
        self._sync_dirty()

    def set_on_state_changed(self, cb: Optional[Callable[[], None]]) -> None:
        self._on_state_changed = cb

    # ------------------------------------------------------------------
    # Private
    # ------------------------------------------------------------------

    def _push(self, cmd: UndoCommand) -> None:
        """Push *cmd*, merge if possible, enforce depth limit, clear redo."""
        if self._undo_stack and self._undo_stack[-1].can_merge(cmd):
            self._undo_stack[-1].merge(cmd)
        else:
            self._undo_stack.append(cmd)
            # enforce depth limit
            if len(self._undo_stack) > self.MAX_STACK_DEPTH:
                overflow = len(self._undo_stack) - self.MAX_STACK_DEPTH
                dirty_dropped = sum(1 for c in self._undo_stack[:overflow] if c.marks_dirty)
                del self._undo_stack[:overflow]
                if self._save_point is not None:
                    self._save_point -= dirty_dropped
                    if self._save_point < 0:
                        self._save_point = None  # save state lost

        self._redo_stack.clear()
        self._sync_dirty()
        self._fire_state_changed()

    def _sync_dirty(self) -> None:
        """Update :class:`SceneFileManager` dirty flag based on save-point.

        Skipped in play mode — runtime property tweaks are transient and
        must not mark the scene file as dirty.
        """
        from InfEngine.engine.play_mode import PlayModeManager, PlayModeState
        pm = PlayModeManager.get_instance()
        if pm and pm.state != PlayModeState.EDIT:
            return
        from InfEngine.engine.scene_manager import SceneFileManager
        sfm = SceneFileManager.instance()
        if sfm is None:
            return
        if self._base_scene_dirty or not self.is_at_save_point:
            sfm.mark_dirty()
        else:
            sfm.clear_dirty()

    def _fire_state_changed(self) -> None:
        if self._on_state_changed:
            self._on_state_changed()
