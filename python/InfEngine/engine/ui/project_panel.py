"""
Unity-style Project panel for browsing assets.

File operations and templates are in ``project_file_ops``.
Pure utilities are in ``project_utils``.
"""

import os
from InfEngine.lib import InfGUIContext, TextureLoader
from InfEngine.resources import file_type_icons_dir
from .closable_panel import ClosablePanel
from .theme import Theme, ImGuiCol, ImGuiStyleVar
from . import project_file_ops as file_ops
from . import project_utils


class ProjectPanel(ClosablePanel):
    """
    Unity-style Project panel for browsing assets.
    Left: Folder tree view
    Right: File grid/list view
    Supports: Create folders, create scripts, double-click to open
    """
    
    WINDOW_TYPE_ID = "project"
    WINDOW_DISPLAY_NAME = "项目 Project"
    
    # File extensions to hide
    HIDDEN_EXTENSIONS = {'.meta', '.pyc', '.pyo'}
    HIDDEN_PREFIXES = {'.', '__'}
    
    # Image extensions that support thumbnails
    IMAGE_EXTENSIONS = {'.png', '.jpg', '.jpeg', '.bmp', '.tga', '.gif'}
    
    # Extension -> icon filename (without .png) mapping
    # Icons are 80x80 PNGs located in resources/icons/
    ICON_MAP = {
        # Directories
        '__dir__':  'folder',
        # Scripts
        '.py':      'script_py',
        '.lua':     'script_lua',
        '.cs':      'script_cs',
        '.cpp':     'script_cpp',
        '.c':       'script_cpp',
        '.h':       'script_cpp',
        # Shaders — each type has its own icon
        '.vert':    'shader_vert',
        '.frag':    'shader_frag',
        '.glsl':    'shader_glsl',
        '.hlsl':    'shader_hlsl',
        # Materials
        '.mat':     'material',
        # Images
        '.png':     'image',
        '.jpg':     'image',
        '.jpeg':    'image',
        '.bmp':     'image',
        '.tga':     'image',
        '.gif':     'image',
        # 3D models
        '.fbx':     'model_3d',
        '.obj':     'model_3d',
        '.gltf':    'model_3d',
        '.glb':     'model_3d',
        # Audio
        '.wav':     'audio',
        '.mp3':     'audio',
        '.ogg':     'audio',
        # Fonts
        '.ttf':     'font',
        '.otf':     'font',
        # Text / docs
        '.txt':     'text',
        '.md':      'readme',
        # Config
        '.json':    'config',
        '.yaml':    'config',
        '.yml':     'config',
        '.xml':     'config',
        # Scene
        '.scene':   'scene',
    }
    
    # Key codes (ImGuiKey enum values)
    KEY_F2 = 573  # ImGuiKey_F2
    KEY_DELETE = 522  # ImGuiKey_Delete
    KEY_ENTER = 525  # ImGuiKey_Enter
    
    SCRIPT_TEMPLATE = file_ops.SCRIPT_TEMPLATE
    VERTEX_SHADER_TEMPLATE = file_ops.VERTEX_SHADER_TEMPLATE
    FRAGMENT_SHADER_TEMPLATE = file_ops.FRAGMENT_SHADER_TEMPLATE
    MATERIAL_TEMPLATE = file_ops.MATERIAL_TEMPLATE
    
    def __init__(self, root_path: str = "", title: str = "项目 Project", engine=None):
        super().__init__(title, window_id="project")
        self.__root_path = root_path
        # Default to Assets folder if it exists
        assets_path = os.path.join(root_path, "Assets") if root_path else ""
        if assets_path and os.path.exists(assets_path):
            self.__current_path = assets_path
        else:
            self.__current_path = root_path
        self.__selected_file = None
        self.__on_file_selected = None
        self.__on_file_double_click = None
        
        # Engine and file manager reference
        self.__engine = engine
        self.__file_manager = None
        self.__asset_database = None
        if engine:
            self.set_engine(engine)
        
        # Thumbnail cache: path -> (texture_id, last_modified_time)
        self.__thumbnail_cache = {}
        
        # File-type icon cache: icon_key (str) -> imgui texture id (int)
        self.__type_icon_cache = {}
        self.__type_icons_loaded = False
        
        # State for create dialogs
        self.__show_create_folder_popup = False
        self.__show_create_script_popup = False
        self.__new_item_name = ""
        self.__create_error = ""
        
        # Double-click detection state
        self.__last_clicked_file = None
        self.__last_click_time = 0.0

        # Rename state
        self.__renaming_path = None
        self.__renaming_name = ""
        self.__rename_focus_requested = False
        
        # Pending script creation (name first, then create)
        self.__pending_script_name = None  # When set, we show rename input for new script
        self.__pending_script_focus = False
        
        # Pending shader creation
        self.__pending_shader_name = None
        self.__pending_shader_focus = False
        self.__pending_shader_type = None  # 'vert' or 'frag'
        
        # Pending material creation
        self.__pending_material_name = None
        self.__pending_material_focus = False
        
        # Pending scene creation
        self.__pending_scene_name = None
        self.__pending_scene_focus = False
    
    def set_root_path(self, path: str):
        self.__root_path = path
        # Default to Assets folder
        assets_path = os.path.join(path, "Assets")
        if os.path.exists(assets_path):
            self.__current_path = assets_path
        else:
            self.__current_path = path
    
    def set_on_file_selected(self, callback):
        self.__on_file_selected = callback

    def clear_selection(self):
        """Clear current file selection and notify listeners."""
        if self.__selected_file is not None:
            self.__selected_file = None
            if self.__on_file_selected:
                self.__on_file_selected(None)
    
    def set_on_file_double_click(self, callback):
        self.__on_file_double_click = callback
    
    def set_engine(self, engine):
        """Set the engine instance for resource management."""
        self.__engine = engine
        print(f"[ProjectPanel] set_engine called with: {type(engine)}")
        if engine:
            # Try to get FileManager from engine
            if hasattr(engine, 'get_file_manager'):
                self.__file_manager = engine.get_file_manager()
            elif hasattr(engine, 'get_native_engine'):
                native = engine.get_native_engine()
                if native and hasattr(native, 'get_file_manager'):
                    self.__file_manager = native.get_file_manager()
            # Try to get AssetDatabase from engine
            if hasattr(engine, 'get_asset_database'):
                self.__asset_database = engine.get_asset_database()
            elif hasattr(engine, 'get_native_engine'):
                native = engine.get_native_engine()
                if native and hasattr(native, 'get_asset_database'):
                    self.__asset_database = native.get_asset_database()
    
    def _get_thumbnail(self, file_path: str, size: float) -> int:
        """Get or create a thumbnail texture ID for an image file. Returns 0 if not available."""
        if not file_path or not os.path.exists(file_path):
            return 0
        
        ext = os.path.splitext(file_path)[1].lower()
        if ext not in self.IMAGE_EXTENSIONS:
            return 0
        
        # Use a unique name for thumbnail to avoid conflict with Inspector preview
        thumbnail_name = f"__thumb__{file_path}"
        
        # Get native engine
        native_engine = None
        if self.__engine:
            if hasattr(self.__engine, 'has_imgui_texture'):
                native_engine = self.__engine
            elif hasattr(self.__engine, 'get_native_engine'):
                native_engine = self.__engine.get_native_engine()

        if not native_engine:
            return 0

        # Check if texture is already uploaded (use engine's cache)
        if native_engine.has_imgui_texture(thumbnail_name):
            tex_id = native_engine.get_imgui_texture_id(thumbnail_name)
            if tex_id != 0:
                return tex_id

        # Check our local cache for mtime (to detect file changes)
        mtime = os.path.getmtime(file_path)
        if file_path in self.__thumbnail_cache:
            cached_id, cached_mtime = self.__thumbnail_cache[file_path]
            if cached_mtime == mtime and cached_id != 0:
                return cached_id

        # Limit how many textures we load per frame to avoid overwhelming GPU
        if not hasattr(self, '_thumbnails_loaded_this_frame'):
            self._thumbnails_loaded_this_frame = 0

        if self._thumbnails_loaded_this_frame >= 1:
            # Skip loading more this frame, will try next frame
            return 0

        # Load texture using TextureLoader
        texture_data = TextureLoader.load_from_file(file_path)
        if not texture_data or not texture_data.is_valid():
            return 0

        # Upload to GPU for ImGui
        pixels = texture_data.get_pixels_list()
        tex_id = native_engine.upload_texture_for_imgui(
            thumbnail_name,
            pixels,
            texture_data.width,
            texture_data.height
        )

        if tex_id != 0:
            self.__thumbnail_cache[file_path] = (tex_id, mtime)
            self._thumbnails_loaded_this_frame += 1
            return tex_id

        
        return 0
    
    def _reset_frame_counters(self):
        """Reset per-frame counters. Call at start of on_render."""
        self._thumbnails_loaded_this_frame = 0
    
    # ------------------------------------------------------------------ icons
    def _get_native_engine(self):
        """Resolve to the raw C++ InfEngine object."""
        if not self.__engine:
            return None
        if hasattr(self.__engine, 'has_imgui_texture'):
            return self.__engine
        if hasattr(self.__engine, 'get_native_engine'):
            return self.__engine.get_native_engine()
        return None

    def _ensure_type_icons_loaded(self):
        """Lazily upload all file-type icons to GPU (once)."""
        if self.__type_icons_loaded:
            return
        native = self._get_native_engine()
        if native is None:
            return

        # Collect unique icon filenames we need
        needed = set(self.ICON_MAP.values())
        needed.add('file')  # generic fallback

        for icon_key in needed:
            tex_name = f"__typeicon__{icon_key}"
            # Already uploaded in a previous session / hot-reload?
            if native.has_imgui_texture(tex_name):
                self.__type_icon_cache[icon_key] = native.get_imgui_texture_id(tex_name)
                continue

            icon_path = os.path.join(file_type_icons_dir, f"{icon_key}.png")
            if not os.path.isfile(icon_path):
                continue  # user hasn't added this icon yet, will fall back to text

            tex_data = TextureLoader.load_from_file(icon_path)
            if tex_data and tex_data.is_valid():
                pixels = tex_data.get_pixels_list()
                tid = native.upload_texture_for_imgui(
                    tex_name, pixels, tex_data.width, tex_data.height)
                if tid != 0:
                    self.__type_icon_cache[icon_key] = tid

        self.__type_icons_loaded = True

    def _get_type_icon_id(self, item_type: str, ext: str) -> int:
        """Return the ImGui texture id for a file type, or 0 if not available."""
        if item_type == 'dir':
            key = self.ICON_MAP.get('__dir__')
        else:
            key = self.ICON_MAP.get(ext)
        if key is None:
            key = 'file'  # generic fallback
        return self.__type_icon_cache.get(key, 0)

    def _delete_item(self, item_path: str):
        file_ops.delete_item(item_path, self.__asset_database, self.__file_manager)
        if self.__selected_file == item_path:
            self.__selected_file = None
        if item_path in self.__thumbnail_cache:
            del self.__thumbnail_cache[item_path]
    
    def _should_show(self, name: str) -> bool:
        return project_utils.should_show(name)
    
    def _open_file_with_system(self, file_path: str):
        project_utils.open_file_with_system(file_path, project_root=self.__root_path)
    
    def _create_folder(self, folder_name: str) -> bool:
        ok, err = file_ops.create_folder(self.__current_path, folder_name)
        if not ok:
            self.__create_error = err
        return ok
    
    def _create_script(self, script_name: str) -> bool:
        ok, err = file_ops.create_script(self.__current_path, script_name,
                                         self.__asset_database, self.__file_manager)
        if not ok:
            self.__create_error = err
        return ok
    
    def _create_shader(self, shader_name: str, shader_type: str) -> bool:
        ok, err = file_ops.create_shader(self.__current_path, shader_name, shader_type,
                                         self.__asset_database, self.__file_manager)
        if not ok:
            self.__create_error = err
        return ok
    
    def _create_material(self, material_name: str) -> bool:
        ok, err = file_ops.create_material(self.__current_path, material_name,
                                           self.__asset_database)
        if not ok:
            self.__create_error = err
        return ok

    def _create_scene(self, scene_name: str):
        """Create a new .scene file and return the path on success."""
        ok, result = file_ops.create_scene(self.__current_path, scene_name,
                                           self.__asset_database)
        if not ok:
            self.__create_error = result
            return None
        return result

    def _open_scene_file(self, file_path: str):
        """Open a .scene file via the SceneFileManager."""
        from InfEngine.engine.scene_manager import SceneFileManager
        sfm = SceneFileManager.instance()
        if sfm:
            sfm.open_scene(file_path)
        else:
            from InfEngine.debug import Debug
            Debug.log_warning("SceneFileManager not initialized")

    def _get_unique_name(self, base_name: str, extension: str = "") -> str:
        return file_ops.get_unique_name(self.__current_path, base_name, extension)
    
    def _do_rename(self):
        if not self.__renaming_path or not self.__renaming_name:
            self.__renaming_path = None
            return
        old_path = self.__renaming_path
        new_path = file_ops.do_rename(old_path, self.__renaming_name,
                                      self.__asset_database, self.__file_manager)
        if new_path and self.__selected_file == old_path:
            self.__selected_file = new_path
        if new_path and self.__current_path == old_path:
            self.__current_path = new_path
        self.__renaming_path = None
    
    def _update_material_name_in_file(self, mat_path: str, new_name: str):
        project_utils.update_material_name_in_file(mat_path, new_name)
    
    def _handle_item_click(self, item: dict):
        """Handle click on an item (file or folder)."""
        import time
        current_time = time.time()
        double_clicked = (self.__last_clicked_file == item['path'] and 
                          current_time - self.__last_click_time < 0.4)
        
        self.__last_clicked_file = item['path']
        self.__last_click_time = current_time
        
        if item['type'] == 'dir':
            self.__selected_file = item['path']  # Allow selecting folder
            if self.__on_file_selected:
                self.__on_file_selected(self.__selected_file)  # Notify inspector
                
            if double_clicked:
                self.__current_path = item['path']
                self.__last_clicked_file = None
        else:
            self.__selected_file = item['path']
            if self.__on_file_selected:
                self.__on_file_selected(self.__selected_file)
            if double_clicked:
                ext = os.path.splitext(item['path'])[1].lower()
                if ext == '.scene':
                    # Double-click .scene -> open scene
                    self._open_scene_file(item['path'])
                else:
                    self._open_file_with_system(item['path'])

    def _render_context_menu(self, ctx: InfGUIContext):
        """Render right-click context menu for creating items."""
        if ctx.begin_popup_context_window("ProjectContextMenu", 1):
            if ctx.begin_menu("创建  Create"):
                if ctx.selectable("文件夹  Folder", False, 0, 0, 0):
                    # Directly create folder with unique name
                    folder_name = self._get_unique_name("NewFolder")
                    self._create_folder(folder_name)
                ctx.separator()
                if ctx.selectable("脚本  Script (.py)", False, 0, 0, 0):
                    # Enter pending script mode - user names it first, then we create
                    # Pass .py extension to properly detect existing scripts
                    self.__pending_script_name = self._get_unique_name("NewComponent", ".py")
                    self.__pending_script_focus = True
                ctx.separator()
                if ctx.selectable("顶点着色器  Vertex Shader (.vert)", False, 0, 0, 0):
                    self.__pending_shader_name = self._get_unique_name("NewShader", ".vert")
                    self.__pending_shader_type = "vert"
                    self.__pending_shader_focus = True
                if ctx.selectable("片段着色器  Fragment Shader (.frag)", False, 0, 0, 0):
                    self.__pending_shader_name = self._get_unique_name("NewShader", ".frag")
                    self.__pending_shader_type = "frag"
                    self.__pending_shader_focus = True
                ctx.separator()
                if ctx.selectable("材质  Material (.mat)", False, 0, 0, 0):
                    self.__pending_material_name = self._get_unique_name("NewMaterial", ".mat")
                    self.__pending_material_focus = True
                ctx.separator()
                if ctx.selectable("场景  Scene (.scene)", False, 0, 0, 0):
                    self.__pending_scene_name = self._get_unique_name("NewScene", ".scene")
                    self.__pending_scene_focus = True
                ctx.end_menu()
            
            if self.__selected_file and os.path.exists(self.__selected_file):
                ctx.separator()
                if ctx.selectable("重命名 Rename  (F2)", False, 0, 0, 0):
                    self.__renaming_path = self.__selected_file
                    name = os.path.basename(self.__selected_file)
                    if os.path.isfile(self.__renaming_path):
                        name = os.path.splitext(name)[0]
                    self.__renaming_name = name
                    self.__rename_focus_requested = True
                if ctx.selectable("删除 Delete  (Del)", False, 0, 0, 0):
                    self._delete_item(self.__selected_file)
            
            ctx.end_popup()
    
    def _render_folder_tree(self, ctx: InfGUIContext, path: str, depth: int = 0):
        """Recursively render folder tree."""
        entries = sorted(os.listdir(path))
        dirs = [e for e in entries if os.path.isdir(os.path.join(path, e)) and self._should_show(e)]

        for d in dirs:
            full_path = os.path.join(path, d)
            is_selected = self.__current_path == full_path

            # Check if folder has subfolders
            sub_dirs = [e for e in os.listdir(full_path)
                       if os.path.isdir(os.path.join(full_path, e)) and self._should_show(e)]

            if sub_dirs:
                # Has subfolders - use tree node (arrow indicator shows expandability)
                # ImGuiTreeNodeFlags_OpenOnArrow = 32, click arrow to expand, click label to select
                node_label = f"{d}##{full_path}"
                node_open = ctx.tree_node(node_label)
                # Check if item was clicked (not just arrow) - select this folder
                if ctx.is_item_clicked():
                    self.__current_path = full_path
                if node_open:
                    self._render_folder_tree(ctx, full_path, depth + 1)
                    ctx.tree_pop()
            else:
                # No subfolders - use selectable (leaf node)
                if ctx.selectable(f"{d}##{full_path}", is_selected):
                    self.__current_path = full_path
    
    def _get_file_type(self, filename: str) -> str:
        return project_utils.get_file_type(filename)
    
    def on_render(self, ctx: InfGUIContext):
        if not self._is_open:
            return
        
        # Reset per-frame counters
        self._reset_frame_counters()
        # Ensure file-type icons are uploaded to GPU
        self._ensure_type_icons_loaded()
            
        ctx.set_next_window_size(800, 250, Theme.COND_FIRST_USE_EVER)
        if self._begin_closable_window(ctx, 0):
            # Top toolbar with breadcrumb
            rel_path = os.path.relpath(self.__current_path, self.__root_path) if self.__root_path else self.__current_path
            if rel_path == '.':
                rel_path = os.path.basename(self.__root_path) if self.__root_path else 'Project'
            ctx.label(f"Path: {rel_path}")
            ctx.separator()
            
            # Left panel: Folder tree showing entire project (about 25% width)
            tree_width = 200
            if ctx.begin_child("FolderTree", tree_width, 0, False):
                # Show entire project root in tree
                if self.__root_path and os.path.exists(self.__root_path):
                    project_name = os.path.basename(self.__root_path)
                    is_root_selected = self.__current_path == self.__root_path
                    # Root node - click to select, arrow to expand
                    ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
                    node_open = ctx.tree_node(f"{project_name}##{self.__root_path}")
                    if ctx.is_item_clicked():
                        self.__current_path = self.__root_path
                    if node_open:
                        self._render_folder_tree(ctx, self.__root_path)
                        ctx.tree_pop()
                else:
                    ctx.label("No project path set")
            ctx.end_child()
            
            ctx.same_line()
            
            # Right panel: File grid/list (use border=True so WindowPadding applies, hide border color)
            ctx.push_style_var_vec2(ImGuiStyleVar.WindowPadding, *Theme.PROJECT_PANEL_PAD)
            Theme.push_transparent_border(ctx)  # 1 colour
            if ctx.begin_child("FileGrid", 0, 0, True):
                # Right-click context menu for creating items
                self._render_context_menu(ctx)
                
                if self.__current_path and os.path.exists(self.__current_path):
                    entries = sorted(os.listdir(self.__current_path))

                    # Back button
                    if self.__current_path != self.__root_path:
                        if ctx.selectable("[..]", False):
                            self.__current_path = os.path.dirname(self.__current_path)

                    # Grid config
                    icon_size = 64
                    padding = 10
                    cell_width = icon_size + padding
                    avail_w = ctx.get_content_region_avail_width()
                    cols = int(avail_w / cell_width)
                    if cols < 1: cols = 1

                    # Prepare items - FOLDERS FIRST, then FILES
                    items = []

                    # Separate dirs and files
                    dirs = []
                    files = []
                    for e in entries:
                        if not self._should_show(e): continue
                        full_path = os.path.join(self.__current_path, e)
                        if os.path.isdir(full_path):
                            dirs.append({'type': 'dir', 'name': e, 'path': full_path})
                        else:
                            files.append({'type': 'file', 'name': e, 'path': full_path})

                    # Add to items (dirs already sorted by os.listdir usually, but ensure consistency)
                    # os.listdir returns arbitrary order, we sorted 'entries' initially so dirs and files are sorted by name
                    items.extend(dirs)
                    items.extend(files)

                    if not items and self.__current_path == self.__root_path:
                        ctx.label("(Empty folder)")
                        ctx.label("Right-click to create new items")

                    # Handle F2 for rename and Delete key
                    if self.__selected_file and not self.__renaming_path:
                        if ctx.is_key_pressed(self.KEY_F2):
                            self.__renaming_path = self.__selected_file
                            name = os.path.basename(self.__selected_file)
                            if os.path.isfile(self.__renaming_path):
                                name = os.path.splitext(name)[0]
                            self.__renaming_name = name
                            self.__rename_focus_requested = True
                        elif ctx.is_key_pressed(self.KEY_DELETE):
                            self._delete_item(self.__selected_file)

                    if ctx.begin_table("FileGrid", cols, 0, 0.0):
                        for item in items:
                            ctx.table_next_column()
                            ctx.begin_group()

                            is_selected = (self.__selected_file == item['path'])

                            # Check if this is an image file that can have a thumbnail
                            ext = os.path.splitext(item['name'])[1].lower()
                            thumbnail_id = 0
                            if item['type'] == 'file' and ext in self.IMAGE_EXTENSIONS:
                                thumbnail_id = self._get_thumbnail(item['path'], icon_size)

                            # Determine which texture to show for the cell
                            display_tex_id = thumbnail_id
                            if display_tex_id == 0:
                                display_tex_id = self._get_type_icon_id(item['type'], ext)

                            if display_tex_id != 0:
                                # Render clickable icon image
                                # Zero out FramePadding so the image is centered in the cell
                                ctx.push_style_var_vec2(ImGuiStyleVar.FramePadding, *Theme.ICON_BTN_NO_PAD)
                                # Remove border on unselected items, highlight selected
                                if is_selected:
                                    Theme.push_selected_icon_style(ctx)  # 2 colours
                                else:
                                    Theme.push_unselected_icon_style(ctx)  # 2 colours + 1 var
                                if ctx.image_button(f"##icon_{item['path']}", display_tex_id, icon_size, icon_size):
                                    self._handle_item_click(item)
                                if is_selected:
                                    ctx.pop_style_color(2)
                                else:
                                    ctx.pop_style_color(2)
                                    ctx.pop_style_var(1)  # FrameBorderSize
                                ctx.pop_style_var(1)  # FramePadding
                            else:
                                # Ultimate fallback — text label (only when icon PNGs are missing)
                                label_icon = self._get_file_type(item['name']) if item['type'] != 'dir' else '[DIR]'
                                if ctx.selectable(f"{label_icon}\n##{item['path']}", is_selected, 0, icon_size, icon_size):
                                    self._handle_item_click(item)

                            # Drag-drop sources
                            if item['type'] == 'file':
                                if ext == '.py':
                                    if ctx.begin_drag_drop_source(0):
                                        ctx.set_drag_drop_payload_str("SCRIPT_FILE", item['path'])
                                        ctx.label(f"Script: {item['name']}")
                                        ctx.end_drag_drop_source()
                                elif ext == '.mat':
                                    if ctx.begin_drag_drop_source(0):
                                        ctx.set_drag_drop_payload_str("MATERIAL_FILE", item['path'])
                                        ctx.label(f"Material: {item['name']}")
                                        ctx.end_drag_drop_source()
                                elif ext in ['.vert', '.frag', '.glsl', '.hlsl']:
                                    if ctx.begin_drag_drop_source(0):
                                        ctx.set_drag_drop_payload_str("SHADER_FILE", item['path'])
                                        ctx.label(f"Shader: {item['name']}")
                                        ctx.end_drag_drop_source()
                                elif ext in ['.png', '.jpg', '.jpeg', '.bmp', '.tga', '.gif', '.psd', '.hdr', '.pic']:
                                    if ctx.begin_drag_drop_source(0):
                                        ctx.set_drag_drop_payload_str("TEXTURE_FILE", item['path'])
                                        ctx.label(f"Texture: {item['name']}")
                                        ctx.end_drag_drop_source()
                                elif ext == '.wav':
                                    if ctx.begin_drag_drop_source(0):
                                        ctx.set_drag_drop_payload_str("AUDIO_FILE", item['path'])
                                        ctx.label(f"Audio: {item['name']}")
                                        ctx.end_drag_drop_source()
                                elif ext == '.scene':
                                    if ctx.begin_drag_drop_source(0):
                                        ctx.set_drag_drop_payload_str("SCENE_FILE", item['path'])
                                        ctx.label(f"Scene: {item['name']}")
                                        ctx.end_drag_drop_source()

                            # Name or Rename Input
                            if self.__renaming_path == item['path']:
                                # Render input field
                                # Note: text_input now returns the current string value
                                if self.__rename_focus_requested:
                                    ctx.set_keyboard_focus_here()
                                    self.__rename_focus_requested = False

                                # Set width to avoid taking too much space
                                ctx.set_next_item_width(cell_width - 8)
                                new_name = ctx.text_input(f"##rename_{item['path']}", self.__renaming_name, 256)
                                self.__renaming_name = new_name

                                # Check Enter key to commit (ImGuiKey_Enter = 525)
                                if ctx.is_key_pressed(self.KEY_ENTER):
                                    self._do_rename()

                                # Check Escape to cancel (ImGuiKey_Escape = 526)
                                elif ctx.is_key_pressed(526):
                                    self.__renaming_path = None

                                # Commit on blur (focus lost)
                                elif ctx.is_item_deactivated():
                                    self._do_rename()

                            else:
                                name_display = item['name']
                                if item['type'] == 'file':
                                    name_display = os.path.splitext(item['name'])[0]

                                # Allow full name display (wrap if needed logic to be added later if supported)
                                # For now, just show the name, maybe let it overflow slightly or rely on cell clipping
                                ctx.label(name_display)

                            ctx.end_group()
                        ctx.end_table()

                        # Render pending script creation input (after table, at bottom)
                        if self.__pending_script_name is not None:
                            ctx.table_next_column() if False else None  # Skip - we're outside table now
                            ctx.separator()
                            ctx.label("新建脚本 New Script:")
                            ctx.same_line()

                            if self.__pending_script_focus:
                                ctx.set_keyboard_focus_here()
                                self.__pending_script_focus = False

                            ctx.set_next_item_width(200)
                            new_name = ctx.text_input("##pending_script", self.__pending_script_name, 256)
                            self.__pending_script_name = new_name

                            # Enter to confirm
                            if ctx.is_key_pressed(self.KEY_ENTER):
                                if self.__pending_script_name.strip():
                                    if self._create_script(self.__pending_script_name):
                                        # Select the newly created file and enter rename mode
                                        script_file = self.__pending_script_name
                                        if not script_file.endswith('.py'):
                                            script_file += '.py'
                                        new_path = os.path.join(self.__current_path, script_file)
                                        self.__selected_file = new_path
                                self.__pending_script_name = None

                            # Escape to cancel
                            elif ctx.is_key_pressed(526):  # Escape
                                self.__pending_script_name = None

                            # Blur to confirm
                            elif ctx.is_item_deactivated():
                                if self.__pending_script_name and self.__pending_script_name.strip():
                                    if self._create_script(self.__pending_script_name):
                                        script_file = self.__pending_script_name
                                        if not script_file.endswith('.py'):
                                            script_file += '.py'
                                        new_path = os.path.join(self.__current_path, script_file)
                                        self.__selected_file = new_path
                                self.__pending_script_name = None

                        # Render pending shader creation input
                        if self.__pending_shader_name is not None:
                            ctx.separator()
                            shader_type_label = "顶点着色器" if self.__pending_shader_type == "vert" else "片段着色器"
                            ctx.label(f"新建{shader_type_label} New Shader:")
                            ctx.same_line()

                            if self.__pending_shader_focus:
                                ctx.set_keyboard_focus_here()
                                self.__pending_shader_focus = False

                            ctx.set_next_item_width(200)
                            new_name = ctx.text_input("##pending_shader", self.__pending_shader_name, 256)
                            self.__pending_shader_name = new_name

                            # Enter to confirm
                            if ctx.is_key_pressed(self.KEY_ENTER):
                                if self.__pending_shader_name.strip():
                                    if self._create_shader(self.__pending_shader_name, self.__pending_shader_type):
                                        shader_file = self.__pending_shader_name
                                        if not shader_file.endswith(f'.{self.__pending_shader_type}'):
                                            shader_file += f'.{self.__pending_shader_type}'
                                        new_path = os.path.join(self.__current_path, shader_file)
                                        self.__selected_file = new_path
                                self.__pending_shader_name = None
                                self.__pending_shader_type = None

                            # Escape to cancel
                            elif ctx.is_key_pressed(526):  # Escape
                                self.__pending_shader_name = None
                                self.__pending_shader_type = None

                            # Blur to confirm
                            elif ctx.is_item_deactivated():
                                if self.__pending_shader_name and self.__pending_shader_name.strip():
                                    if self._create_shader(self.__pending_shader_name, self.__pending_shader_type):
                                        shader_file = self.__pending_shader_name
                                        if not shader_file.endswith(f'.{self.__pending_shader_type}'):
                                            shader_file += f'.{self.__pending_shader_type}'
                                        new_path = os.path.join(self.__current_path, shader_file)
                                        self.__selected_file = new_path
                                self.__pending_shader_name = None
                                self.__pending_shader_type = None

                        # Render pending material creation input
                        if self.__pending_material_name is not None:
                            ctx.separator()
                            ctx.label("新建材质 New Material:")
                            ctx.same_line()

                            if self.__pending_material_focus:
                                ctx.set_keyboard_focus_here()
                                self.__pending_material_focus = False

                            ctx.set_next_item_width(200)
                            new_name = ctx.text_input("##pending_material", self.__pending_material_name, 256)
                            self.__pending_material_name = new_name

                            # Enter to confirm
                            if ctx.is_key_pressed(self.KEY_ENTER):
                                if self.__pending_material_name.strip():
                                    if self._create_material(self.__pending_material_name):
                                        mat_file = self.__pending_material_name
                                        if not mat_file.endswith('.mat'):
                                            mat_file += '.mat'
                                        new_path = os.path.join(self.__current_path, mat_file)
                                        self.__selected_file = new_path
                                self.__pending_material_name = None

                            # Escape to cancel
                            elif ctx.is_key_pressed(526):  # Escape
                                self.__pending_material_name = None

                            # Blur to confirm
                            elif ctx.is_item_deactivated():
                                if self.__pending_material_name and self.__pending_material_name.strip():
                                    if self._create_material(self.__pending_material_name):
                                        mat_file = self.__pending_material_name
                                        if not mat_file.endswith('.mat'):
                                            mat_file += '.mat'
                                        new_path = os.path.join(self.__current_path, mat_file)
                                        self.__selected_file = new_path
                                self.__pending_material_name = None

                        # Render pending scene creation input
                        if self.__pending_scene_name is not None:
                            ctx.separator()
                            ctx.label("新建场景 New Scene:")
                            ctx.same_line()

                            if self.__pending_scene_focus:
                                ctx.set_keyboard_focus_here()
                                self.__pending_scene_focus = False

                            ctx.set_next_item_width(200)
                            new_name = ctx.text_input("##pending_scene", self.__pending_scene_name, 256)
                            self.__pending_scene_name = new_name

                            # Enter to confirm
                            if ctx.is_key_pressed(self.KEY_ENTER):
                                if self.__pending_scene_name.strip():
                                    scene_path = self._create_scene(self.__pending_scene_name)
                                    if scene_path:
                                        self.__selected_file = scene_path
                                self.__pending_scene_name = None

                            # Escape to cancel
                            elif ctx.is_key_pressed(526):  # Escape
                                self.__pending_scene_name = None

                            # Blur to confirm
                            elif ctx.is_item_deactivated():
                                if self.__pending_scene_name and self.__pending_scene_name.strip():
                                    scene_path = self._create_scene(self.__pending_scene_name)
                                    if scene_path:
                                        self.__selected_file = scene_path
                                self.__pending_scene_name = None

                else:
                    ctx.label("Invalid path")
            ctx.end_child()
            ctx.pop_style_color(1)   # Border (from push_transparent_border)
            ctx.pop_style_var(1)     # WindowPadding
            
        ctx.end_window()
