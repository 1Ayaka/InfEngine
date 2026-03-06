"""
Unity-style Hierarchy panel showing scene objects tree.
"""

from InfEngine.lib import InfGUIContext
from .closable_panel import ClosablePanel
from .theme import Theme, ImGuiCol, ImGuiStyleVar, ImGuiTreeNodeFlags


class HierarchyPanel(ClosablePanel):
    """
    Unity-style Hierarchy panel showing scene objects tree.
    Uses the actual scene graph from the C++ backend via pybind11 bindings.
    Supports drag-and-drop to reparent objects.
    """
    
    WINDOW_TYPE_ID = "hierarchy"
    WINDOW_DISPLAY_NAME = "层级 Hierarchy"
    
    # Drag-drop payload type
    DRAG_DROP_TYPE = "HIERARCHY_GAMEOBJECT"
    
    def __init__(self, title: str = "层级 Hierarchy"):
        super().__init__(title, window_id="hierarchy")
        self._selected_object_id: int = 0
        self._scene_dirty: bool = True
        self._right_clicked_object_id: int = 0  # Track which object was right-clicked
        self._pending_expand_id: int = 0  # To auto-expand parent after drag-drop
        self._pending_expand_ids: set = set()  # Set of IDs to auto-expand (parent chain)
        self._on_selection_changed = None  # Callback when selection changes
        # Deferred selection: left-click records a candidate; committed on mouse-up
        # only if the user did NOT start a drag.  This allows drag-and-drop from
        # the Hierarchy without instantly changing the Inspector.
        self._pending_select_id: int = 0
        # Virtual scrolling — only render nodes inside the visible scroll viewport.
        # _cached_item_height is measured from the first rendered item each session.
        self._cached_item_height: float = 27.0  # FramePad(5)*2 + font(14) + ItemSpacing(3)
        self._item_height_measured: bool = False
        # Root objects cache — avoids re-creating 1024 pybind11 wrappers every frame.
        self._cached_root_objects = None
        self._cached_structure_version: int = -1
        # UI Mode: when True, only show Canvas GameObjects & their children
        self._ui_mode: bool = False
    
    def set_on_selection_changed(self, callback):
        """Set callback to be called when selection changes. Callback receives the selected GameObject or None."""
        self._on_selection_changed = callback

    def set_ui_mode(self, enabled: bool):
        """Enter or exit UI Mode.  In UI Mode the hierarchy only shows Canvas trees."""
        self._ui_mode = bool(enabled)
        # Invalidate root-object cache so the filtered list is rebuilt.
        self._cached_structure_version = -1

    @property
    def ui_mode(self) -> bool:
        return self._ui_mode
    
    def _notify_selection_changed(self):
        """Notify listeners about selection change."""
        if self._on_selection_changed:
            self._on_selection_changed(self.get_selected_object())

    def _get_root_objects_cached(self, scene):
        """Return root objects, reusing a cached list when the scene structure hasn't changed."""
        ver = scene.structure_version
        if ver != self._cached_structure_version:
            self._cached_root_objects = scene.get_root_objects()
            self._cached_structure_version = ver
        return self._cached_root_objects

    def _mark_scene_dirty(self):
        """Mark the scene as dirty (modified) both locally and in SceneFileManager."""
        self._scene_dirty = True
        from InfEngine.engine.scene_manager import SceneFileManager
        sfm = SceneFileManager.instance()
        if sfm:
            sfm.mark_dirty()

    def _record_create(self, object_id: int, description: str = "Create GameObject"):
        """Record a GameObject creation through the undo system (or just mark dirty)."""
        from InfEngine.engine.undo import UndoManager, CreateGameObjectCommand
        mgr = UndoManager.instance()
        if mgr:
            mgr.record(CreateGameObjectCommand(object_id, description))
            return
        self._mark_scene_dirty()

    def _execute_reparent(self, obj_id: int, old_parent_id, new_parent_id):
        """Execute a reparent through the undo system (or directly as fallback)."""
        from InfEngine.engine.undo import UndoManager, ReparentCommand
        mgr = UndoManager.instance()
        if mgr:
            mgr.execute(ReparentCommand(obj_id, old_parent_id, new_parent_id, "Reparent"))
            return
        # Fallback: direct reparent + mark dirty
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if scene:
            obj = scene.find_by_id(obj_id)
            if obj:
                if new_parent_id is not None:
                    parent = scene.find_by_id(new_parent_id)
                    obj.set_parent(parent)
                else:
                    obj.set_parent(None)
        self._mark_scene_dirty()

    def clear_selection(self):
        """Clear current selection and notify listeners."""
        if self._selected_object_id != 0:
            self._selected_object_id = 0
            self._notify_selection_changed()

    def set_selected_object_by_id(self, object_id: int):
        """Set selection by GameObject ID and notify listeners."""
        if object_id is None:
            object_id = 0
        object_id = int(object_id)

        if self._selected_object_id != object_id:
            self._selected_object_id = object_id
            self._notify_selection_changed()

    def expand_to_object(self, go):
        """Expand the hierarchy tree to reveal *go* by opening all its ancestors."""
        if go is None:
            return
        parent = go.get_parent()
        while parent is not None:
            self._pending_expand_ids.add(parent.id)
            parent = parent.get_parent()
    
    def _render_game_object_tree(self, ctx: InfGUIContext, obj) -> None:
        """Recursively render a GameObject and its children as tree nodes."""
        if obj is None:
            return
        
        # Use unique ID to avoid ImGui ID conflicts
        ctx.push_id(obj.id)
        
        # Tree node flags for hierarchy items
        node_flags = (ImGuiTreeNodeFlags.OpenOnArrow
                      | ImGuiTreeNodeFlags.OpenOnDoubleClick
                      | ImGuiTreeNodeFlags.SpanAvailWidth
                      | ImGuiTreeNodeFlags.FramePadding)
        
        # Check if this object is selected
        if self._selected_object_id == obj.id:
            node_flags |= ImGuiTreeNodeFlags.Selected
        
        # Check if has children - if not, use leaf flag (no arrow)
        children = obj.get_children()
        if len(children) == 0:
            node_flags |= ImGuiTreeNodeFlags.Leaf
        
        # Handle auto-expansion (single id — legacy; also check multi-id set)
        if self._pending_expand_id == obj.id:
            ctx.set_next_item_open(True)
            self._pending_expand_id = 0
        if obj.id in self._pending_expand_ids:
            ctx.set_next_item_open(True)
            self._pending_expand_ids.discard(obj.id)

        # Create tree node - display name can be duplicated, ID is unique via PushID
        is_open = ctx.tree_node_ex(obj.name, node_flags)
        
        # Handle selection — defer left-click until mouse-up so dragging
        # does not immediately change the Inspector.
        if ctx.is_item_clicked(0):
            # Record candidate; will be committed in on_render when button released
            self._pending_select_id = obj.id
        if ctx.is_item_clicked(1):
            # Right-click selects immediately (needed for context menu)
            if self._selected_object_id != obj.id:
                self._selected_object_id = obj.id
                self._notify_selection_changed()
            
        # Right-click context menu for this specific object
        # IMPORTANT: Trigger context menu BEFORE drag source to ensure it captures right-click properly
        if ctx.begin_popup_context_item(f"ctx_menu_{obj.id}", 1):
            self._right_clicked_object_id = obj.id
            if ctx.begin_menu("创建子对象  Create Child"):
                self._show_create_primitive_menu(ctx, parent_id=obj.id)
                if ctx.selectable("空对象  Empty", False, 0, 0, 0):
                    self._create_empty_object(parent_id=obj.id)
                ctx.end_menu()
            ctx.separator()
            if ctx.selectable("删除  Delete", False, 0, 0, 0):
                self._delete_object(obj)
            ctx.end_popup()
        
        # Drag source - start dragging this object
        if ctx.begin_drag_drop_source(0):
            ctx.set_drag_drop_payload(self.DRAG_DROP_TYPE, obj.id)
            ctx.label(f"{obj.name}")
            ctx.end_drag_drop_source()
        
        # Drop target - accept dragged objects as children
        if ctx.begin_drag_drop_target():
            payload = ctx.accept_drag_drop_payload(self.DRAG_DROP_TYPE)
            if payload is not None:
                self._reparent_object(payload, obj.id)
            ctx.end_drag_drop_target()
        
        if is_open:
            # Render children
            for child in children:
                self._render_game_object_tree(ctx, child)
            ctx.tree_pop()
        
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
                old_parent = dragged_obj.get_parent()
                old_parent_id = old_parent.id if old_parent else None
                self._execute_reparent(dragged_id, old_parent_id, new_parent_id)
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
        """Delete a GameObject from the scene."""
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if scene:
            scene.destroy_game_object(obj)
            if self._selected_object_id == obj.id:
                self._selected_object_id = 0
                self._notify_selection_changed()
            self._mark_scene_dirty()
    
    def on_render(self, ctx: InfGUIContext):
        if not self._is_open:
            return
            
        ctx.set_next_window_size(250, 400, Theme.COND_FIRST_USE_EVER)

        # ── Deferred left-click selection ────────────────────────────
        # Commit the pending selection only when the left mouse button
        # has been released AND the user was not dragging.
        if self._pending_select_id != 0:
            if not ctx.is_mouse_button_down(0):
                # Mouse released — commit if not dragging
                if not ctx.is_mouse_dragging(0):
                    if self._selected_object_id != self._pending_select_id:
                        self._selected_object_id = self._pending_select_id
                        self._notify_selection_changed()
                self._pending_select_id = 0
            elif ctx.is_mouse_dragging(0):
                # Drag started — cancel the pending selection
                self._pending_select_id = 0

        if self._begin_closable_window(ctx, 0):
            # Header with scene name (shows file name + dirty indicator)
            from InfEngine.engine.scene_manager import SceneFileManager
            sfm = SceneFileManager.instance()
            if self._ui_mode:
                ctx.label("UI Mode")
            elif sfm:
                ctx.label(sfm.get_display_name())
            else:
                from InfEngine.lib import SceneManager
                scene = SceneManager.instance().get_active_scene()
                if scene:
                    ctx.label(f"{scene.name}")
                else:
                    ctx.label("(无场景 No Scene)")
            
            ctx.separator()
            
            # Render scene hierarchy
            from InfEngine.lib import SceneManager
            scene = SceneManager.instance().get_active_scene()
            if scene:
                # Small gap between objects
                ctx.push_style_var_vec2(ImGuiStyleVar.ItemSpacing, *Theme.TREE_ITEM_SPC)
                # Make tree nodes taller (~+10px top/bottom, easier to click)
                ctx.push_style_var_vec2(ImGuiStyleVar.FramePadding, *Theme.TREE_FRAME_PAD)
                # Drag-drop target highlight
                Theme.push_drag_drop_target_style(ctx)  # 1 colour

                root_objects = self._get_root_objects_cached(scene)

                # In UI Mode, filter to only Canvas root GameObjects
                if self._ui_mode:
                    root_objects = self._filter_canvas_roots(root_objects)

                n_roots = len(root_objects) if root_objects else 0

                if n_roots > 0:
                    avail_w = ctx.get_content_region_avail_width()
                    scroll_y = ctx.get_scroll_y()
                    # Capture viewport height BEFORE rendering any items while
                    # cursor is still near the top of the content region.
                    viewport_h = ctx.get_content_region_avail_height()
                    if viewport_h <= 0:
                        viewport_h = 400.0
                    start_y = ctx.get_cursor_pos_y()
                    item_h = self._cached_item_height

                    # --- Virtual scroll: compute first/last visible root index ---
                    # Items at content-Y < scroll_y are above the viewport.
                    # Items at content-Y > scroll_y + viewport_h are below it.
                    # Each root item occupies approximately item_h pixels
                    # (exact for flat/collapsed trees; slightly off when nodes
                    #  are expanded, but gracefully degrades).
                    first_vis = max(0, int((scroll_y - start_y) / item_h) - 1)
                    last_vis = min(n_roots - 1,
                                   int((scroll_y + viewport_h - start_y) / item_h) + 2)

                    # Spacer for the items that scroll above the viewport
                    if first_vis > 0:
                        ctx.dummy(avail_w, first_vis * item_h)

                    # Render only the items inside (or near) the viewport
                    for i in range(first_vis, last_vis + 1):
                        before_y = ctx.get_cursor_pos_y()
                        self._render_game_object_tree(ctx, root_objects[i])
                        after_y = ctx.get_cursor_pos_y()
                        # Measure the true item height from the first rendered item
                        # (accounts for current font size + style vars).
                        actual_h = after_y - before_y
                        if actual_h > 1.0 and not self._item_height_measured:
                            self._cached_item_height = actual_h
                            item_h = actual_h
                            self._item_height_measured = True

                    # Spacer for items below the viewport
                    remaining = n_roots - last_vis - 1
                    if remaining > 0:
                        ctx.dummy(avail_w, remaining * item_h)

                # Drop target for empty space - reparent to root
                # Keep style pushed so this target also shows the red highlight
                remaining_height = ctx.get_content_region_avail_height()
                if remaining_height > 20:
                    ctx.invisible_button("##drop_to_root", ctx.get_content_region_avail_width(), remaining_height)

                    # Left click on empty space deselects
                    if ctx.is_item_clicked(0):
                        self.clear_selection()

                    if ctx.begin_drag_drop_target():
                        payload = ctx.accept_drag_drop_payload(self.DRAG_DROP_TYPE)
                        if payload is not None:
                            self._reparent_to_root(payload)
                        ctx.end_drag_drop_target()

                ctx.pop_style_color(1)
                ctx.pop_style_var(2)  # FramePadding + ItemSpacing
            
            # Parent for new objects: if something is selected, use it as parent
            parent_id_for_new = None
            if self._selected_object_id != 0:
                parent_id_for_new = self._selected_object_id
            
            # Right-click menu for window background
            if ctx.begin_popup_context_window("", 1):
                if self._ui_mode:
                    # UI Mode context menu — only UI-related creation
                    self._show_ui_mode_context_menu(ctx, parent_id=parent_id_for_new)
                else:
                    if ctx.begin_menu("创建 3D 对象  Create 3D Object"):
                        self._show_create_primitive_menu(ctx, parent_id=parent_id_for_new)
                        ctx.end_menu()
                    if ctx.begin_menu("灯光 Light"):
                        self._show_create_light_menu(ctx, parent_id=parent_id_for_new)
                        ctx.end_menu()
                    if ctx.selectable("创建空对象  Create Empty", False, 0, 0, 0):
                        self._create_empty_object(parent_id=parent_id_for_new)
                
                # Add Delete menu if something is selected
                if self._selected_object_id != 0:
                    ctx.separator()
                    if ctx.selectable("删除选中对象 Delete Selected", False, 0, 0, 0):
                        self._delete_selected_object()
                
                ctx.end_popup()

            # UI Mode: show create Canvas button at bottom
            if self._ui_mode:
                ctx.separator()
                ctx.button("+ 创建 Canvas  Create Canvas", self._create_ui_canvas, height=28)
        
        ctx.end_window()

    def _delete_selected_object(self) -> None:
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if scene:
            selected = scene.find_by_id(self._selected_object_id)
            if selected:
                self._delete_object(selected)

    def _reparent_to_root(self, dragged_id: int) -> None:
        """Reparent a GameObject to root (no parent)."""
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return

        dragged_obj = scene.find_by_id(dragged_id)
        if dragged_obj:
            old_parent = dragged_obj.get_parent()
            old_parent_id = old_parent.id if old_parent else None
            if old_parent_id is not None:  # only if actually has a parent
                self._execute_reparent(dragged_id, old_parent_id, None)
    
    def _show_create_primitive_menu(self, ctx: InfGUIContext, parent_id: int = None) -> None:
        """Show the Create 3D Object submenu."""
        from InfEngine.lib import SceneManager, PrimitiveType
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            ctx.label("(无场景)")
            return

        primitives = [
            ("立方体 Cube", PrimitiveType.Cube),
            ("球体 Sphere", PrimitiveType.Sphere),
            ("胶囊体 Capsule", PrimitiveType.Capsule),
            ("圆柱体 Cylinder", PrimitiveType.Cylinder),
            ("平面 Plane", PrimitiveType.Plane),
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
                    self._selected_object_id = new_obj.id
                    self._record_create(new_obj.id, f"Create {name.split()[0]}")
                    # Notify Inspector about the new selection
                    self._notify_selection_changed()
    
    def _show_create_light_menu(self, ctx: InfGUIContext, parent_id: int = None) -> None:
        """Show the Create Light submenu."""
        from InfEngine.lib import SceneManager, LightType, LightShadows, vec3f
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            ctx.label("(无场景)")
            return

        light_types = [
            ("平行光 Directional Light", LightType.Directional),
            ("点光源 Point Light", LightType.Point),
            ("聚光灯 Spot Light", LightType.Spot),
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
                                trans.euler_angles = vec3f(50.0, -30.0, 0.0)
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
                    self._selected_object_id = new_obj.id
                    self._record_create(new_obj.id, f"Create {name.split()[0]}")
                    self._notify_selection_changed()

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
                self._selected_object_id = new_obj.id
                self._record_create(new_obj.id, "Create Empty")
                # Notify Inspector about the new selection
                self._notify_selection_changed()
    
    def get_selected_object(self):
        """Get the currently selected GameObject, or None."""
        if self._selected_object_id == 0:
            return None
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if scene:
            return scene.find_by_id(self._selected_object_id)
        return None

    # ------------------------------------------------------------------
    # UI Mode helpers
    # ------------------------------------------------------------------

    def _filter_canvas_roots(self, root_objects):
        """Return only root GameObjects that have a UICanvas component (or ancestor of one)."""
        from InfEngine.ui import UICanvas
        result = []
        for go in root_objects:
            if self._has_canvas_descendant(go):
                result.append(go)
        return result

    def _has_canvas_descendant(self, go) -> bool:
        from InfEngine.ui import UICanvas
        for comp in go.get_py_components():
            if isinstance(comp, UICanvas):
                return True
        for child in go.get_children():
            if self._has_canvas_descendant(child):
                return True
        return False

    def _show_ui_mode_context_menu(self, ctx: InfGUIContext, parent_id: int = None):
        """Show right-click context menu in UI Mode (Canvas/Text creation only)."""
        from InfEngine.lib import SceneManager
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            ctx.label("(无场景)")
            return

        if ctx.selectable("Canvas", False, 0, 0, 0):
            self._create_ui_canvas(parent_id=parent_id)
        if ctx.selectable("T 文本 Text", False, 0, 0, 0):
            self._create_ui_text(parent_id=parent_id)

    def _create_ui_canvas(self, parent_id: int = None):
        """Create a Canvas GameObject with UICanvas component."""
        from InfEngine.lib import SceneManager
        from InfEngine.ui import UICanvas as UICanvasCls
        scene = SceneManager.instance().get_active_scene()
        if not scene:
            return
        go = scene.create_game_object("Canvas")
        if go:
            go.add_py_component(UICanvasCls())
            if parent_id is not None:
                parent = scene.find_by_id(parent_id)
                if parent:
                    go.set_parent(parent)
                    self._pending_expand_id = parent_id
            self._selected_object_id = go.id
            self._record_create(go.id, "Create Canvas")
            self._notify_selection_changed()

    def _create_ui_text(self, parent_id: int = None):
        """Create a Text GameObject with UIText component under a Canvas."""
        from InfEngine.lib import SceneManager
        from InfEngine.ui import UIText as UITextCls, UICanvas
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
            self._selected_object_id = go.id
            self._record_create(go.id, "Create Text")
            self._notify_selection_changed()
