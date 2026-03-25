"""
Unity-style Hierarchy panel showing scene objects tree.
"""

import json
import os
import time

from InfEngine.lib import InfGUIContext
from InfEngine.engine.i18n import t
from .editor_panel import EditorPanel
from .panel_registry import editor_panel
from .selection_manager import SelectionManager
from .theme import Theme, ImGuiCol, ImGuiStyleVar, ImGuiTreeNodeFlags
from .imgui_keys import (KEY_LEFT_CTRL, KEY_RIGHT_CTRL, KEY_LEFT_SHIFT,
                         KEY_RIGHT_SHIFT, KEY_F2, KEY_DELETE, KEY_ENTER,
                         KEY_ESCAPE, KEY_C, KEY_V, KEY_X)


def _scene_cache_key(scene) -> str:
    if scene is None:
        return ""
    return str(getattr(scene, "name", ""))


@editor_panel("Hierarchy", type_id="hierarchy", title_key="panel.hierarchy")
class HierarchyPanel(EditorPanel):
    """
    Unity-style Hierarchy panel showing scene objects tree.
    Uses the actual scene graph from the C++ backend via pybind11 bindings.
    Supports drag-and-drop to reparent objects.
    """
    
    WINDOW_TYPE_ID = "hierarchy"
    WINDOW_DISPLAY_NAME = "Hierarchy"
    
    # Drag-drop payload type
    DRAG_DROP_TYPE = "HIERARCHY_GAMEOBJECT"
    _STALE_ROOT_REFRESH_INTERVAL = 0.12
    _STALE_ROOT_REFRESH_ROOT_THRESHOLD = 128
    
    def __init__(self, title: str = "Hierarchy"):
        super().__init__(title, window_id="hierarchy")
        self._sel = SelectionManager.instance()
        from InfEngine.engine.undo import HierarchyUndoTracker
        self._undo = HierarchyUndoTracker()
        self._right_clicked_object_id: int = 0  # Track which object was right-clicked
        self._pending_expand_id: int = 0  # To auto-expand parent after drag-drop
        self._pending_expand_ids: set = set()  # Set of IDs to auto-expand (parent chain)
        self._on_selection_changed = None  # Callback when selection changes
        self._on_double_click_focus = None  # Callback(game_object) for double-click focus
        # Deferred selection: left-click records a candidate; committed on mouse-up
        # only if the user did NOT start a drag.  This allows drag-and-drop from
        # the Hierarchy without instantly changing the Inspector.
        self._pending_select_id: int = 0
        self._pending_ctrl: bool = False   # ctrl was held when click started
        self._pending_shift: bool = False  # shift was held when click started
        # Virtual scrolling — only render nodes inside the visible scroll viewport.
        # _cached_item_height is measured from the first rendered item each session.
        self._cached_item_height: float = 18.0  # FramePad(2)*2 + font(14) + ItemSpacing(0)
        self._item_height_measured: bool = False
        # Inline rename state (F2)
        self._rename_id: int = 0          # ID of object being renamed, 0 = not renaming
        self._rename_buf: str = ""        # Current text in the rename input
        self._rename_focus: bool = False  # True on the first frame to auto-focus the input
        # Root objects cache — avoids re-creating 1024 pybind11 wrappers every frame.
        self._cached_root_objects = None
        self._cached_scene_key: str = ""
        self._cached_structure_version: int = -1
        self._last_root_refresh_time: float = 0.0
        # UI Mode: when True, show Canvas trees normally, dim others
        self._ui_mode: bool = False
        self._ui_mode_canvas_root_ids: set = set()  # root IDs that are canvas trees
        self._on_selection_changed_ui_editor = None  # Extra callback for UI editor sync
        self._cached_ordered_ids = None
        self._cached_canvas_roots = None
        self._clipboard_entries: list[dict] = []
        self._clipboard_cut: bool = False
        self._frame_dragging: bool = False  # per-frame drag state for perf optimization
    
    def set_on_selection_changed(self, callback):
        """Set callback to be called when selection changes. Callback receives the selected GameObject or None."""
        self._on_selection_changed = callback

    def set_on_selection_changed_ui_editor(self, callback):
        """Set extra callback for syncing hierarchy selection → UI editor."""
        self._on_selection_changed_ui_editor = callback

    def set_on_double_click_focus(self, callback):
        """Set callback for double-click focus. Callback receives the GameObject."""
        self._on_double_click_focus = callback

    def set_ui_mode(self, enabled: bool):
        """Enter or exit UI Mode.  In UI Mode the hierarchy only shows Canvas trees."""
        self._ui_mode = bool(enabled)
        # Invalidate root-object cache so the filtered list is rebuilt.
        self._cached_scene_key = ""
        self._cached_structure_version = -1
        self._last_root_refresh_time = 0.0
        self._cached_ordered_ids = None
        self._cached_canvas_roots = None

    @property
    def ui_mode(self) -> bool:
        return self._ui_mode
    
    def _notify_selection_changed(self):
        """Notify listeners about selection change."""
        obj = self.get_selected_object()
        # In UI mode, only update inspector for canvas-tree objects
        if self._ui_mode and obj is not None:
            if not self._is_in_canvas_tree(obj):
                # Still notify UI editor callback but skip inspector
                if self._on_selection_changed_ui_editor:
                    self._on_selection_changed_ui_editor(obj)
                return
        if self._on_selection_changed:
            self._on_selection_changed(obj)
        if self._on_selection_changed_ui_editor:
            self._on_selection_changed_ui_editor(obj)

    def _is_ctrl(self, ctx: InfGUIContext) -> bool:
        return ctx.is_key_down(KEY_LEFT_CTRL) or ctx.is_key_down(KEY_RIGHT_CTRL)

    def _is_shift(self, ctx: InfGUIContext) -> bool:
        return ctx.is_key_down(KEY_LEFT_SHIFT) or ctx.is_key_down(KEY_RIGHT_SHIFT)

    @staticmethod
    def _collect_ordered_ids(root_objects) -> list:
        """Build a flat depth-first list of all object IDs for shift-range selection."""
        result = []
        stack = list(reversed(root_objects))
        while stack:
            obj = stack.pop()
            result.append(obj.id)
            children = obj.get_children()
            if children:
                stack.extend(reversed(children))
        return result

    def _get_root_objects_cached(self, scene, *, allow_stale: bool = False):
        """Return root objects, reusing a cached list when the scene structure hasn't changed."""
        scene_key = _scene_cache_key(scene)
        ver = scene.structure_version
        cached_roots = self._cached_root_objects
        can_reuse_stale = (
            allow_stale
            and self._cached_scene_key == scene_key
            and cached_roots is not None
            and len(cached_roots) >= self._STALE_ROOT_REFRESH_ROOT_THRESHOLD
            and (time.perf_counter() - self._last_root_refresh_time) < self._STALE_ROOT_REFRESH_INTERVAL
        )
        if scene_key != self._cached_scene_key or (ver != self._cached_structure_version and not can_reuse_stale):
            self._cached_root_objects = scene.get_root_objects()
            self._cached_ordered_ids = None  # invalidate ordered IDs cache
            self._cached_canvas_roots = None  # invalidate canvas roots cache
            self._cached_scene_key = scene_key
            self._cached_structure_version = ver
            self._last_root_refresh_time = time.perf_counter()
        return self._cached_root_objects

    def _get_ordered_ids_cached(self, root_objects) -> list:
        """Return ordered IDs, reusing cache when structure hasn't changed."""
        if self._cached_ordered_ids is None:
            self._cached_ordered_ids = self._collect_ordered_ids(root_objects)
        return self._cached_ordered_ids

    def _get_canvas_roots_cached(self, root_objects) -> list:
        """Return canvas-filtered roots, reusing cache when structure hasn't changed."""
        if self._cached_canvas_roots is None:
            self._cached_canvas_roots = self._filter_canvas_roots(root_objects)
        return self._cached_canvas_roots

    def _record_create(self, object_id: int, description: str = "Create GameObject"):
        """Record a GameObject creation through the undo system (or just mark dirty)."""
        self._undo.record_create(object_id, description)

    def _execute_reparent(self, obj_id: int, old_parent_id, new_parent_id):
        """Execute a reparent through the undo system (or directly as fallback)."""
        self._undo.record_reparent(obj_id, old_parent_id, new_parent_id)

    def _execute_hierarchy_move(self, obj_id: int, old_parent_id, new_parent_id,
                                old_sibling_index: int, new_sibling_index: int):
        """Execute a parent/order move through the undo system when available."""
        self._undo.record_move(obj_id, old_parent_id, new_parent_id,
                               old_sibling_index, new_sibling_index)

    def clear_selection(self):
        """Clear current selection and notify listeners."""
        if not self._sel.is_empty():
            self._sel.clear()
            self._notify_selection_changed()

    def set_selected_object_by_id(self, object_id: int):
        """Set selection by GameObject ID and notify listeners.

        Automatically expands all parent levels so the selected object
        is visible in the hierarchy tree.
        """
        if object_id is None:
            object_id = 0
        object_id = int(object_id)

        changed = (self._sel.get_primary() != object_id or self._sel.count() != 1)
        if changed:
            self._sel.select(object_id)

        # Always expand the parent chain so the object is visible in the tree,
        # even if the selection state didn't change (e.g. scene-view pick
        # already updated SelectionManager).
        if object_id:
            from InfEngine.lib import SceneManager
            scene = SceneManager.instance().get_active_scene()
            if scene:
                go = scene.find_by_id(object_id)
                if go:
                    self.expand_to_object(go)

        if changed:
            self._notify_selection_changed()

    def expand_to_object(self, go):
        """Expand the hierarchy tree to reveal *go* by opening all its ancestors."""
        if go is None:
            return
        parent = go.get_parent()
        while parent is not None:
            self._pending_expand_ids.add(parent.id)
            parent = parent.get_parent()
    
    # Height of the invisible separator drop zone (pixels) – thinner for Unity feel


    def _render_game_object_tree(self, ctx: InfGUIContext, obj) -> None:
        """Recursively render a GameObject and its children as tree nodes.

        Drop behaviour
        ~~~~~~~~~~~~~~
        * **Dropping onto the tree node body** → reparent the dragged object
          as a child of *obj* (appended at the end).
        * **Dropping onto the thin separator after the node** → insert the
          dragged object *after* this sibling (same parent level).

        A blue horizontal line is drawn on the separator while dragging
        to clearly indicate the insertion point (via ``IGUI.reorder_separator``).
        """
        if obj is None:
            return

        from .igui import IGUI

        obj_id = obj.id
        _dragging = self._frame_dragging

        # Use string-based push_id — obj.id is uint64_t which can exceed
        # the 32-bit int limit of push_id().
        ctx.push_id_str(str(obj_id))

        # ── Inline rename mode ───────────────────────────────────────
        if self._rename_id == obj_id:
            self._render_rename_input(ctx, obj)
            ctx.pop_id()
            return

        # Tree node flags for hierarchy items
        node_flags = (ImGuiTreeNodeFlags.OpenOnArrow
                      | ImGuiTreeNodeFlags.SpanAvailWidth
                      | ImGuiTreeNodeFlags.FramePadding)

        # Check if this object is selected
        if self._sel.is_selected(obj_id):
            node_flags |= ImGuiTreeNodeFlags.Selected

        # Check if has children - if not, use leaf flag (no arrow).
        # NoTreePushOnOpen avoids an unnecessary tree_pop for leaves.
        children = obj.get_children()
        is_leaf = len(children) == 0
        if is_leaf:
            node_flags |= ImGuiTreeNodeFlags.Leaf | ImGuiTreeNodeFlags.NoTreePushOnOpen

        # Handle auto-expansion (single id — legacy; also check multi-id set)
        if self._pending_expand_id == obj_id:
            ctx.set_next_item_open(True)
            self._pending_expand_id = 0
        if obj_id in self._pending_expand_ids:
            ctx.set_next_item_open(True)
            self._pending_expand_ids.discard(obj_id)

        # ── Display name with prefab decoration (Unity blue + diamond) ──
        is_prefab = getattr(obj, 'is_prefab_instance', False)
        display_name = f"{Theme.PREFAB_ICON} {obj.name}" if is_prefab else obj.name

        # In UI mode, dim objects not belonging to a canvas tree
        ui_dimmed = self._ui_mode and not self._is_in_canvas_tree(obj)
        text_color_pushed = 0
        if ui_dimmed:
            ctx.push_style_color(ImGuiCol.Text, *Theme.TEXT_DISABLED)
            text_color_pushed = 1
        elif is_prefab:
            ctx.push_style_color(ImGuiCol.Text, *Theme.PREFAB_TEXT)
            text_color_pushed = 1
        is_open = ctx.tree_node_ex(display_name, node_flags)
        if text_color_pushed:
            ctx.pop_style_color(1)

        # Handle selection — defer left-click until mouse-up so dragging
        # does not immediately change the Inspector.
        if ctx.is_item_clicked(0):
            # Cancel any active rename when clicking a different item
            if self._rename_id and self._rename_id != obj_id:
                self._cancel_rename()
            # Record candidate; will be committed in on_render when button released
            self._pending_select_id = obj_id
            self._pending_ctrl = self._is_ctrl(ctx)
            self._pending_shift = self._is_shift(ctx)
        if ctx.is_item_clicked(1):
            # Right-click selects immediately (needed for context menu)
            if not self._sel.is_selected(obj_id):
                self._sel.select(obj_id)
                self._notify_selection_changed()

        # Double-click → focus editor camera on this object
        # Check is_mouse_double_clicked first (rare) to avoid per-item hovered check
        if ctx.is_mouse_double_clicked(0) and ctx.is_item_hovered():
            if self._on_double_click_focus:
                self._on_double_click_focus(obj)

        # Right-click context menu for this specific object
        if ctx.begin_popup_context_item(f"ctx_menu_{obj_id}", 1):
            self._right_clicked_object_id = obj_id
            if ctx.begin_menu(t("hierarchy.create_child")):
                if ctx.begin_menu(t("hierarchy.create_3d_object")):
                    self._show_create_primitive_menu(ctx, parent_id=obj_id)
                    ctx.end_menu()
                if ctx.begin_menu(t("hierarchy.light_menu")):
                    self._show_create_light_menu(ctx, parent_id=obj_id)
                    ctx.end_menu()
                if ctx.begin_menu(t("hierarchy.rendering_menu")):
                    self._show_create_rendering_menu(ctx, parent_id=obj_id)
                    ctx.end_menu()
                if ctx.selectable(t("hierarchy.empty_object"), False, 0, 0, 0):
                    self._create_empty_object(parent_id=obj_id)
                ctx.end_menu()
            ctx.separator()
            if ctx.selectable(t("project.copy"), False, 0, 0, 0):
                self._copy_selected_objects(cut=False)
            if ctx.selectable(t("project.cut"), False, 0, 0, 0):
                self._copy_selected_objects(cut=True)
            if ctx.selectable(t("project.paste"), False, 0, 0, 0):
                self._paste_clipboard_objects()
            ctx.separator()
            if ctx.selectable(t("hierarchy.rename"), False, 0, 0, 0):
                self._begin_rename(obj_id)
            ctx.separator()
            if ctx.selectable(t("hierarchy.save_as_prefab"), False, 0, 0, 0):
                self._save_as_prefab(obj)

            # Prefab instance actions
            _is_prefab = getattr(obj, 'is_prefab_instance', False)
            if _is_prefab:
                ctx.separator()
                ctx.push_style_color(ImGuiCol.Text, *Theme.PREFAB_TEXT)
                ctx.label(t("hierarchy.prefab_label"))
                ctx.pop_style_color(1)
                if ctx.selectable(t("hierarchy.select_prefab_asset"), False, 0, 0, 0):
                    self._prefab_select_asset(obj)
                if ctx.selectable(t("hierarchy.open_prefab"), False, 0, 0, 0):
                    self._prefab_open_asset(obj)
                if ctx.selectable(t("hierarchy.apply_all_overrides"), False, 0, 0, 0):
                    self._prefab_apply_overrides(obj)
                if ctx.selectable(t("hierarchy.revert_all_overrides"), False, 0, 0, 0):
                    self._prefab_revert_overrides(obj)
                ctx.separator()
                if ctx.selectable(t("hierarchy.unpack_prefab"), False, 0, 0, 0):
                    self._prefab_unpack(obj)

            ctx.separator()
            if ctx.selectable(t("hierarchy.delete"), False, 0, 0, 0):
                self._delete_object(obj)
            ctx.end_popup()

        # Drag source - start dragging this object
        if ctx.begin_drag_drop_source(0):
            ctx.set_drag_drop_payload(self.DRAG_DROP_TYPE, obj_id)
            ctx.label(f"{obj.name}")
            ctx.end_drag_drop_source()

        # ── Drop target on the tree node body → reparent as child ──
        # Only during active drag to reduce per-node ImGui call overhead.
        if _dragging:
            IGUI.multi_drop_target(
                ctx,
                [self.DRAG_DROP_TYPE, "MODEL_GUID", "MODEL_FILE", "PREFAB_GUID", "PREFAB_FILE"],
                lambda dt, payload, _oid=obj_id: self._handle_external_drop(dt, payload, parent_id=_oid),
            )

        if is_open and not is_leaf:
            # ── Separator BEFORE first child → allows drop as first child ──
            if _dragging and children:
                first_id = children[0].id
                IGUI.reorder_separator(ctx, f"##sep_before_first_{obj_id}", self.DRAG_DROP_TYPE,
                                       lambda payload, _fid=first_id: self._move_object_adjacent(payload, _fid, after=False))
            for child in children:
                self._render_game_object_tree(ctx, child)
            ctx.tree_pop()

        # ── Separator drop zone AFTER this tree node ──
        if _dragging:
            IGUI.reorder_separator(ctx, f"##sep_after_{obj_id}", self.DRAG_DROP_TYPE,
                                   lambda payload, _oid=obj_id: self._move_object_adjacent(payload, _oid, after=True))

        ctx.pop_id()
    
    def _reparent_object(self, dragged_id: int, new_parent_id: int) -> None:
        """Reparent a GameObject to a new parent."""
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return

        dragged_obj = scene.find_by_id(dragged_id)
        new_parent = scene.find_by_id(new_parent_id)

        if dragged_obj and new_parent and dragged_id != new_parent_id:
            # Prevent parenting to own child
            if not self._is_descendant_of(new_parent, dragged_obj):
                # UI Mode validation: Canvas must stay root
                if self._ui_mode:
                    if self._go_has_canvas(dragged_obj):
                        self._show_ui_mode_warning(
                            "Canvas 只能作为根物体，不能放入其他物体下。\n"
                            "Canvas can only be a root object.")
                        return
                # Always block: UI screen components must stay under a Canvas
                if self._go_has_ui_screen_component(dragged_obj):
                    if not self._parent_has_canvas_ancestor(new_parent):
                        self._show_ui_mode_warning(
                            "UI 组件只能放在 Canvas 下。\n"
                            "UI components must be placed under a Canvas.")
                        return
                old_parent = dragged_obj.get_parent()
                old_parent_id = old_parent.id if old_parent else None
                old_index = dragged_obj.transform.get_sibling_index() if getattr(dragged_obj, "transform", None) else 0
                new_index = len(new_parent.get_children())
                if old_parent_id == new_parent_id and old_index < new_index:
                    new_index -= 1
                self._execute_hierarchy_move(dragged_id, old_parent_id, new_parent_id, old_index, new_index)
                self._pending_expand_id = new_parent_id

    def _move_object_adjacent(self, dragged_id: int, target_id: int, *, after: bool) -> None:
        """Move a GameObject before/after another sibling target."""
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if not scene or dragged_id == target_id:
            return

        dragged_obj = scene.find_by_id(dragged_id)
        target_obj = scene.find_by_id(target_id)
        if dragged_obj is None or target_obj is None:
            return
        if self._is_descendant_of(target_obj, dragged_obj):
            return

        new_parent = target_obj.get_parent()
        new_parent_id = new_parent.id if new_parent else None

        # UI Mode validation
        if self._ui_mode:
            if self._go_has_canvas(dragged_obj) and new_parent_id is not None:
                self._show_ui_mode_warning(
                    "Canvas 只能作为根物体，不能放入其他物体下。\n"
                    "Canvas can only be a root object.")
                return
            if not self._go_has_canvas(dragged_obj) and new_parent_id is None:
                self._show_ui_mode_warning(
                    "UI 元素不能成为根物体，必须放在 Canvas 下。\n"
                    "UI elements must be placed under a Canvas.")
                return

        # Always block: UI screen components must stay under a Canvas
        if self._go_has_ui_screen_component(dragged_obj):
            if new_parent_id is None or not self._parent_has_canvas_ancestor(new_parent):
                self._show_ui_mode_warning(
                    "UI 组件只能放在 Canvas 下。\n"
                    "UI components must be placed under a Canvas.")
                return

        old_parent = dragged_obj.get_parent()
        old_parent_id = old_parent.id if old_parent else None
        old_index = dragged_obj.transform.get_sibling_index() if getattr(dragged_obj, "transform", None) else 0
        target_index = target_obj.transform.get_sibling_index() if getattr(target_obj, "transform", None) else 0
        new_index = target_index + (1 if after else 0)

        if old_parent_id == new_parent_id and old_index < new_index:
            new_index -= 1

        if old_parent_id == new_parent_id and old_index == new_index:
            return

        self._execute_hierarchy_move(dragged_id, old_parent_id, new_parent_id, old_index, new_index)
        if new_parent_id is not None:
            self._pending_expand_id = new_parent_id
    
    def _is_descendant_of(self, potential_child, potential_parent) -> bool:
        """Check if potential_child is a descendant of potential_parent."""
        current = potential_child
        while current is not None:
            if current.id == potential_parent.id:
                return True
            current = current.get_parent()
        return False
    
    def _delete_object(self, obj) -> None:
        """Delete a GameObject from the scene via the undo system."""
        obj_id = obj.id
        self._undo.record_delete(obj_id, "Delete GameObject")
        if self._sel.is_selected(obj_id):
            self._sel.clear()
            self._notify_selection_changed()

    def _delete_selected_objects(self) -> None:
        """Delete all selected GameObjects."""
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return
        ids = list(self._sel.get_ids())
        if not ids:
            return
        for oid in ids:
            obj = scene.find_by_id(oid)
            if obj:
                self._undo.record_delete(oid, "Delete GameObject")
        self._sel.clear()
        self._notify_selection_changed()

    def _get_scene_clipboard_signature(self, scene) -> str:
        """Return a stable identifier for the current scene for clipboard routing."""
        from InfEngine.engine.scene_manager import SceneFileManager

        sfm = SceneFileManager.instance()
        scene_path = getattr(sfm, "current_scene_path", None) if sfm else None
        if scene_path:
            return os.path.abspath(scene_path)
        return f"scene:{getattr(scene, 'name', '')}"

    def _get_selected_top_level_objects(self, scene):
        """Return selected objects excluding descendants of other selected objects."""
        selected_ids = []
        seen = set()
        for object_id in self._sel.get_ids():
            if object_id in seen:
                continue
            obj = scene.find_by_id(object_id)
            if obj is not None:
                selected_ids.append(object_id)
                seen.add(object_id)

        selected_set = set(selected_ids)
        result = []
        for object_id in selected_ids:
            obj = scene.find_by_id(object_id)
            if obj is None:
                continue
            parent = obj.get_parent()
            skip = False
            while parent is not None:
                if parent.id in selected_set:
                    skip = True
                    break
                parent = parent.get_parent()
            if not skip:
                result.append(obj)
        return result

    def _copy_selected_objects(self, *, cut: bool = False) -> bool:
        """Copy or cut the current hierarchy selection into the internal clipboard."""
        from InfEngine.lib import SceneManager

        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return False

        roots = self._get_selected_top_level_objects(scene)
        if not roots:
            return False

        scene_sig = self._get_scene_clipboard_signature(scene)
        entries = []
        for obj in roots:
            parent = obj.get_parent()
            transform = getattr(obj, "transform", None)
            entries.append({
                "json": obj.serialize(),
                "source_scene": scene_sig,
                "source_parent_id": parent.id if parent else None,
                "source_sibling_index": transform.get_sibling_index() if transform else 0,
            })

        self._clipboard_entries = entries
        self._clipboard_cut = bool(cut)

        if cut:
            self._cut_selected_objects(roots)

        return True

    def _cut_selected_objects(self, root_objects) -> None:
        """Delete the selected root objects as a single undoable cut operation."""
        from InfEngine.engine.undo import CompoundCommand, DeleteGameObjectCommand, UndoManager

        commands = [DeleteGameObjectCommand(obj.id, "Cut GameObject") for obj in root_objects]
        if not commands:
            return

        mgr = UndoManager.instance()
        if mgr:
            cmd = commands[0] if len(commands) == 1 else CompoundCommand(commands, "Cut GameObjects")
            mgr.execute(cmd)
        else:
            from InfEngine.lib import SceneManager
            from InfEngine.engine.scene_manager import SceneFileManager
            scene = SceneManager.instance().get_active_scene()
            if scene:
                for obj in root_objects:
                    live_obj = scene.find_by_id(obj.id)
                    if live_obj is not None:
                        scene.destroy_game_object(live_obj)
                sfm = SceneFileManager.instance()
                if sfm:
                    sfm.mark_dirty()

        self._sel.clear()
        self._notify_selection_changed()

    def _clipboard_has_data(self) -> bool:
        return bool(self._clipboard_entries)

    def _instantiate_clipboard_entries(self, scene, entries, explicit_parent=None):
        """Instantiate clipboard JSON payloads into the current scene with fresh IDs."""
        from InfEngine.engine.prefab_manager import _restore_pending_py_components, _strip_prefab_runtime_fields
        from InfEngine.engine.scene_manager import SceneFileManager

        created_objects = []
        scene_sig = self._get_scene_clipboard_signature(scene)

        for entry in entries:
            parent = explicit_parent
            if parent is None and entry.get("source_scene") == scene_sig:
                source_parent_id = entry.get("source_parent_id")
                if source_parent_id is not None:
                    parent = scene.find_by_id(source_parent_id)

            try:
                obj_data = json.loads(entry["json"])
            except Exception:
                continue

            _strip_prefab_runtime_fields(obj_data)
            new_obj = scene.instantiate_from_json(json.dumps(obj_data), parent)
            if new_obj is not None:
                created_objects.append(new_obj)
                if parent is not None:
                    self._pending_expand_ids.add(parent.id)

        if created_objects and scene.has_pending_py_components():
            sfm = SceneFileManager.instance()
            asset_db = getattr(sfm, "_asset_database", None) if sfm else None
            _restore_pending_py_components(scene, asset_database=asset_db)

        return created_objects

    def _paste_clipboard_objects(self) -> bool:
        """Paste the clipboard payload into the active scene and record undo."""
        if not self._clipboard_entries:
            return False

        from InfEngine.lib import SceneManager
        from InfEngine.engine.undo import CompoundCommand, CreateGameObjectCommand, UndoManager

        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return False

        explicit_parent = None
        if self._sel.count() == 1:
            explicit_parent = self.get_selected_object()

        created_objects = self._instantiate_clipboard_entries(scene, self._clipboard_entries, explicit_parent)
        if not created_objects:
            return False

        created_ids = [obj.id for obj in created_objects]
        commands = [CreateGameObjectCommand(object_id, "Paste GameObject") for object_id in created_ids]
        mgr = UndoManager.instance()
        if mgr:
            cmd = commands[0] if len(commands) == 1 else CompoundCommand(commands, "Paste GameObjects")
            mgr.record(cmd)
        else:
            from InfEngine.engine.scene_manager import SceneFileManager
            sfm = SceneFileManager.instance()
            if sfm:
                sfm.mark_dirty()

        self._sel.set_ids(created_ids)
        self._notify_selection_changed()

        if self._clipboard_cut:
            self._clipboard_entries = []
            self._clipboard_cut = False

        return True

    def _handle_clipboard_shortcuts(self, ctx: InfGUIContext) -> None:
        """Handle hierarchy clipboard shortcuts when the panel is focused."""
        if not ctx.is_window_focused(0) or ctx.want_text_input():
            return
        if not self._is_ctrl(ctx):
            return

        if ctx.is_key_pressed(KEY_C):
            self._copy_selected_objects(cut=False)
            return
        if ctx.is_key_pressed(KEY_X):
            self._copy_selected_objects(cut=True)
            return
        if ctx.is_key_pressed(KEY_V):
            self._paste_clipboard_objects()

    # ------------------------------------------------------------------
    # Inline rename (F2)
    # ------------------------------------------------------------------

    def _begin_rename(self, obj_id: int):
        """Enter inline rename mode for *obj_id*."""
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return
        obj = scene.find_by_id(obj_id)
        if not obj:
            return
        self._rename_id = obj_id
        self._rename_buf = obj.name
        self._rename_focus = True

    def _commit_rename(self):
        """Confirm the rename — apply new name to the object."""
        if not self._rename_id:
            return
        new_name = self._rename_buf.strip()
        if new_name:
            from InfEngine.lib import SceneManager
            scene = SceneManager.instance().get_active_scene()
            if scene:
                obj = scene.find_by_id(self._rename_id)
                if obj:
                    obj.name = new_name
        self._rename_id = 0
        self._rename_buf = ""

    def _cancel_rename(self):
        """Cancel the rename without applying changes."""
        self._rename_id = 0
        self._rename_buf = ""

    def _render_rename_input(self, ctx: InfGUIContext, obj):
        """Render the inline text input for renaming *obj*.

        Replaces the tree-node row when self._rename_id matches.
        Enter commits, Escape cancels, click-away deactivates and commits.
        """
        if self._rename_focus:
            ctx.set_keyboard_focus_here()
            self._rename_focus = False

        # Use input_text_with_hint (supports flags); EnterReturnsTrue = 32
        avail_w = ctx.get_content_region_avail_width()
        ctx.set_next_item_width(avail_w)
        self._rename_buf = ctx.input_text_with_hint(
            "##rename", "", self._rename_buf, 256, 0)

        # Commit on Enter
        if ctx.is_key_pressed(KEY_ENTER):
            self._commit_rename()
            return

        # Cancel on Escape
        if ctx.is_key_pressed(KEY_ESCAPE):
            self._cancel_rename()
            return

        # Commit when the input loses focus (click elsewhere)
        if ctx.is_item_deactivated():
            self._commit_rename()

    # ------------------------------------------------------------------
    # EditorPanel hooks
    # ------------------------------------------------------------------

    def _initial_size(self):
        return (250, 400)

    def _pre_render(self, ctx: InfGUIContext):
        # ── Keyboard shortcuts (F2 rename, Delete) ───────────────────
        # Fire regardless of which panel is focused so that selecting
        # an object in the Scene view and pressing F2/Delete works.
        # Guard only against active text input to avoid conflicts.
        if not ctx.want_text_input() and not self._sel.is_empty():
            if ctx.is_key_pressed(KEY_F2) and self._rename_id == 0:
                primary = self._sel.get_primary()
                if primary:
                    self._begin_rename(primary)

            if ctx.is_key_pressed(KEY_DELETE):
                self._delete_selected_objects()

        # ── Deferred left-click selection ────────────────────────────
        # Commit the pending selection only when the left mouse button
        # has been released AND the user was not dragging.
        if self._pending_select_id != 0:
            if not ctx.is_mouse_button_down(0):
                # Mouse released — commit if not dragging
                if not ctx.is_mouse_dragging(0):
                    pid = self._pending_select_id
                    _scene = None
                    # In UI mode, block selection of non-canvas objects
                    if self._ui_mode:
                        from InfEngine.lib import SceneManager
                        _scene = SceneManager.instance().get_active_scene()
                        if _scene:
                            _go = _scene.find_by_id(pid)
                            if _go and not self._is_in_canvas_tree(_go):
                                self._pending_select_id = 0
                                self._pending_ctrl = False
                                self._pending_shift = False
                                return
                    if self._pending_ctrl:
                        self._sel.toggle(pid)
                    elif self._pending_shift:
                        if _scene is None:
                            from InfEngine.lib import SceneManager
                            _scene = SceneManager.instance().get_active_scene()
                        if _scene:
                            root_objects = self._get_root_objects_cached(_scene, allow_stale=False)
                            self._sel.set_ordered_ids(self._get_ordered_ids_cached(root_objects))
                        self._sel.range_select(pid)
                    else:
                        self._sel.select(pid)
                    self._notify_selection_changed()
                self._pending_select_id = 0
                self._pending_ctrl = False
                self._pending_shift = False
            elif ctx.is_mouse_dragging(0):
                # Drag started — cancel the pending selection
                self._pending_select_id = 0
                self._pending_ctrl = False
                self._pending_shift = False

    def on_render_content(self, ctx: InfGUIContext):
        self._handle_clipboard_shortcuts(ctx)

        # Header with scene name (shows file name + dirty indicator)
        from InfEngine.engine.scene_manager import SceneFileManager
        sfm = SceneFileManager.instance()
        if self._ui_mode:
            ctx.label(t("hierarchy.ui_mode"))
        elif sfm and sfm.is_prefab_mode:
            # Prefab Mode header — show prefab name in accent color
            prefab_name = sfm.get_display_name()
            ctx.push_style_color(ImGuiCol.Text, *Theme.PREFAB_TEXT)
            ctx.label(t("hierarchy.prefab_mode_header").format(name=prefab_name))
            ctx.pop_style_color(1)
        elif sfm:
            ctx.label(sfm.get_display_name())
        else:
            from InfEngine.lib import SceneManager
            scene = SceneManager.instance().get_active_scene()
            if scene:
                ctx.label(f"{scene.name}")
            else:
                ctx.label(t("hierarchy.no_scene"))
        
        ctx.separator()
        
        # Render scene hierarchy
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if scene:
            # Unity-compact spacing
            ctx.push_style_var_vec2(ImGuiStyleVar.ItemSpacing, *Theme.TREE_ITEM_SPC)
            ctx.push_style_var_vec2(ImGuiStyleVar.FramePadding, *Theme.TREE_FRAME_PAD)
            ctx.push_style_var_float(ImGuiStyleVar.IndentSpacing, Theme.TREE_INDENT)

            allow_stale_roots = (
                not ctx.is_window_focused(0)
                and not ctx.is_window_hovered()
                and self._cached_root_objects is not None
            )
            root_objects = self._get_root_objects_cached(scene, allow_stale=allow_stale_roots)

            # In UI Mode, compute canvas root set for dim/allow logic
            if self._ui_mode:
                canvas_roots = self._get_canvas_roots_cached(root_objects)
                self._ui_mode_canvas_root_ids = {go.id for go in canvas_roots}

            n_roots = len(root_objects) if root_objects else 0

            # Cache drag state once per frame to skip drop-target ImGui calls
            # when the user isn't dragging anything (~9 fewer calls per node).
            self._frame_dragging = ctx.is_mouse_dragging(0)

            if n_roots > 0:
                avail_w = ctx.get_content_region_avail_width()
                scroll_y = ctx.get_scroll_y()
                viewport_h = ctx.get_content_region_avail_height()
                if viewport_h <= 0:
                    viewport_h = 400.0
                start_y = ctx.get_cursor_pos_y()
                item_h = self._cached_item_height

                first_vis = max(0, int((scroll_y - start_y) / item_h) - 1)
                last_vis = min(n_roots - 1,
                               int((scroll_y + viewport_h - start_y) / item_h) + 2)

                if first_vis > 0:
                    ctx.dummy(avail_w, first_vis * item_h)

                for i in range(first_vis, last_vis + 1):
                    before_y = ctx.get_cursor_pos_y()
                    self._render_game_object_tree(ctx, root_objects[i])
                    after_y = ctx.get_cursor_pos_y()
                    actual_h = after_y - before_y
                    if actual_h > 1.0 and not self._item_height_measured:
                        self._cached_item_height = actual_h
                        item_h = actual_h
                        self._item_height_measured = True

                remaining = n_roots - last_vis - 1
                if remaining > 0:
                    ctx.dummy(avail_w, remaining * item_h)

            # Drop target for empty space - reparent to root
            remaining_height = ctx.get_content_region_avail_height()
            if remaining_height > 20:
                ctx.invisible_button("##drop_to_root", ctx.get_content_region_avail_width(), remaining_height)

                if ctx.is_item_clicked(0):
                    # Cancel rename if clicking empty space
                    self._cancel_rename()
                    self.clear_selection()

                from .igui import IGUI
                IGUI.multi_drop_target(
                    ctx,
                    [self.DRAG_DROP_TYPE, "MODEL_GUID", "MODEL_FILE", "PREFAB_GUID", "PREFAB_FILE"],
                    self._handle_external_drop,
                )

            ctx.pop_style_var(3)  # IndentSpacing + FramePadding + ItemSpacing
        
        # Parent for new objects: if something is selected, use it as parent.
        # In prefab mode, all new objects MUST be children of the prefab root.
        parent_id_for_new = None
        if sfm and sfm.is_prefab_mode:
            from InfEngine.lib import SceneManager as _SM
            _pscene = _SM.instance().get_active_scene()
            _proots = _pscene.get_root_objects() if _pscene else []
            if _proots:
                parent_id_for_new = _proots[0].id
        elif not self._sel.is_empty():
            parent_id_for_new = self._sel.get_primary()
        
        # Right-click menu for window background
        if ctx.begin_popup_context_window("", 1):
            if self._ui_mode:
                self._show_ui_mode_context_menu(ctx, parent_id=parent_id_for_new)
            else:
                if ctx.begin_menu(t("hierarchy.create_3d_object")):
                    self._show_create_primitive_menu(ctx, parent_id=parent_id_for_new)
                    ctx.end_menu()
                if ctx.begin_menu(t("hierarchy.light_menu")):
                    self._show_create_light_menu(ctx, parent_id=parent_id_for_new)
                    ctx.end_menu()
                if ctx.begin_menu(t("hierarchy.rendering_menu")):
                    self._show_create_rendering_menu(ctx, parent_id=parent_id_for_new)
                    ctx.end_menu()
                if ctx.begin_menu(t("hierarchy.ui_menu")):
                    self._show_ui_menu(ctx, parent_id=parent_id_for_new)
                    ctx.end_menu()
                if ctx.selectable(t("hierarchy.create_empty"), False, 0, 0, 0):
                    self._create_empty_object(parent_id=parent_id_for_new)

            if not self._sel.is_empty() or self._clipboard_has_data():
                ctx.separator()
                if not self._sel.is_empty():
                    if ctx.selectable(t("project.copy"), False, 0, 0, 0):
                        self._copy_selected_objects(cut=False)
                    if ctx.selectable(t("project.cut"), False, 0, 0, 0):
                        self._copy_selected_objects(cut=True)
                if self._clipboard_has_data():
                    if ctx.selectable(t("project.paste"), False, 0, 0, 0):
                        self._paste_clipboard_objects()
            
            if not self._sel.is_empty():
                ctx.separator()
                if ctx.selectable(t("hierarchy.delete_selected"), False, 0, 0, 0):
                    self._delete_selected_objects()
            
            ctx.end_popup()

    def _reparent_to_root(self, dragged_id: int) -> None:
        """Reparent a GameObject to root (no parent)."""
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return

        dragged_obj = scene.find_by_id(dragged_id)
        if dragged_obj:
            # UI Mode validation: only Canvas can be root
            if self._ui_mode and not self._go_has_canvas(dragged_obj):
                self._show_ui_mode_warning(
                    "UI 元素不能成为根物体，必须放在 Canvas 下。\n"
                    "UI elements must be placed under a Canvas.")
                return
            # Always block: UI screen components cannot be root
            if self._go_has_ui_screen_component(dragged_obj):
                self._show_ui_mode_warning(
                    "UI 组件只能放在 Canvas 下。\n"
                    "UI components must be placed under a Canvas.")
                return
            old_parent = dragged_obj.get_parent()
            old_parent_id = old_parent.id if old_parent else None
            old_index = dragged_obj.transform.get_sibling_index() if getattr(dragged_obj, "transform", None) else 0
            root_count = len(scene.get_root_objects())
            new_index = max(0, root_count - (1 if old_parent_id is None else 0))
            if old_parent_id is not None or old_index != new_index:
                self._execute_hierarchy_move(dragged_id, old_parent_id, None, old_index, new_index)
    
    def _show_create_primitive_menu(self, ctx: InfGUIContext, parent_id: int = None) -> None:
        """Show the Create 3D Object submenu."""
        from InfEngine.lib import SceneManager, PrimitiveType
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            ctx.label(t("hierarchy.no_scene"))
            return

        primitives = [
            (t("hierarchy.primitive_cube"), PrimitiveType.Cube),
            (t("hierarchy.primitive_sphere"), PrimitiveType.Sphere),
            (t("hierarchy.primitive_capsule"), PrimitiveType.Capsule),
            (t("hierarchy.primitive_cylinder"), PrimitiveType.Cylinder),
            (t("hierarchy.primitive_plane"), PrimitiveType.Plane),
        ]

        for name, prim_type in primitives:
            if ctx.selectable(name, False, 0, 0, 0):
                new_obj = scene.create_primitive(prim_type)
                if new_obj:
                    # Set parent if specified
                    if parent_id is not None:
                        parent = scene.find_by_id(parent_id)
                        if parent:
                            new_obj.set_parent(parent)
                            self._pending_expand_id = parent_id
                    self._sel.select(new_obj.id)
                    self._record_create(new_obj.id, f"Create {name.split()[0]}")
                    # Notify Inspector about the new selection
                    self._notify_selection_changed()
    
    def _show_create_light_menu(self, ctx: InfGUIContext, parent_id: int = None) -> None:
        """Show the Create Light submenu."""
        from InfEngine.lib import SceneManager, LightType, LightShadows, Vector3
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            ctx.label(t("hierarchy.no_scene"))
            return

        light_types = [
            (t("hierarchy.light_directional"), LightType.Directional),
            (t("hierarchy.light_point"), LightType.Point),
            (t("hierarchy.light_spot"), LightType.Spot),
        ]

        for name, light_type in light_types:
            if ctx.selectable(name, False, 0, 0, 0):
                # Create a new light object
                new_obj = scene.create_game_object(name.split()[0])  # Use Chinese name
                if new_obj:
                    # Add Light component
                    light_comp = new_obj.add_component("Light")
                    if light_comp:
                        light_comp.light_type = light_type
                        light_comp.shadows = LightShadows.Hard
                        # Set default values based on type
                        if light_type == LightType.Directional:
                            # Default directional light rotation (pointing down-forward)
                            trans = new_obj.transform
                            if trans:
                                trans.euler_angles = Vector3(50.0, -30.0, 0.0)
                        elif light_type == LightType.Point:
                            light_comp.range = 10.0
                        elif light_type == LightType.Spot:
                            light_comp.range = 10.0
                            light_comp.outer_spot_angle = 45.0
                            light_comp.spot_angle = 30.0

                    # Set parent if specified
                    if parent_id is not None:
                        parent = scene.find_by_id(parent_id)
                        if parent:
                            new_obj.set_parent(parent)
                            self._pending_expand_id = parent_id
                    self._sel.select(new_obj.id)
                    self._record_create(new_obj.id, f"Create {name.split()[0]}")
                    self._notify_selection_changed()

    def _show_create_rendering_menu(self, ctx: InfGUIContext, parent_id: int = None) -> None:
        """Show the Rendering submenu."""
        if ctx.selectable(t("hierarchy.camera"), False, 0, 0, 0):
            self._create_camera_object(parent_id=parent_id)
        if ctx.selectable(t("hierarchy.render_stack"), False, 0, 0, 0):
            self._create_render_stack_object(parent_id=parent_id)

    def _create_empty_object(self, parent_id: int = None) -> None:
        """Create an empty GameObject in the scene."""
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if scene:
            new_obj = scene.create_game_object("GameObject")
            if new_obj:
                # Set parent if specified
                if parent_id is not None:
                    parent = scene.find_by_id(parent_id)
                    if parent:
                        new_obj.set_parent(parent)
                        self._pending_expand_id = parent_id
                self._sel.select(new_obj.id)
                self._record_create(new_obj.id, "Create Empty")
                # Notify Inspector about the new selection
                self._notify_selection_changed()

    def _create_camera_object(self, parent_id: int = None) -> None:
        """Create a Camera GameObject in the scene."""
        from InfEngine.lib import SceneManager
        from InfEngine.math import Vector3

        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return

        has_main_camera = getattr(scene, "main_camera", None) is not None
        object_name = "Main Camera" if not has_main_camera and parent_id is None else "Camera"
        new_obj = scene.create_game_object(object_name)
        if not new_obj:
            return

        camera_comp = new_obj.add_component("Camera")
        if camera_comp is None:
            return

        if parent_id is not None:
            parent = scene.find_by_id(parent_id)
            if parent:
                new_obj.set_parent(parent)
                self._pending_expand_id = parent_id
        elif not has_main_camera:
            new_obj.tag = "MainCamera"
            new_obj.transform.position = Vector3(0.0, 1.0, -10.0)

        self._sel.select(new_obj.id)
        self._record_create(new_obj.id, "Create Camera")
        self._notify_selection_changed()

    def _create_render_stack_object(self, parent_id: int = None) -> None:
        """Create a RenderStack GameObject in the scene."""
        from InfEngine.lib import GameObject, SceneManager
        from InfEngine.renderstack import RenderStack as RenderStackCls

        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return

        new_obj = scene.create_game_object("RenderStack")
        if not new_obj:
            return

        stack = new_obj.add_py_component(RenderStackCls())
        if stack is None:
            GameObject.destroy(new_obj)
            return

        if parent_id is not None:
            parent = scene.find_by_id(parent_id)
            if parent:
                new_obj.set_parent(parent)
                self._pending_expand_id = parent_id

        self._sel.select(new_obj.id)
        self._record_create(new_obj.id, "Create RenderStack")
        self._notify_selection_changed()

    def _create_model_object(self, model_ref: str, parent_id: int = None, is_guid: bool = False) -> None:
        """Create a GameObject hierarchy from a dropped 3D model asset.

        For models with multiple submeshes, create_from_model returns a parent
        container with one child per submesh, each rendering its own submesh.
        """
        from InfEngine.lib import SceneManager, AssetRegistry
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return
        guid = model_ref if is_guid else ""
        if not guid:
            registry = AssetRegistry.instance()
            adb = registry.get_asset_database()
            if not adb:
                return
            guid = adb.get_guid_from_path(model_ref)
        if not guid:
            return

        new_obj = scene.create_from_model(guid)
        if not new_obj:
            return

        if parent_id is not None:
            parent = scene.find_by_id(parent_id)
            if parent:
                new_obj.set_parent(parent)
                self._pending_expand_id = parent_id
        self._sel.select(new_obj.id)
        self._record_create(new_obj.id, "Create Model")
        self._notify_selection_changed()

    def _instantiate_prefab(self, prefab_ref: str, parent_id: int = None, is_guid: bool = False) -> None:
        """Instantiate a prefab dropped from the Project panel into the scene."""
        from InfEngine.debug import Debug
        from InfEngine.lib import SceneManager, AssetRegistry
        from InfEngine.engine.prefab_manager import instantiate_prefab
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return

        adb = None
        registry = AssetRegistry.instance()
        if registry:
            adb = registry.get_asset_database()

        parent = None
        if parent_id is not None:
            parent = scene.find_by_id(parent_id)

        try:
            if is_guid:
                new_obj = instantiate_prefab(guid=prefab_ref, scene=scene,
                                             parent=parent, asset_database=adb)
            else:
                new_obj = instantiate_prefab(file_path=prefab_ref, scene=scene,
                                             parent=parent, asset_database=adb)
        except Exception as exc:
            Debug.log_error(f"Prefab instantiation failed: {exc}")
            return

        if new_obj:
            if parent_id is not None and parent:
                self._pending_expand_id = parent_id
            self._sel.select(new_obj.id)
            self._record_create(new_obj.id, "Instantiate Prefab")
            self._notify_selection_changed()

    def _handle_external_drop(self, drop_type: str, payload, parent_id: int = None) -> None:
        from InfEngine.debug import Debug

        # In Prefab Mode, force all new objects under the prefab root.
        from InfEngine.engine.scene_manager import SceneFileManager
        sfm = SceneFileManager.instance()
        if sfm and sfm.is_prefab_mode and parent_id is None:
            from InfEngine.lib import SceneManager as _SM
            _pscene = _SM.instance().get_active_scene()
            _proots = _pscene.get_root_objects() if _pscene else []
            if _proots:
                parent_id = _proots[0].id

        try:
            if drop_type == self.DRAG_DROP_TYPE:
                if parent_id is None:
                    self._reparent_to_root(payload)
                else:
                    self._reparent_object(payload, parent_id)
                return

            if drop_type in ("PREFAB_GUID", "PREFAB_FILE"):
                self._instantiate_prefab(payload, parent_id=parent_id, is_guid=(drop_type == "PREFAB_GUID"))
                return

            if drop_type in ("MODEL_GUID", "MODEL_FILE"):
                self._create_model_object(payload, parent_id=parent_id, is_guid=(drop_type == "MODEL_GUID"))
        except Exception as exc:
            Debug.log_error(f"Hierarchy drop failed ({drop_type}): {exc}")

    def _save_as_prefab(self, game_object) -> None:
        """Save a GameObject as a .prefab file in the project's Assets folder."""
        from InfEngine.engine.project_context import get_project_root
        from InfEngine.engine.prefab_manager import save_prefab, PREFAB_EXTENSION
        from InfEngine.lib import AssetRegistry
        from InfEngine.debug import Debug

        root = get_project_root()
        if not root:
            Debug.log_warning("No project root — cannot save prefab.")
            return

        assets_dir = os.path.join(root, "Assets")
        os.makedirs(assets_dir, exist_ok=True)

        adb = None
        registry = AssetRegistry.instance()
        if registry:
            adb = registry.get_asset_database()

        from .project_file_ops import get_unique_name
        prefab_name = get_unique_name(assets_dir, game_object.name, PREFAB_EXTENSION)
        file_path = os.path.join(assets_dir, prefab_name + PREFAB_EXTENSION)

        if save_prefab(game_object, file_path, asset_database=adb):
            Debug.log_internal(f"Prefab saved: {file_path}")

    def _resolve_prefab_path(self, guid: str):
        """Resolve a prefab GUID to a file path."""
        if not guid:
            return None
        try:
            from InfEngine.lib import AssetRegistry
            registry = AssetRegistry.instance()
            if registry:
                adb = registry.get_asset_database()
                if adb:
                    return adb.get_path_from_guid(guid)
        except Exception:
            pass
        return None

    def _prefab_select_asset(self, obj):
        """Select the prefab asset in the Project panel."""
        guid = getattr(obj, 'prefab_guid', '')
        path = self._resolve_prefab_path(guid)
        if path:
            from InfEngine.engine.ui.event_bus import EditorEventBus
            EditorEventBus.instance().emit("select_asset", path)

    def _prefab_open_asset(self, obj):
        """Open the prefab file in the asset inspector."""
        guid = getattr(obj, 'prefab_guid', '')
        path = self._resolve_prefab_path(guid)
        if path:
            from InfEngine.engine.ui.event_bus import EditorEventBus
            EditorEventBus.instance().emit("open_asset", path)

    def _prefab_apply_overrides(self, obj):
        """Apply all overrides back to the .prefab file."""
        guid = getattr(obj, 'prefab_guid', '')
        path = self._resolve_prefab_path(guid)
        if path:
            from InfEngine.engine.prefab_overrides import apply_overrides_to_prefab
            apply_overrides_to_prefab(obj, path)

    def _prefab_revert_overrides(self, obj):
        """Revert the instance to match the source .prefab file."""
        guid = getattr(obj, 'prefab_guid', '')
        path = self._resolve_prefab_path(guid)
        if path:
            from InfEngine.engine.prefab_overrides import revert_overrides
            revert_overrides(obj, path)

    def _prefab_unpack(self, obj):
        """Remove prefab linkage — unpack the instance to regular GameObjects."""
        self._unpack_prefab_recursive(obj)
        from InfEngine.debug import Debug
        Debug.log_internal(f"Unpacked prefab instance: {obj.name}")

    def _unpack_prefab_recursive(self, obj):
        """Recursively clear prefab_guid and prefab_root on an object and its children."""
        try:
            obj.prefab_guid = ""
            obj.prefab_root = False
        except Exception:
            pass
        try:
            for child in obj.get_children():
                self._unpack_prefab_recursive(child)
        except Exception:
            pass

    def get_selected_object(self):
        """Get the currently selected (primary) GameObject, or None."""
        primary = self._sel.get_primary()
        if primary == 0:
            return None
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if scene:
            return scene.find_by_id(primary)
        return None

    def get_selected_objects(self):
        """Get all selected GameObjects in selection order."""
        ids = self._sel.get_ids()
        if not ids:
            return []
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return []
        result = []
        for oid in ids:
            obj = scene.find_by_id(oid)
            if obj is not None:
                result.append(obj)
        return result

    # ------------------------------------------------------------------
    # UI Mode helpers
    # ------------------------------------------------------------------

    def _filter_canvas_roots(self, root_objects):
        """Return only root GameObjects that have a UICanvas component (or ancestor of one)."""
        return [go for go in root_objects if self._has_canvas_descendant(go)]

    @staticmethod
    def _has_canvas_descendant(go) -> bool:
        from InfEngine.ui import UICanvas
        stack = [go]
        while stack:
            cur = stack.pop()
            for comp in cur.get_py_components():
                if isinstance(comp, UICanvas):
                    return True
            stack.extend(cur.get_children())
        return False

    @staticmethod
    def _go_has_canvas(go) -> bool:
        """Check if a GameObject itself has a UICanvas component."""
        from InfEngine.ui import UICanvas
        for comp in go.get_py_components():
            if isinstance(comp, UICanvas):
                return True
        return False

    @staticmethod
    def _go_has_ui_screen_component(go) -> bool:
        """Check if a GameObject has any InfUIScreenComponent (Text/Image/Button etc.)."""
        from InfEngine.ui.inf_ui_screen_component import InfUIScreenComponent
        for comp in go.get_py_components():
            if isinstance(comp, InfUIScreenComponent):
                return True
        return False

    @staticmethod
    def _parent_has_canvas_ancestor(parent) -> bool:
        """Check if *parent* itself has a Canvas or has a Canvas ancestor."""
        from InfEngine.ui import UICanvas
        cur = parent
        while cur is not None:
            for comp in cur.get_py_components():
                if isinstance(comp, UICanvas):
                    return True
            cur = cur.get_parent()
        return False

    def _is_under_canvas(self, go) -> bool:
        """Check if *go* is a descendant (direct or indirect) of a Canvas GameObject."""
        from InfEngine.ui import UICanvas
        parent = go.get_parent()
        while parent is not None:
            for comp in parent.get_py_components():
                if isinstance(comp, UICanvas):
                    return True
            parent = parent.get_parent()
        return False

    def _is_in_canvas_tree(self, go) -> bool:
        """Check if *go* belongs to a canvas tree (itself, ancestor, or descendant has UICanvas)."""
        # Walk up to the root and check if that root is in the canvas root set
        cur = go
        while True:
            parent = cur.get_parent()
            if parent is None:
                break
            cur = parent
        return cur.id in self._ui_mode_canvas_root_ids

    @staticmethod
    def _show_ui_mode_warning(msg: str):
        """Log a warning to the Console panel."""
        from InfEngine.debug import Debug
        Debug.log_warning(msg)

    def _show_ui_menu(self, ctx: InfGUIContext, parent_id: int = None) -> None:
        """Show the UI submenu."""
        if ctx.selectable(t("hierarchy.ui_canvas"), False, 0, 0, 0):
            self._create_ui_canvas(parent_id=parent_id)

    def _show_ui_mode_context_menu(self, ctx: InfGUIContext, parent_id: int = None):
        """Show right-click context menu in UI Mode (Canvas/Text creation only)."""
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            ctx.label(t("hierarchy.no_scene"))
            return

        self._show_ui_menu(ctx, parent_id=parent_id)
        if ctx.selectable(t("hierarchy.ui_text"), False, 0, 0, 0):
            self._create_ui_text(parent_id=parent_id)
        if ctx.selectable(t("hierarchy.ui_button"), False, 0, 0, 0):
            self._create_ui_button(parent_id=parent_id)

    def _create_ui_canvas(self, parent_id: int = None):
        """Create a Canvas GameObject with UICanvas component (always as root)."""
        from InfEngine.lib import SceneManager
        from InfEngine.ui import UICanvas as UICanvasCls
        from InfEngine.ui.ui_canvas_utils import invalidate_canvas_cache
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return
        go = scene.create_game_object("Canvas")
        if go:
            go.add_py_component(UICanvasCls())
            invalidate_canvas_cache()
            # Canvas is always a root object — ignore parent_id
            self._sel.select(go.id)
            self._record_create(go.id, "Create Canvas")
            self._notify_selection_changed()

    def _create_ui_text(self, parent_id: int = None):
        """Create a Text GameObject with UIText component under a Canvas."""
        from InfEngine.lib import SceneManager
        from InfEngine.ui import UIText as UITextCls, UICanvas
        from InfEngine.ui.ui_canvas_utils import invalidate_canvas_cache
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return

        # Find a suitable canvas parent
        canvas_parent_id = parent_id
        if canvas_parent_id is not None:
            # Check if the parent (or an ancestor) is a Canvas
            obj = scene.find_by_id(canvas_parent_id)
            if obj:
                found_canvas = False
                current = obj
                while current is not None:
                    for c in current.get_py_components():
                        if isinstance(c, UICanvas):
                            canvas_parent_id = current.id
                            found_canvas = True
                            break
                    if found_canvas:
                        break
                    current = current.get_parent()
                if not found_canvas:
                    canvas_parent_id = obj.id  # still use as parent

        go = scene.create_game_object("Text")
        if go:
            go.add_py_component(UITextCls())
            if canvas_parent_id is not None:
                parent = scene.find_by_id(canvas_parent_id)
                if parent:
                    go.set_parent(parent)
                    self._pending_expand_id = canvas_parent_id
            invalidate_canvas_cache()
            self._sel.select(go.id)
            self._record_create(go.id, "Create Text")
            self._notify_selection_changed()

    def _create_ui_button(self, parent_id: int = None):
        """Create a Button GameObject with UIButton component under a Canvas."""
        from InfEngine.lib import SceneManager
        from InfEngine.ui import UIButton as UIButtonCls, UICanvas
        from InfEngine.ui.ui_canvas_utils import invalidate_canvas_cache
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return

        canvas_parent_id = parent_id
        if canvas_parent_id is not None:
            obj = scene.find_by_id(canvas_parent_id)
            if obj:
                found_canvas = False
                current = obj
                while current is not None:
                    for c in current.get_py_components():
                        if isinstance(c, UICanvas):
                            canvas_parent_id = current.id
                            found_canvas = True
                            break
                    if found_canvas:
                        break
                    current = current.get_parent()
                if not found_canvas:
                    canvas_parent_id = obj.id

        go = scene.create_game_object("Button")
        if go:
            btn = UIButtonCls()
            btn.width = 160.0
            btn.height = 40.0
            go.add_py_component(btn)
            if canvas_parent_id is not None:
                parent = scene.find_by_id(canvas_parent_id)
                if parent:
                    go.set_parent(parent)
                    self._pending_expand_id = canvas_parent_id
            invalidate_canvas_cache()
            self._sel.select(go.id)
            self._record_create(go.id, "Create Button")
            self._notify_selection_changed()
