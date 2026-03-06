"""
Unity-style Inspector panel with properties and raw data preview.

Component rendering helpers live in ``inspector_components``, and shared
layout helpers in ``inspector_utils``.

All asset inspectors (texture, audio, shader, material) are driven by the
unified ``asset_inspector`` module.  Material body rendering is delegated
to ``inspector_material``.
"""

import os
from enum import Enum, auto
from InfEngine.lib import InfGUIContext, TextureLoader
from InfEngine.components.component import InfComponent
from InfEngine.resources import component_icons_dir
from InfEngine.core.asset_types import asset_category_from_extension
from .closable_panel import ClosablePanel
from .theme import Theme, ImGuiCol, ImGuiStyleVar
from .inspector_utils import max_label_w, field_label, LABEL_PAD
from . import inspector_components as comp_ui
from .inspector_components import _notify_scene_modified, _record_property, _record_add_component
from .asset_inspector import render_asset_inspector, invalidate as invalidate_asset_inspector
from .object_execution_layer import ObjectExecutionLayer


class InspectorMode(Enum):
    """Inspector display mode — mutually exclusive."""
    OBJECT = auto()    # GameObject selected from Hierarchy
    ASSET = auto()     # Asset file selected from Project panel
    PREVIEW = auto()   # Non-editable file preview


class InspectorPanel(ClosablePanel):
    """
    Unity-style Inspector panel with two modules:
    1. Properties module (top) - displays object properties (Transform, etc.)
    2. Raw Data module (bottom) - displays file content preview using backend ResourcePreviewManager
    
    The backend ResourcePreviewManager handles all file type detection and preview rendering.
    When new previewers (e.g., Material, Model) are added to the backend, they automatically
    work here without any frontend changes.
    
    A splitter bar controls the ratio between the two modules.
    """
    
    WINDOW_TYPE_ID = "inspector"
    WINDOW_DISPLAY_NAME = "检视器 Inspector"
    
    # Minimum heights for splitter
    MIN_PROPERTIES_HEIGHT = Theme.INSPECTOR_MIN_PROPS_H
    MIN_RAW_DATA_HEIGHT = Theme.INSPECTOR_MIN_RAWDATA_H
    SPLITTER_HEIGHT = Theme.INSPECTOR_SPLITTER_H
    
    def __init__(self, title: str = "检视器 Inspector", engine=None):
        super().__init__(title, window_id="inspector")
        self.__engine = None
        self.__preview_manager = None  # ResourcePreviewManager from backend
        self.__asset_database = None
        self.__selected_object = None
        self.__selected_object_id = 0
        self.__selected_file = None
        self.__current_loaded_file = None
        self.__right_click_remove_enabled = True
        # Ratio of properties height to total available height (properties on top)
        self.__properties_ratio = Theme.INSPECTOR_DEFAULT_RATIO

        # Inspector mode state
        self.__inspector_mode = InspectorMode.OBJECT
        self.__asset_category: str = ""  # "material" | "texture" | "shader" | ""
        
        self.__add_component_search = ""  # Search text for Add Component popup
        self.__add_component_scripts = []  # Cached list of (display_name, path)
        self.__add_component_native_types = []  # Cached list of native type names
        self.__object_exec = ObjectExecutionLayer()

        # Component icon cache
        self.__comp_icon_cache: dict[str, int] = {}  # type_name_lower -> imgui tex id
        self.__comp_icons_loaded = False

        # Register MeshRenderer into the component renderer registry so
        # dispatch is fully unified (uses bound method for panel-level state).
        comp_ui.register_component_renderer("MeshRenderer", self._render_mesh_renderer)

        # Initialize engine if provided
        if engine:
            self.set_engine(engine)
    
    def set_engine(self, engine):
        """Set the engine instance for resource preview."""
        self.__engine = engine
        if engine:
            # Check if it's a Python Engine wrapper or native InfEngine
            if hasattr(engine, 'get_resource_preview_manager'):
                self.__preview_manager = engine.get_resource_preview_manager()
                if hasattr(engine, 'get_asset_database'):
                    self.__asset_database = engine.get_asset_database()
            elif hasattr(engine, 'get_native_engine'):
                # It's the Python Engine class, get native and then preview manager
                native = engine.get_native_engine()
                if native and hasattr(native, 'get_resource_preview_manager'):
                    self.__preview_manager = native.get_resource_preview_manager()
                if native and hasattr(native, 'get_asset_database'):
                    self.__asset_database = native.get_asset_database()
    
    # ---- Component icon helpers ----

    def _load_component_icons(self, native):
        """Lazily load component icon PNGs from resources/icons/components/."""
        if self.__comp_icons_loaded:
            return
        if not os.path.isdir(component_icons_dir):
            self.__comp_icons_loaded = True
            return
        for fname in os.listdir(component_icons_dir):
            if not fname.startswith("component_") or not fname.endswith(".png"):
                continue
            key = fname[len("component_"):-len(".png")]  # e.g. "camera"
            tex_name = f"__compicon__{key}"
            if native.has_imgui_texture(tex_name):
                self.__comp_icon_cache[key] = native.get_imgui_texture_id(tex_name)
                continue
            icon_path = os.path.join(component_icons_dir, fname)
            tex_data = TextureLoader.load_from_file(icon_path)
            if tex_data and tex_data.is_valid():
                pixels = tex_data.get_pixels_list()
                tid = native.upload_texture_for_imgui(
                    tex_name, pixels, tex_data.width, tex_data.height)
                if tid != 0:
                    self.__comp_icon_cache[key] = tid
        self.__comp_icons_loaded = True

    def _load_custom_icon(self, icon_path: str, type_name: str) -> int:
        """Load a custom icon specified by the ``@icon`` decorator and return
        its ImGui texture id, or 0 on failure."""
        key = type_name.lower()
        if key in self.__comp_icon_cache:
            return self.__comp_icon_cache[key]
        native = self._get_native_engine()
        if not native:
            return 0
        # Resolve project-relative paths
        if not os.path.isabs(icon_path):
            from InfEngine.engine.project_context import get_project_root
            root = get_project_root()
            if root:
                icon_path = os.path.join(root, icon_path)
        if not os.path.isfile(icon_path):
            self.__comp_icon_cache[key] = 0  # cache miss
            return 0
        tex_name = f"__compicon__{key}"
        if native.has_imgui_texture(tex_name):
            tid = native.get_imgui_texture_id(tex_name)
            self.__comp_icon_cache[key] = tid
            return tid
        tex_data = TextureLoader.load_from_file(icon_path)
        if tex_data and tex_data.is_valid():
            pixels = tex_data.get_pixels_list()
            tid = native.upload_texture_for_imgui(
                tex_name, pixels, tex_data.width, tex_data.height)
            if tid != 0:
                self.__comp_icon_cache[key] = tid
                return tid
        self.__comp_icon_cache[key] = 0
        return 0

    def _get_component_icon_id(self, type_name: str, is_script: bool = False) -> int:
        """Return ImGui texture id for a component icon, or 0 if unavailable.

        For script components, falls back to the generic ``component_script.png``
        icon when no component-specific icon is found.
        """
        tid = self.__comp_icon_cache.get(type_name.lower(), 0)
        if tid == 0 and is_script:
            tid = self.__comp_icon_cache.get("script", 0)
        return tid

    def _render_component_header_icon(self, ctx, type_name: str,
                                       is_script: bool = False, py_comp=None):
        """Draw the component icon inline before a collapsing header.

        Call this *before* ``collapsing_header`` — it renders a 16×16 image
        and uses ``same_line`` so the header appears right after the icon.

        For Python script components, the ``@icon("path")`` decorator is
        checked first.  If no decorator icon is found, falls back to the
        bundled ``component_<name>.png``, then to ``component_script.png``.
        """
        icon_id = 0
        # 1) Check @icon decorator on the Python component class
        if py_comp is not None:
            custom_path = getattr(py_comp.__class__, '_component_icon_', None)
            if custom_path:
                icon_id = self._load_custom_icon(custom_path, type_name)
        # 2) Fall back to bundled icon cache
        if icon_id == 0:
            icon_id = self._get_component_icon_id(type_name, is_script)
        if icon_id == 0:
            return
        ctx.image(icon_id, Theme.COMPONENT_ICON_SIZE, Theme.COMPONENT_ICON_SIZE)
        ctx.same_line()

    def set_selected_object(self, obj):
        """Set the selected scene object for properties display."""
        self.__selected_object = obj
        self.__selected_object_id = obj.id if obj is not None else 0
        # Clear file selection when object is selected
        if obj is not None:
            self.__selected_file = None
            self.__current_loaded_file = None
            self.__inspector_mode = InspectorMode.OBJECT
            self.__asset_category = ""

    def _get_selected_object(self):
        """Resolve selected object by ID to avoid stale pointers after scene reload."""
        return self.__object_exec.resolve_selected_object(self.__selected_object_id)
    
    def set_selected_file(self, file_path: str):
        """Set the selected file for raw data display."""
        if file_path != self.__selected_file:
            self.__selected_file = file_path
            self.__current_loaded_file = None
            # Invalidate unified asset inspector state on file change
            invalidate_asset_inspector()
        # Determine inspector mode & asset category
        if file_path:
            ext = os.path.splitext(file_path)[1].lower()
            cat = asset_category_from_extension(ext)
            if cat:
                self.__inspector_mode = InspectorMode.ASSET
                self.__asset_category = cat
            else:
                self.__inspector_mode = InspectorMode.PREVIEW
                self.__asset_category = ""
            # Clear object selection when file is selected
            self.__selected_object = None
            self.__selected_object_id = 0
        else:
            self.__inspector_mode = InspectorMode.OBJECT
            self.__asset_category = ""
    
    def set_detail_file(self, file_path: str):
        """Open an asset file in the detail (raw-data) module while keeping
        the current object selection, triggering a split view.

        Unlike ``set_selected_file`` (used by the Project panel), this does
        **not** clear the hierarchy selection — the properties module keeps
        showing the current object while the bottom half shows the asset
        editor.
        """
        if file_path != self.__selected_file:
            self.__selected_file = file_path
            self.__current_loaded_file = None
            invalidate_asset_inspector()
        if file_path:
            ext = os.path.splitext(file_path)[1].lower()
            cat = asset_category_from_extension(ext)
            if cat:
                self.__inspector_mode = InspectorMode.ASSET
                self.__asset_category = cat
            else:
                self.__inspector_mode = InspectorMode.PREVIEW
                self.__asset_category = ""
        else:
            self.__inspector_mode = InspectorMode.OBJECT
            self.__asset_category = ""

    def _get_file_extension(self, file_path: str) -> str:
        """Get lowercase file extension with dot."""
        if not file_path:
            return ""
        _, ext = os.path.splitext(file_path)
        return ext.lower()
    
    def _can_preview_file(self, file_path: str) -> bool:
        """Check if the backend has a previewer for this file type."""
        if not self.__preview_manager or not file_path:
            return False
        ext = self._get_file_extension(file_path)
        return self.__preview_manager.has_previewer(ext)
    
    def _load_preview(self, file_path: str) -> bool:
        """Load file for preview using backend ResourcePreviewManager."""
        if not self.__preview_manager or not file_path:
            return False
        
        # Skip if already loaded
        if self.__current_loaded_file == file_path and self.__preview_manager.is_preview_loaded():
            return True
        
        # Load the file
        if self.__preview_manager.load_preview(file_path):
            self.__current_loaded_file = file_path
            return True
        return False
    
    def _render_raw_data_module(self, ctx: InfGUIContext, height: float):
        """Render the Raw Data module - shows asset editor, file preview, or material details."""
        child_visible = ctx.begin_child("RawDataModule", 0, height, True)
        if child_visible:
            # Asset mode — route to dedicated asset inspectors
            if self.__selected_file and self.__inspector_mode == InspectorMode.ASSET:
                self._render_asset_inspector(ctx)
            # Priority 3: Preview mode — generic file preview
            elif self.__selected_file:
                self._render_file_preview(ctx)
            else:
                ctx.label("No selection")
                ctx.label("Select a file in the Project panel to preview")
        ctx.end_child()

    def _render_asset_inspector(self, ctx: InfGUIContext):
        """Delegate to the unified asset inspector."""
        fp = self.__selected_file
        cat = self.__asset_category
        if cat:
            render_asset_inspector(ctx, self, fp, cat)
        else:
            self._render_file_preview(ctx)
    
    def _render_file_preview(self, ctx: InfGUIContext):
        """Render file preview using backend ResourcePreviewManager."""
        if os.path.isdir(self.__selected_file):
            folder_name = os.path.basename(self.__selected_file)
            ctx.label(f"Folder: {folder_name}")
            ctx.separator()
            ctx.label(f"Path: {self.__selected_file}")
        elif not self.__preview_manager:
            filename = os.path.basename(self.__selected_file)
            ctx.label(f"File: {filename}")
            ctx.separator()
            ctx.label("(Preview system not initialized)")
        elif not self._can_preview_file(self.__selected_file):
            filename = os.path.basename(self.__selected_file)
            ctx.label(f"File: {filename}")
            ctx.separator()
            ctx.label("(No previewer available for this file type)")
            ext = self._get_file_extension(self.__selected_file)
            ctx.label(f"Extension: {ext}")
        elif not self._load_preview(self.__selected_file):
            filename = os.path.basename(self.__selected_file)
            ctx.label(f"File: {filename}")
            ctx.separator()
            ctx.label("(Failed to load preview)")
        else:
            # Render metadata from backend
            self.__preview_manager.render_metadata(ctx)
            ctx.separator()
            
            # Get remaining space for preview content
            avail_width = ctx.get_content_region_avail_width()
            avail_height = ctx.get_content_region_avail_height()
            
            # Render the actual preview (image, text, model, etc.)
            if avail_width > 0 and avail_height > 0:
                self.__preview_manager.render_preview(ctx, avail_width, avail_height)
    
    def _get_native_engine(self):
        """Get the native C++ InfEngine instance."""
        if self.__engine:
            if hasattr(self.__engine, 'get_native_engine'):
                return self.__engine.get_native_engine()
            elif hasattr(self.__engine, 'refresh_material_pipeline'):
                return self.__engine
        return None

    @staticmethod
    def _ensure_material_file_path(material) -> str:
        """Ensure *material* has a ``file_path``; assign a default one if needed.

        Returns the resolved file path, or ``""`` on failure.
        """
        if getattr(material, 'file_path', ''):
            return material.file_path
        from InfEngine.engine.project_context import get_project_root
        project_root = get_project_root()
        if not project_root:
            return ""
        materials_dir = os.path.join(project_root, "materials")
        os.makedirs(materials_dir, exist_ok=True)
        mat_name = getattr(material, 'name', 'DefaultUnlit')
        if mat_name == "DefaultLit":
            mat_file = os.path.join(materials_dir, "default_lit.mat")
        elif mat_name == "DefaultUnlit":
            mat_file = os.path.join(materials_dir, "default_unlit.mat")
        else:
            import re as _re
            file_name = _re.sub(r'([A-Z])', r'_\1', mat_name).lower().strip('_') + ".mat"
            mat_file = os.path.join(materials_dir, file_name)
        material.file_path = mat_file
        return mat_file

    # ------------------------------------------------------------------
    # Layout helpers — delegates to inspector_utils
    # ------------------------------------------------------------------
    _max_label_w = staticmethod(max_label_w)
    _field_label = staticmethod(field_label)

    # ------------------------------------------------------------------
    # Tag & Layer rendering
    # ------------------------------------------------------------------
    def _render_tag_layer_row(self, ctx: InfGUIContext, obj):
        """Render tag and layer dropdowns for a GameObject, on a single row."""
        from InfEngine.lib import TagLayerManager
        mgr = TagLayerManager.instance()

        all_tags = list(mgr.get_all_tags())
        current_tag = obj.tag if hasattr(obj, 'tag') else "Untagged"
        tag_idx = all_tags.index(current_tag) if current_tag in all_tags else 0

        all_layers = list(mgr.get_all_layers())
        current_layer = obj.layer if hasattr(obj, 'layer') else 0
        # Build display labels: "0: Default", "3: (empty)", etc.
        layer_labels = []
        for i, name in enumerate(all_layers):
            if name:
                layer_labels.append(f"{i}: {name}")
            else:
                layer_labels.append(f"{i}: ---")

        # Append "Add Tag..." / "Add Layer..." at the end of each combo
        tag_items = all_tags + ["Add Tag..."]
        layer_items = layer_labels + ["Add Layer..."]

        # Layout: Tag [combo▼]   Layer [combo▼]
        # Each label is rendered, then the combo is placed right after it with a small gap.
        # A fixed mid-point keeps the two columns aligned.
        avail_w = ctx.get_content_region_avail_width()
        half_w = avail_w * 0.5 - 4

        # --- Tag (left column) ---
        ctx.label("Tag")
        ctx.same_line(0, 4)
        ctx.set_next_item_width(half_w - 30)   # 30 ≈ "Tag" text + 4 gap
        new_tag_idx = ctx.combo("##Tag", tag_idx, tag_items, 10)
        if new_tag_idx != tag_idx:
            if new_tag_idx == len(all_tags):
                # "Add Tag..." selected — open Tag & Layer settings panel
                if self._window_manager:
                    self._window_manager.open_window("tag_layer_settings")
            elif 0 <= new_tag_idx < len(all_tags):
                _record_property(obj, "tag", all_tags[tag_idx], all_tags[new_tag_idx], "Set Tag")

        # --- Layer (right column) — start at fixed half-width mark ---
        ctx.same_line(half_w + 8)
        ctx.label("Layer")
        ctx.same_line(0, 4)
        ctx.set_next_item_width(-1)             # fill remaining width
        new_layer = ctx.combo("##Layer", current_layer, layer_items, 12)
        if new_layer != current_layer:
            if new_layer == len(layer_labels):
                # "Add Layer..." selected — open Tag & Layer settings panel
                if self._window_manager:
                    self._window_manager.open_window("tag_layer_settings")
            else:
                _record_property(obj, "layer", current_layer, new_layer, "Set Layer")

    # ------------------------------------------------------------------
    # Component rendering — delegates to inspector_components
    # ------------------------------------------------------------------
    def _render_transform_component(self, ctx: InfGUIContext, trans):
        comp_ui.render_transform_component(ctx, trans)

    def _render_mesh_renderer(self, ctx: InfGUIContext, renderer):
        """Render MeshRenderer component using direct C++ property access."""
        lw = self._max_label_w(ctx, ["Mesh", "Materials", "Element 0"])

        # ── Mesh field ─────────────────────────────────────────────────
        self._field_label(ctx, "Mesh", lw)
        mesh_display = "(Primitive)" if renderer.has_inline_mesh() else "None"
        self._render_object_field(ctx, "mesh_field", mesh_display, "Mesh", clickable=False)

        ctx.separator()

        # ── Material ───────────────────────────────────────────────────
        self._field_label(ctx, "Materials", lw)
        ctx.label("Size: 1")

        mat = renderer.get_effective_material()
        mat_name = getattr(mat, 'name', 'None') if mat else 'None'
        is_default = not renderer.has_render_material()
        display_name = f"{mat_name}" + (" (Default)" if is_default else "")

        mat_file = getattr(mat, 'file_path', '') if mat else ''
        is_selected = bool(mat_file) and self.__selected_file == mat_file

        def on_material_drop(mat_path):
            self._apply_dropped_material(renderer, mat_path)

        self._field_label(ctx, "Element 0", lw)
        if self._render_object_field(ctx, "mat_0", display_name, "Material",
                                      selected=is_selected, clickable=True,
                                      accept_drag_type="MATERIAL_FILE",
                                      on_drop_callback=on_material_drop):
            if mat:
                fp = self._ensure_material_file_path(mat)
                if fp:
                    mat.save()
                    self.set_detail_file(fp)

        ctx.separator()

        # ── Shadow settings (direct property read/write) ──────────────
        casts = renderer.casts_shadows
        new_casts = ctx.checkbox("Cast Shadows", casts)
        if new_casts != casts:
            _record_property(renderer, "casts_shadows", casts, new_casts, "Set Cast Shadows")

        receives = renderer.receives_shadows
        new_receives = ctx.checkbox("Receive Shadows", receives)
        if new_receives != receives:
            _record_property(renderer, "receives_shadows", receives, new_receives, "Set Receive Shadows")

    
    def _render_object_field(self, ctx: InfGUIContext, field_id: str, display_text: str,
                             type_hint: str, selected: bool = False, clickable: bool = True,
                             accept_drag_type: str = None, on_drop_callback=None) -> bool:
        return comp_ui.render_object_field(ctx, field_id, display_text, type_hint, selected,
                                           clickable, accept_drag_type, on_drop_callback)

    def _apply_dropped_material(self, renderer, mat_path: str):
        """Apply a dropped material file to the MeshRenderer."""
        from InfEngine.lib import MaterialManager
        from InfEngine.debug import Debug

        mat_manager = MaterialManager.instance()
        material = mat_manager.load_material(mat_path)
        if material:
            renderer.render_material = material
            Debug.log_internal(f"Applied material from: {mat_path}")
            self.set_detail_file(mat_path)
        else:
            Debug.log_warning(f"Failed to load material from: {mat_path}")

    def _render_cpp_component_generic(self, ctx: InfGUIContext, comp):
        comp_ui.render_cpp_component_generic(ctx, comp)
    
    def _open_add_component_popup(self, ctx: InfGUIContext):
        """Open the Add Component popup and refresh script list."""
        self.__add_component_search = ""
        self.__add_component_scripts = self._scan_project_scripts()
        self.__add_component_native_types = self._get_native_component_types()
        # Pre-cache menu paths once (avoids exec_module per frame)
        self.__script_menu_paths: dict[str, str | None] = {}
        for _, path in self.__add_component_scripts:
            self.__script_menu_paths[path] = self._get_script_menu_path(path)
        ctx.open_popup("##add_component_popup")

    def _get_native_component_types(self):
        """Get list of available native (C++) component type names."""
        from InfEngine.lib import get_registered_component_types
        types = get_registered_component_types()
        # Filter out Transform (always present)
        return [t for t in sorted(types) if t != "Transform"]

    def _scan_project_scripts(self):
        """Scan project root for .py files containing InfComponent subclasses."""
        results = []
        from InfEngine.engine.project_context import get_project_root
        project_root = get_project_root()
        if not project_root or not os.path.isdir(project_root):
            return results

        for dirpath, _dirnames, filenames in os.walk(project_root):
            # Skip hidden dirs, __pycache__, etc.
            rel = os.path.relpath(dirpath, project_root)
            if any(part.startswith('.') or part == '__pycache__' for part in rel.split(os.sep)):
                continue
            for fn in filenames:
                if not fn.endswith('.py') or fn.startswith('_'):
                    continue
                full = os.path.join(dirpath, fn)
                # Quick check: file must reference InfComponent
                with open(full, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read(4096)
                if 'InfComponent' in content:
                    display = fn[:-3]  # strip .py
                    results.append((display, full))
        results.sort(key=lambda x: x[0].lower())
        return results

    def _get_script_menu_path(self, script_path: str) -> str | None:
        """Return the ``@add_component_menu`` path for a script, or None.

        Loads the script module to inspect the class attribute.  Results are
        cached implicitly because ``_scan_project_scripts`` only runs on popup
        open.
        """
        import importlib.util
        spec = importlib.util.spec_from_file_location("_tmp_scan", script_path)
        if not spec or not spec.loader:
            return None
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        from InfEngine.components.component import InfComponent
        for attr in dir(mod):
            obj = getattr(mod, attr, None)
            if (isinstance(obj, type) and issubclass(obj, InfComponent)
                    and obj is not InfComponent):
                return getattr(obj, '_component_menu_path_', None)
        return None

    def _render_add_component_popup(self, ctx: InfGUIContext):
        """Render the searchable Add Component popup content."""
        # Styled padding
        ctx.push_style_var_vec2(ImGuiStyleVar.WindowPadding, *Theme.POPUP_ADD_COMP_PAD)
        ctx.push_style_var_vec2(ImGuiStyleVar.ItemSpacing, *Theme.POPUP_ADD_COMP_SPC)
        
        # Search field
        ctx.set_next_item_width(Theme.ADD_COMP_SEARCH_W)
        new_text = ctx.input_text_with_hint("##comp_search", "Search components...",
                                            self.__add_component_search)
        if isinstance(new_text, str):
            self.__add_component_search = new_text
        
        ctx.separator()
        
        search = self.__add_component_search.lower().strip()
        found_any = False
        
        # --- Native (C++) components grouped by category ---
        # Dynamically read _component_category_ from each wrapper class
        from InfEngine.components.builtin_component import BuiltinComponent
        native_types = getattr(self, '_InspectorPanel__add_component_native_types', [])
        native_matched = [t for t in native_types if not search or search in t.lower()]
        if native_matched:
            # Bucket matched types into categories via _component_category_
            cat_items: dict[str, list[str]] = {}
            uncategorized_native: list[str] = []
            for t in native_matched:
                wrapper_cls = BuiltinComponent._builtin_registry.get(t)
                cat = getattr(wrapper_cls, '_component_category_', '') if wrapper_cls else ''
                if cat:
                    cat_items.setdefault(cat, []).append(t)
                else:
                    uncategorized_native.append(t)

            # Render each category in stable sorted order
            for cat in sorted(cat_items.keys()):
                items = cat_items[cat]
                ctx.label(cat)
                ctx.separator()
                for type_name in items:
                    found_any = True
                    if ctx.selectable(f"  {type_name}"):
                        self._add_native_component(type_name)
                        ctx.close_current_popup()
                ctx.dummy(0, 4)

            # Any remaining native types not in a known category
            if uncategorized_native:
                ctx.label("Miscellaneous")
                ctx.separator()
                for type_name in uncategorized_native:
                    found_any = True
                    if ctx.selectable(f"  {type_name}"):
                        self._add_native_component(type_name)
                        ctx.close_current_popup()
                ctx.dummy(0, 4)
        
        # --- Engine Python components (Rendering) ---
        engine_py_components = self._get_engine_py_components()
        engine_matched = [(n, c) for n, c in engine_py_components
                          if not search or search in n.lower()]
        if engine_matched:
            # Group engine Python components by their _component_category_
            engine_cats: dict[str, list] = {}
            for comp_name, comp_cls in engine_matched:
                cat = getattr(comp_cls, '_component_category_', '') or 'Miscellaneous'
                engine_cats.setdefault(cat, []).append((comp_name, comp_cls))
            for cat in sorted(engine_cats.keys()):
                # Merge into the same category label as native components if it
                # hasn't been drawn yet; otherwise draw a new header.
                ctx.label(cat)
                ctx.separator()
                for comp_name, comp_cls in engine_cats[cat]:
                    found_any = True
                    if ctx.selectable(f"  {comp_name}"):
                        self._add_engine_py_component(comp_cls)
                        ctx.close_current_popup()
                ctx.dummy(0, 4)

        # --- Script components (grouped by @add_component_menu categories) ---
        script_matched = [(d, p) for d, p in self.__add_component_scripts
                          if not search or search in d.lower()]
        if script_matched:
            # Build category tree from @add_component_menu paths
            categorized: dict[str, list] = {}   # category -> [(display, path)]
            uncategorized: list = []
            for display_name, script_path in script_matched:
                menu_path = self.__script_menu_paths.get(script_path)
                if menu_path:
                    # Use the first path segment as the category header
                    parts = menu_path.split('/')
                    category = parts[0]
                    leaf_name = parts[-1] if len(parts) > 1 else display_name
                    categorized.setdefault(category, []).append((leaf_name, script_path))
                else:
                    uncategorized.append((display_name, script_path))

            # Render categorized scripts
            for cat in sorted(categorized.keys()):
                ctx.label(cat)
                ctx.separator()
                for leaf_name, spath in categorized[cat]:
                    found_any = True
                    if ctx.selectable(f"  {leaf_name}"):
                        self._handle_script_drop(spath)
                        ctx.close_current_popup()
                ctx.dummy(0, 4)

            # Render uncategorized scripts
            if uncategorized:
                ctx.label("Scripts")
                ctx.separator()
                for display_name, spath in uncategorized:
                    found_any = True
                    if ctx.selectable(f"  {display_name}"):
                        self._handle_script_drop(spath)
                        ctx.close_current_popup()
        
        if not found_any:
            ctx.label("No components found")
        
        ctx.pop_style_var(2)

    def _add_native_component(self, type_name: str):
        """Add a built-in (C++) component to the selected object."""
        selected_object = self._get_selected_object()
        if not selected_object:
            return
        result = selected_object.add_component(type_name)
        if result is not None:
            from InfEngine.debug import Debug
            Debug.log_internal(f"Added component: {type_name}")
            _record_add_component(selected_object, type_name, result, is_py=False)
        else:
            from InfEngine.debug import Debug
            Debug.log_error(f"Failed to add component: {type_name}")

    @staticmethod
    def _get_engine_py_components():
        """Return a list of (display_name, class) for engine-level Python
        components that should appear in the Add Component popup."""
        result = []
        from InfEngine.renderstack.render_stack import RenderStack
        result.append(("RenderStack", RenderStack))
        return result

    def _add_engine_py_component(self, comp_cls):
        """Instantiate and attach an engine-level Python component."""
        selected_object = self._get_selected_object()
        if not selected_object:
            return
        from InfEngine.debug import Debug
        # Enforce singleton for @disallow_multiple (RenderStack uses class singleton)
        if getattr(comp_cls, '_disallow_multiple_', False):
            for pc in selected_object.get_py_components():
                if isinstance(pc, comp_cls):
                    Debug.log_warning(
                        f"Cannot add another '{comp_cls.__name__}' — "
                        f"only one per scene is allowed")
                    return
        instance = comp_cls()
        # Resolve script GUID for engine Python components so they
        # survive save/load round-trips (same logic as script drops).
        if self.__asset_database:
            import inspect as _inspect
            src_file = _inspect.getfile(comp_cls)
            if src_file:
                guid = self.__asset_database.get_guid_from_path(src_file)
                if not guid:
                    guid = self.__asset_database.import_asset(src_file)
                if guid:
                    instance._script_guid = guid
        selected_object.add_py_component(instance)
        Debug.log_internal(f"Added component: {comp_cls.__name__}")
        _record_add_component(selected_object, comp_cls.__name__, instance, is_py=True)

    def _handle_script_drop(self, script_path: str):
        """Handle script file drop - load and attach component."""
        selected_object = self._get_selected_object()
        if not selected_object:
            return
        
        from InfEngine.components import load_and_create_component
        from InfEngine.debug import Debug

        # Load component from script file
        component_instance = load_and_create_component(script_path, asset_database=self.__asset_database)

        # --- Enforce @disallow_multiple ---
        comp_cls = component_instance.__class__
        if getattr(comp_cls, '_disallow_multiple_', False):
            existing = selected_object.get_py_components()
            for ec in existing:
                if type(ec).__name__ == comp_cls.__name__:
                    Debug.log_warning(
                        f"Cannot add another '{comp_cls.__name__}' — "
                        f"@disallow_multiple is set")
                    return

        # --- Enforce @require_component ---
        required = getattr(comp_cls, '_require_components_', [])
        for req_type in required:
            req_name = req_type if isinstance(req_type, str) else req_type.__name__
            # Check C++ components
            has_it = False
            if hasattr(selected_object, 'get_components'):
                for c in selected_object.get_components():
                    if c.type_name == req_name:
                        has_it = True
                        break
            # Check existing Python components
            if not has_it and hasattr(selected_object, 'get_py_components'):
                for pc in selected_object.get_py_components():
                    if pc.type_name == req_name:
                        has_it = True
                        break
            if not has_it:
                # Try to auto-add the required component
                if isinstance(req_type, str):
                    selected_object.add_component(req_type)
                    Debug.log_internal(
                        f"Auto-added required component '{req_name}'")
                else:
                    Debug.log_warning(
                        f"'{comp_cls.__name__}' requires '{req_name}' — "
                        f"please add it manually")

        # Track script path for reload
        if self.__asset_database:
            guid = self.__asset_database.get_guid_from_path(script_path)
            if not guid:
                guid = self.__asset_database.import_asset(script_path)
            component_instance._script_guid = guid

        # Attach to selected GameObject
        selected_object.add_py_component(component_instance)
        _record_add_component(selected_object, component_instance.type_name, component_instance, is_py=True)

        Debug.log_internal(f"Added component {component_instance.type_name} from {os.path.basename(script_path)}")


    def _render_py_component(self, ctx: InfGUIContext, py_comp):
        comp_ui.render_py_component(ctx, py_comp)

    def _render_properties_module(self, ctx: InfGUIContext, height: float):
        """Render the Properties module showing object properties (on top)."""
        # Lazily load component icons on first frame
        if not self.__comp_icons_loaded:
            native = self._get_native_engine()
            if native:
                self._load_component_icons(native)

        child_visible = ctx.begin_child("PropertiesModule", 0, height, True)
        if child_visible:
            selected_object = self._get_selected_object()
            if selected_object:
                ctx.push_id_str(f"selected_obj_{selected_object.id}")
                # Active checkbox (no label — matches Unity's checkbox-only style)
                is_active = selected_object.active
                new_active = ctx.checkbox("##obj_active", is_active)
                if new_active != is_active:
                    _record_property(selected_object, "active", is_active, new_active, "Set Active")

                ctx.same_line(0, 6)
                # Editable object name
                ctx.set_next_item_width(-1)
                old_name = selected_object.name
                new_name = ctx.text_input("##obj_name", old_name, 256)
                if new_name != old_name:
                    _record_property(selected_object, "name", old_name, new_name, "Rename")

                # --- Tag & Layer dropdowns ---
                self._render_tag_layer_row(ctx, selected_object)

                ctx.separator()

                # Check if any py_component hides Transform
                # (e.g. InfUIScreenComponent uses canvas-space coords)
                _hide_transform = False
                if hasattr(selected_object, 'get_py_components'):
                    for _pc in selected_object.get_py_components():
                        if getattr(type(_pc), '_hide_transform_', False):
                            _hide_transform = True
                            break

                # Transform (skip for screen-space UI elements)
                if not _hide_transform:
                    trans = selected_object.get_transform()
                    ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
                    self._render_component_header_icon(ctx, "Transform")
                    if ctx.collapsing_header("Transform"):
                        self._render_transform_component(ctx, trans)

                # C++ components (MeshRenderer, etc.)
                if hasattr(selected_object, 'get_components'):
                    components = selected_object.get_components()
                    for comp in components:
                        type_name = comp.type_name
                        if type_name == "Transform":
                            continue

                        # Check if this is a PyComponentProxy (Python component)
                        if hasattr(comp, 'get_py_component'):
                            # Skip - we handle Python components separately below
                            continue

                        comp_id = getattr(comp, "component_id", None)
                        if not comp_id:
                            comp_id = id(comp)
                        ctx.push_id_str(f"native_{type_name}_{comp_id}")
                        ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
                        self._render_component_header_icon(ctx, type_name)
                        ctx.set_next_item_allow_overlap()  # let the enabled checkbox receive clicks over the header
                        header_open = ctx.collapsing_header(type_name)
                        # Right-click context menu
                        if self.__right_click_remove_enabled and ctx.begin_popup_context_item("comp_ctx"):
                            if ctx.selectable("Remove"):
                                if hasattr(selected_object, 'remove_component'):
                                    blockers = []
                                    if hasattr(selected_object, 'get_remove_component_blockers'):
                                        try:
                                            blockers = list(selected_object.get_remove_component_blockers(comp) or [])
                                        except Exception:
                                            blockers = []
                                    can_remove = not blockers
                                    if can_remove and hasattr(selected_object, 'can_remove_component'):
                                        can_remove = selected_object.can_remove_component(comp)
                                    if can_remove and selected_object.remove_component(comp):
                                        _notify_scene_modified()
                                    else:
                                        from InfEngine.debug import Debug
                                        suffix = (
                                            f" required by: {', '.join(blockers)}"
                                            if blockers else
                                            "another component depends on it"
                                        )
                                        Debug.log_warning(
                                            f"Cannot remove '{type_name}' — "
                                            f"{suffix}")
                                ctx.end_popup()
                                continue
                            ctx.end_popup()

                        # Enabled checkbox — right-aligned on header line
                        is_enabled = comp.enabled
                        ctx.same_line(ctx.get_window_width() - Theme.COMP_ENABLED_CB_OFFSET)
                        new_enabled = ctx.checkbox("##comp_en", is_enabled)
                        if new_enabled != is_enabled:
                            _record_property(comp, "enabled", is_enabled, new_enabled, f"Toggle {type_name}")

                        if header_open:
                            comp_ui.render_component(ctx, comp)
                        ctx.pop_id()

                # Python components (InfComponent subclasses)
                if hasattr(selected_object, 'get_py_components'):
                    py_components = selected_object.get_py_components()
                    for py_comp in py_components:
                        type_name = py_comp.type_name
                        comp_id = getattr(py_comp, "component_id", None)
                        if not comp_id:
                            comp_id = id(py_comp)
                        ctx.push_id_str(f"py_comp_{type_name}_{comp_id}")
                        ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
                        self._render_component_header_icon(
                            ctx, type_name, is_script=True, py_comp=py_comp)
                        ctx.set_next_item_allow_overlap()  # let the enabled checkbox receive clicks over the header
                        header_open = ctx.collapsing_header(f"{type_name} (Script)")
                        # Right-click context menu
                        if self.__right_click_remove_enabled and ctx.begin_popup_context_item("py_comp_ctx"):
                            if ctx.selectable("Remove"):
                                if hasattr(selected_object, 'remove_py_component'):
                                    # Python components are wrapped in PyComponentProxy;
                                    # remove_py_component calls RemoveComponent on the proxy,
                                    # which already checks CanRemoveComponent in C++.
                                    if not selected_object.remove_py_component(py_comp):
                                        from InfEngine.debug import Debug
                                        Debug.log_warning(
                                            f"Cannot remove '{type_name}' — "
                                            f"another component depends on it")
                                    else:
                                        _notify_scene_modified()
                                ctx.end_popup()
                                continue
                            ctx.end_popup()

                        # Enabled checkbox — right-aligned on header line
                        is_enabled = py_comp.enabled
                        ctx.same_line(ctx.get_window_width() - Theme.COMP_ENABLED_CB_OFFSET)
                        new_enabled = ctx.checkbox("##pycomp_en", is_enabled)
                        if new_enabled != is_enabled:
                            _record_property(py_comp, "enabled", is_enabled, new_enabled, f"Toggle {type_name}")

                        if header_open:
                            # Render serialized fields
                            self._render_py_component(ctx, py_comp)
                        ctx.pop_id()

                # Add Component area
                ctx.separator()
                ctx.dummy(0, 6)

                # Full-width "Add Component" button  (width=-1 fills to end of content region)
                ctx.push_style_var_vec2(ImGuiStyleVar.FramePadding, *Theme.ADD_COMP_FRAME_PAD)
                ctx.button("Add Component", lambda: self._open_add_component_popup(ctx), -1, 0)
                ctx.pop_style_var(1)

                ctx.dummy(0, 6)

                # Add Component popup with search
                if ctx.begin_popup("##add_component_popup"):
                    self._render_add_component_popup(ctx)
                    ctx.end_popup()
                ctx.pop_id()
                
            else:
                ctx.label("No object selected")
        ctx.end_child()

        # Drag-drop target on the entire PropertiesModule child window.
        # Must be called AFTER end_child() — EndChild() submits the child as an item,
        # so BeginDragDropTarget() here applies to the whole child area.
        selected_object = self._get_selected_object()
        if selected_object is not None:
            Theme.push_drag_drop_target_style(ctx)  # 1 colour
            if ctx.begin_drag_drop_target():
                payload = ctx.accept_drag_drop_payload("SCRIPT_FILE")
                if payload is not None:
                    self._handle_script_drop(payload)
                ctx.end_drag_drop_target()
            ctx.pop_style_color(1)
    
    def _render_splitter(self, ctx: InfGUIContext, total_height: float) -> float:
        """Render a horizontal splitter bar. Returns the new properties height ratio."""
        # Draw a visible separator line first
        ctx.separator()
        
        # Splitter bar - use full width invisible button
        avail_width = ctx.get_content_region_avail_width()
        
        # Create an invisible button that spans the splitter area
        # The button ID must be unique
        ctx.invisible_button("##InspectorSplitter", avail_width, self.SPLITTER_HEIGHT)
        
        is_hovered = ctx.is_item_hovered()
        is_active = ctx.is_item_active()
        
        # Change mouse cursor to resize style when hovered or active
        # ImGuiMouseCursor_ResizeNS = 3 (vertical resize)
        if is_hovered or is_active:
            ctx.set_mouse_cursor(3)  # ResizeNS cursor
        
        # Handle drag - check if button is being dragged
        if is_active:
            delta_y = ctx.get_mouse_drag_delta_y(0)
            if abs(delta_y) > 1.0:  # Small threshold to avoid jitter
                # Calculate new ratio
                usable_height = total_height - self.SPLITTER_HEIGHT
                if usable_height > 0:
                    # Delta is how much to move the splitter down (increase properties height)
                    new_properties_height = self.__properties_ratio * usable_height + delta_y
                    new_ratio = new_properties_height / usable_height
                    
                    # Clamp to valid range
                    min_ratio = self.MIN_PROPERTIES_HEIGHT / usable_height
                    max_ratio = 1.0 - (self.MIN_RAW_DATA_HEIGHT / usable_height)
                    self.__properties_ratio = max(min_ratio, min(max_ratio, new_ratio))
                    
                ctx.reset_mouse_drag_delta(0)
        
        # Draw another separator for visual feedback
        ctx.separator()
        
        return self.__properties_ratio
    
    def on_render(self, ctx: InfGUIContext):
        if not self._is_open:
            return
            
        ctx.set_next_window_size(*Theme.INSPECTOR_INIT_SIZE, Theme.COND_FIRST_USE_EVER)
        if self._begin_closable_window(ctx, 0):
            # Get total available height
            total_height = ctx.get_content_region_avail_height()
            
            # Show split view if file is selected alongside an object
            has_detail_content = bool(self.__selected_file)

            # When a file is selected and no object is active, give full
            # height to the file view (asset editor or generic preview).
            file_only = self.__selected_file and not self._get_selected_object()

            if file_only:
                # Full-height file view (asset inspector or generic preview)
                self._render_raw_data_module(ctx, 0)
            elif has_detail_content and total_height > (self.MIN_PROPERTIES_HEIGHT + self.MIN_RAW_DATA_HEIGHT + self.SPLITTER_HEIGHT):
                # Calculate heights based on ratio
                usable_height = total_height - self.SPLITTER_HEIGHT
                properties_height = usable_height * self.__properties_ratio
                raw_data_height = usable_height - properties_height
                
                # Clamp to minimums
                if properties_height < self.MIN_PROPERTIES_HEIGHT:
                    properties_height = self.MIN_PROPERTIES_HEIGHT
                    raw_data_height = usable_height - properties_height
                if raw_data_height < self.MIN_RAW_DATA_HEIGHT:
                    raw_data_height = self.MIN_RAW_DATA_HEIGHT
                    properties_height = usable_height - raw_data_height
                
                # 1. Properties module (TOP)
                self._render_properties_module(ctx, properties_height)
                
                # 2. Splitter bar
                self._render_splitter(ctx, total_height)
                
                # 3. Raw Data module (BOTTOM) - shows file preview or material detail
                self._render_raw_data_module(ctx, raw_data_height)
            else:
                # No file/material selected or not enough space - just show properties
                self._render_properties_module(ctx, 0)  # 0 = fill all
            
        ctx.end_window()
