"""
PlayMode - Runtime/Editor mode manager for InfEngine.

Manages the play mode state machine:
- Edit Mode: Normal editor state, scene changes are persistent
- Play Mode: Runtime simulation, scene changes are temporary
- Pause Mode: Runtime paused, can step frame by frame

Handles:
- Scene state save/restore for play mode isolation (Unity-style)
- Delta time management
- Python component recreation after scene restore
"""

import time
import json
import os
from enum import Enum, auto
from typing import Optional, List, Dict, Any, Callable, TYPE_CHECKING
from dataclasses import dataclass
from InfEngine.debug import Debug, LogType
from InfEngine.engine.project_context import resolve_script_path

if TYPE_CHECKING:
    from InfEngine.lib import SceneManager, Scene, GameObject
    from InfEngine.components.component import InfComponent


class PlayModeState(Enum):
    """Play mode states."""
    EDIT = auto()      # Normal editor mode
    PLAYING = auto()   # Runtime playing
    PAUSED = auto()    # Runtime paused


@dataclass
class PlayModeEvent:
    """Event data for play mode state changes."""
    old_state: PlayModeState
    new_state: PlayModeState
    timestamp: float


def _get_scene_manager():
    """Get the SceneManager singleton from C++ bindings."""
    from InfEngine.lib import SceneManager
    return SceneManager.instance()


class PlayModeManager:
    """
    Manages the runtime/editor play mode.
    
    Implements Unity-style scene isolation:
    - On Play: Serialize entire scene state (C++ objects + Python components)
    - During Play: All changes are runtime-only
    - On Stop: Deserialize to restore original scene state
    
    Handles:
    - State transitions (Edit ↔ Play ↔ Pause)
    - Scene state save/restore via C++ serialization
    - Python component recreation after restore
    - Timing for UI display
    - (Lifecycle is driven by C++)
    
    Usage:
        play_mode = PlayModeManager()
        
        # Start play mode
        play_mode.enter_play_mode()
        
        # In game loop
        play_mode.tick(delta_time)
        
        # Stop and restore
        play_mode.exit_play_mode()
    """
    
    _instance: Optional['PlayModeManager'] = None
    
    def __init__(self):
        self._state = PlayModeState.EDIT
        
        # Timing
        self._last_frame_time: float = 0.0
        self._delta_time: float = 0.0
        self._time_scale: float = 1.0
        self._total_play_time: float = 0.0
        
        # Scene state backup (JSON string from C++ Scene::Serialize)
        self._scene_backup: Optional[str] = None
        # Original scene file path (to restore correct scene on Stop)
        self._scene_path_backup: Optional[str] = None
        self._scene_dirty_backup: bool = False
        
        # Event listeners
        self._state_change_listeners: List[Callable[[PlayModeEvent], None]] = []
        
        # Store singleton reference
        PlayModeManager._instance = self

        # Asset database for GUID-based script lookup
        self._asset_database = None
    
    @classmethod
    def get_instance(cls) -> Optional['PlayModeManager']:
        """Get the singleton instance if it exists."""
        return cls._instance
    
    def _get_scene_manager(self):
        """Get the SceneManager singleton."""
        return _get_scene_manager()

    def set_asset_database(self, asset_database):
        """Set AssetDatabase for GUID-based script resolution."""
        self._asset_database = asset_database
    
    # ========================================================================
    # Properties
    # ========================================================================
    
    @property
    def state(self) -> PlayModeState:
        """Current play mode state."""
        return self._state
    
    @property
    def is_playing(self) -> bool:
        """True if in play or paused mode."""
        return self._state in (PlayModeState.PLAYING, PlayModeState.PAUSED)
    
    @property
    def is_paused(self) -> bool:
        """True if currently paused."""
        return self._state == PlayModeState.PAUSED
    
    @property
    def is_edit_mode(self) -> bool:
        """True if in edit mode."""
        return self._state == PlayModeState.EDIT
    
    @property
    def delta_time(self) -> float:
        """Time since last frame in seconds."""
        return self._delta_time
    
    @property
    def time_scale(self) -> float:
        """Time scale factor (1.0 = normal speed)."""
        return self._time_scale
    
    @time_scale.setter
    def time_scale(self, value: float):
        """Set time scale (clamped to >= 0)."""
        self._time_scale = max(0.0, value)
    
    @property
    def total_play_time(self) -> float:
        """Total time elapsed since entering play mode."""
        return self._total_play_time
    
    # ========================================================================
    # State Transitions
    # ========================================================================
    
    def enter_play_mode(self) -> bool:
        """
        Enter play mode from edit mode.
        Saves scene state and initializes components.
        
        Returns:
            True if successfully entered play mode
        """
        if self._state != PlayModeState.EDIT:
            Debug.log_warning("Cannot enter play mode: not in edit mode")
            return False
        
        Debug.log_internal("▶ Entering Play Mode...")
        
        # Step 1: Serialize entire scene state (C++ handles all components)
        self._save_scene_state()

        # Step 1b: Clear undo stack (play-mode changes are temporary)
        from InfEngine.engine.undo import UndoManager
        _undo = UndoManager.instance()
        if _undo:
            _undo.clear()

        # Step 2: Initialize timing
        self._last_frame_time = time.time()
        self._total_play_time = 0.0
        self._delta_time = 0.0
        
        # Step 3: Clear BuiltinComponent wrapper cache (fresh start)
        from InfEngine.components.builtin_component import BuiltinComponent
        BuiltinComponent._clear_cache()

        # Step 4: Rebuild the active scene from the serialized editor snapshot.
        # This creates a clean runtime scene with fresh C++ Components,
        # fresh PyComponentProxy instances, and fresh Python InfComponent
        # instances. Lifecycle is then driven entirely by C++.
        if not self._rebuild_active_scene(self._scene_backup, for_play=True):
            Debug.log_error("Failed to rebuild runtime scene for Play Mode")
            self._state = PlayModeState.EDIT
            return False

        # Step 5: Transition state BEFORE entering scene play mode
        # so that listeners (e.g. Console "Clear on Play") fire BEFORE
        # C++ lifecycle calls start() on components.
        old_state = self._state
        self._state = PlayModeState.PLAYING
        self._notify_state_change(old_state, self._state)

        # Step 6: Enter scene play mode (C++ drives lifecycle)
        scene_manager = self._get_scene_manager()
        if scene_manager:
            scene_manager.play()

        Debug.log_internal("✓ Play Mode started (C++ lifecycle update path)")
        return True
    
    def exit_play_mode(self) -> bool:
        """
        Exit play mode and return to edit mode.
        Restores scene state to before play mode.
        
        Returns:
            True if successfully exited play mode
        """
        if self._state == PlayModeState.EDIT:
            Debug.log_warning("Cannot exit play mode: already in edit mode")
            return False
        
        Debug.log_internal("■ Exiting Play Mode...")
        
        # Step 1: Exit scene play mode
        scene_manager = self._get_scene_manager()
        if scene_manager:
            scene_manager.stop()

        from InfEngine.components.builtin_component import BuiltinComponent
        BuiltinComponent._clear_cache()

        # Step 2: Restore scene from backup as a fresh edit-mode scene.
        if not self._rebuild_active_scene(self._scene_backup, for_play=False, restore_scene_path=True):
            Debug.log_error("Failed to restore scene after exiting Play Mode")
            return False

        # Step 3: Clear undo stack (old commands reference stale objects)
        from InfEngine.engine.undo import UndoManager
        _undo = UndoManager.instance()
        if _undo:
            _undo.clear(scene_is_dirty=self._scene_dirty_backup)

        # Step 4: Transition state
        old_state = self._state
        self._state = PlayModeState.EDIT

        if _undo:
            _undo.sync_dirty_state()
        else:
            from InfEngine.engine.scene_manager import SceneFileManager
            sfm = SceneFileManager.instance()
            if sfm:
                if self._scene_dirty_backup:
                    sfm.mark_dirty()
                else:
                    sfm.clear_dirty()
        
        # Notify listeners
        self._notify_state_change(old_state, self._state)
        
        Debug.log_internal("✓ Returned to Edit Mode (scene restored)")
        return True
    
    def pause(self) -> bool:
        """
        Pause play mode.
        
        Returns:
            True if successfully paused
        """
        if self._state != PlayModeState.PLAYING:
            Debug.log_warning("Cannot pause: not currently playing")
            return False
        
        scene_manager = self._get_scene_manager()
        if scene_manager:
            scene_manager.pause()

        old_state = self._state
        self._state = PlayModeState.PAUSED
        
        Debug.log_internal("⏸ Play Mode Paused")
        self._notify_state_change(old_state, self._state)
        return True
    
    def resume(self) -> bool:
        """
        Resume from pause.
        
        Returns:
            True if successfully resumed
        """
        if self._state != PlayModeState.PAUSED:
            Debug.log_warning("Cannot resume: not currently paused")
            return False
        
        # Reset timing to avoid large delta_time after unpause
        self._last_frame_time = time.time()
        
        scene_manager = self._get_scene_manager()
        if scene_manager:
            scene_manager.play()

        old_state = self._state
        self._state = PlayModeState.PLAYING
        
        Debug.log_internal("▶ Play Mode Resumed")
        self._notify_state_change(old_state, self._state)
        return True
    
    def toggle_pause(self) -> bool:
        """Toggle between playing and paused states."""
        if self._state == PlayModeState.PLAYING:
            return self.pause()
        elif self._state == PlayModeState.PAUSED:
            return self.resume()
        return False
    
    def step_frame(self):
        """
        Execute a single frame while paused.
        Useful for debugging frame-by-frame.
        """
        if self._state != PlayModeState.PAUSED:
            Debug.log_warning("Step only works when paused")
            return
        
        scene_manager = self._get_scene_manager()
        if scene_manager:
            dt = self._delta_time if self._delta_time > 0 else (1.0 / 60.0)
            scene_manager.step(dt)
            Debug.log_internal(f"[Step] Stepped one frame (dt={dt:.4f}s)")
    
    # ========================================================================
    # Game Loop Integration
    # ========================================================================
    
    def tick(self, external_delta_time: float = None):
        """
        Called every frame by the engine.
        Updates timing and processes deferred scene loads.
        
        Args:
            external_delta_time: Optional externally provided delta time.
                                If None, calculates from wall clock.
        """
        if self._state == PlayModeState.EDIT:
            return

        # --- Process deferred scene loads (must run outside C++ iteration) ---
        from InfEngine.scene import SceneManager as _SceneMgr
        _SceneMgr.process_pending_load()
        
        if self._state == PlayModeState.PAUSED:
            # Don't update timing when paused
            return
        
        # Calculate delta time
        current_time = time.time()
        if external_delta_time is not None:
            self._delta_time = external_delta_time * self._time_scale
        else:
            self._delta_time = (current_time - self._last_frame_time) * self._time_scale
        
        self._last_frame_time = current_time
        self._total_play_time += self._delta_time
        
        # Clamp delta time to avoid spiral of death
        self._delta_time = min(self._delta_time, 0.1)  # Max 100ms
        
        # NOTE: Lifecycle update is driven by C++ only.

    def _rebuild_active_scene(
        self,
        snapshot: Optional[str],
        *,
        for_play: bool,
        restore_scene_path: bool = False,
    ) -> bool:
        """Deserialize *snapshot* into the active scene and recreate Python components.

        This is the core of the unified component mode: play/edit transitions no
        longer try to reset lifecycle flags on existing objects. Instead, the
        active scene is rebuilt from serialized data, producing a fresh native
        component graph and fresh Python component instances.
        """
        if not snapshot:
            Debug.log_warning("Cannot rebuild scene: empty snapshot")
            return False

        scene_manager = self._get_scene_manager()
        if not scene_manager:
            Debug.log_warning("Cannot rebuild scene: no SceneManager")
            return False

        scene = scene_manager.get_active_scene()
        if not scene:
            Debug.log_warning("Cannot rebuild scene: no active scene")
            return False

        from InfEngine.components.component import InfComponent
        from InfEngine.components.builtin_component import BuiltinComponent

        InfComponent._clear_all_instances()
        BuiltinComponent._clear_cache()

        if not scene.deserialize(snapshot):
            return False

        # When entering play mode, mark the scene as playing BEFORE restoring
        # Python components so newly attached PyComponentProxy instances use the
        # runtime lifecycle path instead of edit-mode lifecycle.
        if for_play and hasattr(scene, "set_playing"):
            scene.set_playing(True)

        self._restore_pending_py_components()

        if restore_scene_path:
            self._restore_scene_file_path()

        return True

    # ========================================================================
    # Python component helpers (serialization / reload)
    # ========================================================================

    def _serialize_py_component(self, component: 'InfComponent') -> Dict[str, Any]:
        """Serialize Python component fields and metadata.

        Uses the component's ``_serialize_value`` so that ref wrappers
        (GameObjectRef, MaterialRef) are converted to JSON-safe dicts.
        """
        from InfEngine.components.serialized_field import get_serialized_fields

        fields = get_serialized_fields(component.__class__)
        data = {}
        for name, meta in fields.items():
            raw = getattr(component, name, meta.default)
            data[name] = component._serialize_value(raw)

        script_guid = getattr(component, "_script_guid", None)

        return {
            "type_name": getattr(component, "type_name", component.__class__.__name__),
            "script_guid": script_guid,
            "enabled": getattr(component, "enabled", True),
            "fields": data,
        }

    def _apply_py_component_state(self, component: 'InfComponent', state: Dict[str, Any]):
        """Apply serialized field values to a Python component instance.

        Uses ``_deserialize_value`` so that JSON dicts produced by
        ``_serialize_py_component`` are correctly reconstructed into
        GameObjectRef / MaterialRef / enum values.
        """
        if not state:
            return
        component.enabled = bool(state.get("enabled", True))

        fields = state.get("fields", {})
        
        # Get the new class's serialized fields - only restore fields that still exist
        from InfEngine.components.serialized_field import get_serialized_fields
        new_serialized_fields = get_serialized_fields(component.__class__)
        
        for name, value in fields.items():
            # Only restore if the field still exists in the new class definition
            if name not in new_serialized_fields:
                continue
            meta = new_serialized_fields[name]
            value = component._deserialize_value(value, meta.field_type)
            setattr(component, name, value)

        if state.get("script_guid"):
            component._script_guid = state.get("script_guid")

    def reload_components_from_script(self, file_path: str):
        """
        Reload all Python components that originate from the given script file.

        This is intended for Edit mode live-updates when a script changes.
        """
        if self._state != PlayModeState.EDIT:
            return
        if not self._asset_database:
            return

        script_path_abs = resolve_script_path(file_path)
        if not script_path_abs or not os.path.exists(script_path_abs):
            return
        scene_manager = self._get_scene_manager()
        if not scene_manager:
            return
        scene = scene_manager.get_active_scene()
        if not scene:
            return

        from InfEngine.components import load_and_create_component

        reloaded_count = 0
        target_guid = None
        if self._asset_database:
            target_guid = self._asset_database.get_guid_from_path(script_path_abs)
        if not target_guid:
            return

        for obj in scene.get_all_objects():
            if not hasattr(obj, "get_py_components"):
                continue
            py_components = list(obj.get_py_components())

            for comp in py_components:
                comp_guid = getattr(comp, "_script_guid", None)
                if comp_guid != target_guid:
                    continue

                state = self._serialize_py_component(comp)

                if hasattr(obj, "remove_py_component"):
                    obj.remove_py_component(comp)

                new_comp = load_and_create_component(script_path_abs, asset_database=self._asset_database)
                self._apply_py_component_state(new_comp, state)
                new_comp._script_guid = target_guid
                obj.add_py_component(new_comp)
                reloaded_count += 1

        if reloaded_count > 0:
            Debug.log_internal(f"Reloaded {reloaded_count} component(s) from {os.path.basename(script_path_abs)}")

    # ========================================================================
    # Scene Snapshot (for runtime isolation)
    # ========================================================================

    # ========================================================================
    # Python Component Restoration (after C++ scene deserialize)
    # ========================================================================

    def _restore_pending_py_components(self):
        """
        Restore Python components after scene has been deserialized.
        
        C++ Scene::Deserialize() stores pending Python component info,
        which we retrieve and use to recreate the actual Python instances.
        """
        scene_manager = self._get_scene_manager()
        if not scene_manager:
            return
        scene = scene_manager.get_active_scene()
        if not scene:
            return

        # Check if there are pending Python components to restore
        if not scene.has_pending_py_components():
            Debug.log_internal("No pending Python components to restore")
            return

        # Get pending components (this also clears the list in C++)
        pending_list = scene.take_pending_py_components()
        Debug.log_internal(f"Restoring {len(pending_list)} Python components...")

        restored_count = 0
        restored_components = []  # Track for on_after_deserialize callback

        for pending in pending_list:
            obj = scene.find_by_id(pending.game_object_id)
            if not obj:
                Debug.log_warning(f"Cannot restore component: GameObject {pending.game_object_id} not found")
                continue

            # Create new Python component instance
            component = self._create_py_component(
                pending.script_guid,
                pending.type_name,
                pending.fields_json,
                pending.enabled
            )

            if component:
                obj.add_py_component(component)
                restored_count += 1
                restored_components.append(component)

                # Verify game_object is correctly set
                if component.game_object is None:
                    Debug.log_warning(f"Restored component {component.type_name} has no game_object!")

        # Call on_after_deserialize on all restored components after all are attached
        for comp in restored_components:
            comp._call_on_after_deserialize()

        Debug.log_internal(f"Restored {restored_count} Python components")


    def _create_py_component(self, script_guid: str, type_name: str, 
                              fields_json: str, enabled: bool) -> Optional['InfComponent']:
        """
        Create a Python component instance from serialized data.
        
        Args:
            script_guid: GUID of the script asset
            type_name: Python class name
            fields_json: JSON string of field values
            enabled: Whether the component should be enabled
            
        Returns:
            New component instance, or None if creation failed
        """
        component = None

        if not self._asset_database:
            return None
        
        script_path_abs = None
        if script_guid and self._asset_database:
            resolved = self._asset_database.get_path_from_guid(script_guid)
            if resolved:
                script_path_abs = resolve_script_path(resolved)
        if script_path_abs and os.path.exists(script_path_abs):
            from InfEngine.components import load_and_create_component
            component = load_and_create_component(script_path_abs, asset_database=self._asset_database)
            if script_guid:
                component._script_guid = script_guid
        
        # If no script path or failed, try to find by type name in registry
        if component is None and type_name:
            from InfEngine.components.registry import get_type
            cls = get_type(type_name)
            if cls is not None:
                component = cls()
                if script_guid:
                    component._script_guid = script_guid
            else:
                Debug.log_warning(f"Cannot create component '{type_name}': type not found in registry")
                return None
        
        if component is None:
            return None
        
        # Apply serialized field values
        if fields_json:
            component._deserialize_fields(fields_json)
        
        # Set enabled state
        component.enabled = enabled
        
        return component

    # ========================================================================
    # Scene State Management  
    # ========================================================================
    
    def _save_scene_state(self):
        """
        Save scene state before entering play mode.
        Uses C++ Scene::Serialize() which includes:
        - All GameObjects with their hierarchy
        - Transform data
        - C++ components (MeshRenderer, etc.)
        - Python component metadata (script GUID, fields)
        Also saves the current scene file path so we can return to
        the correct scene if the user switches scenes during play.
        """
        scene_manager = self._get_scene_manager()
        if not scene_manager:
            Debug.log_warning("Cannot save scene state: no SceneManager")
            return
        
        scene = scene_manager.get_active_scene()
        if scene:
            self._scene_backup = scene.serialize()
            # Remember which scene file was open
            from InfEngine.engine.scene_manager import SceneFileManager
            sfm = SceneFileManager.instance()
            if sfm:
                self._scene_path_backup = sfm.current_scene_path
                self._scene_dirty_backup = sfm.is_dirty
            else:
                self._scene_dirty_backup = False
            Debug.log_internal("Scene state saved (C++ serialization)")
        else:
            Debug.log_warning("No active scene to save")
    
    def _restore_scene_state(self):
        """
        Restore scene state after exiting play mode.
        Uses C++ Scene::Deserialize() which recreates:
        - All GameObjects with correct IDs
        - Transform data
        - C++ components (MeshRenderer with mesh data, etc.)
        - Pending Python component info (for Python-side recreation)

        If the user switched scenes during play, we restore the original
        scene file path so the editor returns to the correct scene.
        """
        if self._scene_backup is None:
            Debug.log_warning("No scene backup to restore")
            return
        
        if self._rebuild_active_scene(self._scene_backup, for_play=False, restore_scene_path=True):
            Debug.log_internal("Scene state restored (fresh scene rebuild)")
        else:
            Debug.log_error("Scene restore failed")
        self._scene_backup = None
        self._scene_path_backup = None
        self._scene_dirty_backup = False

    def _restore_scene_file_path(self):
        """Restore SceneFileManager's current path and camera to the pre-play scene."""
        if self._scene_path_backup is None:
            return
        from InfEngine.engine.scene_manager import SceneFileManager
        sfm = SceneFileManager.instance()
        if sfm and sfm.current_scene_path != self._scene_path_backup:
            Debug.log_internal(
                f"Restoring editor scene path: "
                f"{os.path.basename(self._scene_path_backup)}"
            )
            sfm._current_scene_path = self._scene_path_backup
            sfm._dirty = self._scene_dirty_backup
            # Restore the editor camera to the position saved for this scene
            sfm._restore_camera_state(self._scene_path_backup)
            if sfm._on_scene_changed:
                sfm._on_scene_changed()
    
    # ========================================================================
    # Event System
    # ========================================================================
    
    def add_state_change_listener(self, callback: Callable[[PlayModeEvent], None]):
        """Add a listener for play mode state changes."""
        if callback not in self._state_change_listeners:
            self._state_change_listeners.append(callback)
    
    def remove_state_change_listener(self, callback: Callable[[PlayModeEvent], None]):
        """Remove a state change listener."""
        if callback in self._state_change_listeners:
            self._state_change_listeners.remove(callback)
    
    def _notify_state_change(self, old_state: PlayModeState, new_state: PlayModeState):
        """Notify all listeners of state change."""
        event = PlayModeEvent(
            old_state=old_state,
            new_state=new_state,
            timestamp=time.time()
        )
        
        for listener in self._state_change_listeners:
            listener(event)
    

