from InfEngine.lib import InfGUIRenderable, InfGUIContext, TextureLoader, TextureData
from InfEngine.resources import engine_font_path, icon_path
from .engine import Engine, LogLevel
from .resources_manager import ResourcesManager
from .play_mode import PlayModeManager, PlayModeState
from .scene_manager import SceneFileManager
from .ui import (
    MenuBarPanel,
    FrameSchedulerPanel,
    ToolbarPanel,
    HierarchyPanel,
    InspectorPanel,
    ConsolePanel,
    SceneViewPanel,
    GameViewPanel,
    ProjectPanel,
    WindowManager,
    TagLayerSettingsPanel,
    StatusBarPanel,
    BuildSettingsPanel,
    UIEditorPanel,
    EditorPanel,
    EditorServices,
    EditorEventBus,
    EditorEvent,
    PanelRegistry,
    editor_panel,
)

def _create_tag_layer_panel(project_path: str):
    """Factory for TagLayerSettingsPanel with project path set."""
    panel = TagLayerSettingsPanel()
    panel.set_project_path(project_path)
    return panel

def release_engine(project_path: str, engine_log_level=LogLevel.Info):
    """
    Launch InfEngine with Unity-style editor layout.
    All panels are dockable and can be rearranged.
    """
    engine = Engine(engine_log_level)
    engine.init_renderer(
        width=1600,
        height=900,
        project_path=project_path,
    )
    engine.set_gui_font(engine_font_path, 14)
    
    # === Load project tag/layer settings ===
    import os
    from InfEngine.lib import TagLayerManager
    _tl_path = os.path.join(project_path, "ProjectSettings", "TagLayerSettings.json")
    if os.path.isfile(_tl_path):
        TagLayerManager.instance().load_from_file(_tl_path)

    # === Create Undo Manager ===
    from InfEngine.engine.undo import UndoManager
    undo_manager = UndoManager()

    # === Create Scene File Manager ===
    scene_file_manager = SceneFileManager()
    scene_file_manager.set_asset_database(engine.get_asset_database())
    scene_file_manager.set_engine(engine.get_native_engine())

    # === Create Window Manager ===
    window_manager = WindowManager(engine)

    # === Initialize editor services & event bus ===
    services = EditorServices()
    services._engine = engine
    services._undo_manager = undo_manager
    services._scene_file_manager = scene_file_manager
    services._play_mode_manager = engine._play_mode_manager
    services._window_manager = window_manager
    services._asset_database = engine.get_asset_database()
    services._project_path = project_path

    event_bus = EditorEventBus()
    
    # === Register window types ===
    # Register all panel types with their factory functions
    window_manager.register_window_type(
        type_id="hierarchy",
        window_class=HierarchyPanel,
        display_name="层级 Hierarchy",
        singleton=True
    )
    window_manager.register_window_type(
        type_id="inspector",
        window_class=InspectorPanel,
        display_name="检视器 Inspector",
        factory=lambda: InspectorPanel(engine=engine),
        singleton=True
    )
    window_manager.register_window_type(
        type_id="console",
        window_class=ConsolePanel,
        display_name="控制台 Console",
        singleton=True
    )
    window_manager.register_window_type(
        type_id="scene_view",
        window_class=SceneViewPanel,
        display_name="场景 Scene",
        factory=lambda: SceneViewPanel(engine=engine),
        singleton=True
    )
    window_manager.register_window_type(
        type_id="game_view",
        window_class=GameViewPanel,
        display_name="游戏 Game",
        factory=lambda: GameViewPanel(engine=engine),
        singleton=True
    )
    window_manager.register_window_type(
        type_id="ui_editor",
        window_class=UIEditorPanel,
        display_name="UI编辑器 UI Editor",
        factory=lambda: UIEditorPanel(),
        singleton=True
    )
    window_manager.register_window_type(
        type_id="project",
        window_class=ProjectPanel,
        display_name="项目 Project",
        factory=lambda: ProjectPanel(root_path=project_path, engine=engine),
        singleton=True
    )
    window_manager.register_window_type(
        type_id="toolbar",
        window_class=ToolbarPanel,
        display_name="工具栏 Toolbar",
        factory=lambda: ToolbarPanel(title="工具栏 Toolbar", engine=engine),
        singleton=True
    )
    window_manager.register_window_type(
        type_id="tag_layer_settings",
        window_class=TagLayerSettingsPanel,
        display_name="标签与图层 Tags & Layers",
        factory=lambda: _create_tag_layer_panel(project_path),
        singleton=True
    )
    # Build Settings is a standalone floating window owned by MenuBarPanel,
    # NOT registered through WindowManager (so it stays outside the dock layout).

    # === Register @editor_panel-decorated custom panels ===
    PanelRegistry.apply_all(window_manager)
    
    # === Register all editor panels ===

    # Per-frame scheduler (runs once per frame, independent of panel visibility)
    frame_scheduler = FrameSchedulerPanel(engine=engine)
    engine.register_gui("frame_scheduler", frame_scheduler)
    
    # Menu bar (top) - not closable, always visible
    menu_bar = MenuBarPanel(engine)
    menu_bar.set_window_manager(window_manager)
    menu_bar.set_scene_file_manager(scene_file_manager)
    engine.register_gui("menu_bar", menu_bar)
    
    # Toolbar (below menu bar) - Play/Pause/Stop + camera/grid/FOV controls
    toolbar = ToolbarPanel(title="工具栏 Toolbar", engine=engine)
    toolbar.set_window_manager(window_manager)
    engine.register_gui("toolbar", toolbar)
    window_manager.register_existing_window("toolbar", toolbar, "toolbar")
    
    # Hierarchy panel (left)
    hierarchy = HierarchyPanel()
    hierarchy.set_window_manager(window_manager)
    engine.register_gui("hierarchy", hierarchy)
    window_manager.register_existing_window("hierarchy", hierarchy, "hierarchy")
    
    # Inspector panel (right) - pass engine for resource preview
    inspector_panel = InspectorPanel(engine=engine)
    inspector_panel.set_window_manager(window_manager)
    engine.register_gui("inspector", inspector_panel)
    window_manager.register_existing_window("inspector", inspector_panel, "inspector")
    
    # ------------------------------------------------------------------
    # Centralized selection helper (used by undo/redo and direct picks)
    # ------------------------------------------------------------------
    # Mutable container to track the last selection (for undo old-value).
    _prev_selection = [0]  # list so the closure can mutate it

    def _apply_selection(object_id: int):
        """Apply a selection change across hierarchy, inspector, and outline.

        Called directly for undo/redo replays.  The *object_id* is 0 for
        deselect or a valid GameObject ID.
        """
        _prev_selection[0] = object_id
        if object_id:
            hierarchy.set_selected_object_by_id(object_id)
        else:
            hierarchy.clear_selection()

    def _record_selection(new_id: int):
        """Record a selection change in the undo system (if not replaying)."""
        old_id = _prev_selection[0]
        if old_id == new_id:
            return
        _prev_selection[0] = new_id
        from InfEngine.engine.undo import UndoManager, SelectionCommand
        mgr = UndoManager.instance()
        if mgr and not mgr.is_executing:
            mgr.record(SelectionCommand(
                old_id, new_id, _apply_selection, "Select"))

    # ------------------------------------------------------------------
    # Outline helper — sets or clears the orange wireframe on selected GO
    # ------------------------------------------------------------------
    def _set_outline(object_id: int):
        native = engine.get_native_engine()
        if not native:
            return
        if object_id:
            native.set_selection_outline(object_id)
        else:
            native.clear_selection_outline()

    # Connect hierarchy selection to inspector (mutually exclusive with project selection)
    def _on_hierarchy_selected(obj):
        # Record selection undo
        _record_selection(obj.id if obj is not None else 0)

        inspector_panel.set_selected_object(obj)
        if obj is not None:
            project_panel.clear_selection()
        _set_outline(obj.id if obj is not None else 0)
        # Emit for custom panels listening via EditorEventBus
        event_bus.emit(EditorEvent.SELECTION_CHANGED, obj)

    # Connect file selection to inspector (mutually exclusive with hierarchy selection)
    def _on_project_selected(path):
        inspector_panel.set_selected_file(path)
        if path:
            hierarchy.clear_selection()
        # Emit for custom panels listening via EditorEventBus
        event_bus.emit(EditorEvent.FILE_SELECTED, path)
    
    # Project panel (bottom) - for browsing assets
    project_panel = ProjectPanel(root_path=project_path, engine=engine)
    project_panel.set_window_manager(window_manager)
    # Wire selection callbacks
    hierarchy.set_on_selection_changed(_on_hierarchy_selected)
    project_panel.set_on_file_selected(_on_project_selected)
    engine.register_gui("project", project_panel)
    window_manager.register_existing_window("project", project_panel, "project")
    
    # Console panel (bottom, tabbed with project)
    console = ConsolePanel()
    console.set_window_manager(window_manager)
    # Wire Clear on Play / Error Pause to the PlayModeManager
    if engine._play_mode_manager is not None:
        console.set_play_mode_manager(engine._play_mode_manager)
    engine.register_gui("console", console)
    window_manager.register_existing_window("console", console, "console")

    # Status bar — fixed bottom strip, not dockable
    status_bar = StatusBarPanel()
    status_bar.set_console_panel(console)
    console.set_status_bar(status_bar)
    engine.register_gui("status_bar", status_bar)
    
    # Scene view (center) - pass engine for camera control
    scene_view = SceneViewPanel(engine=engine)
    scene_view.set_window_manager(window_manager)
    if engine._play_mode_manager is not None:
        scene_view.set_play_mode_manager(engine._play_mode_manager)
    engine.register_gui("scene_view", scene_view)
    window_manager.register_existing_window("scene_view", scene_view, "scene_view")

    # Sync scene view picking -> hierarchy selection & selection outline
    # Records an undo command so Ctrl+Z restores the previous selection.
    def _on_scene_view_picked(object_id: int):
        # Record selection undo
        _record_selection(object_id or 0)

        if object_id:
            hierarchy.set_selected_object_by_id(object_id)
        else:
            hierarchy.clear_selection()
        _set_outline(object_id or 0)
        # Emit for custom panels — resolve the GameObject from ID
        if object_id:
            from InfEngine.lib import SceneManager
            scene = SceneManager.instance().get_active_scene()
            obj = scene.find_by_id(object_id) if scene else None
            event_bus.emit(EditorEvent.SELECTION_CHANGED, obj)
        else:
            event_bus.emit(EditorEvent.SELECTION_CHANGED, None)

    scene_view.set_on_object_picked(_on_scene_view_picked)
    
    # Game view (center, tabbed with scene)
    game_view = GameViewPanel(engine=engine)
    game_view.set_window_manager(window_manager)
    engine.register_gui("game_view", game_view)
    window_manager.register_existing_window("game_view", game_view, "game_view")

    # UI Editor (center, tabbed with scene/game)
    ui_editor = UIEditorPanel()
    ui_editor.set_window_manager(window_manager)
    ui_editor.set_hierarchy_panel(hierarchy)
    ui_editor.set_engine(engine)
    # When UI Editor gains focus → hierarchy enters UI mode; other views exit it
    def _on_ui_mode_request(enter: bool):
        hierarchy.set_ui_mode(enter)
    ui_editor.set_on_request_ui_mode(_on_ui_mode_request)
    # When UI Editor selects a UI element → sync hierarchy + inspector
    def _on_ui_editor_selected(go):
        if go is not None:
            hierarchy.set_selected_object_by_id(go.id)
        else:
            hierarchy.clear_selection()
    ui_editor.set_on_selection_changed(_on_ui_editor_selected)
    engine.register_gui("ui_editor", ui_editor)
    window_manager.register_existing_window("ui_editor", ui_editor, "ui_editor")

    # Exit UI Mode when Scene View or Game View gain focus
    def _exit_ui_mode():
        if hierarchy.ui_mode:
            hierarchy.set_ui_mode(False)
    scene_view._on_focus_gained = _exit_ui_mode
    game_view._on_focus_gained = _exit_ui_mode
    
    # === Reset docking layout when layout version changes ===
    # Bump _LAYOUT_VERSION whenever the dock registration order or panel
    # set changes so that users get the correct tab arrangement.
    _LAYOUT_VERSION = 4  # bumped: layout now stored in Documents

    # Compute layout directory under user's Documents folder (matches C++ side)
    # Use ctypes on Windows to call SHGetFolderPathW for reliable Unicode support
    import pathlib as _pathlib
    _project_name = os.path.basename(project_path)
    _docs_dir = None
    if os.name == 'nt':
        try:
            import ctypes, ctypes.wintypes
            _buf = ctypes.create_unicode_buffer(ctypes.wintypes.MAX_PATH)
            # CSIDL_PERSONAL = 0x0005
            ctypes.windll.shell32.SHGetFolderPathW(None, 0x0005, None, 0, _buf)
            if _buf.value:
                _docs_dir = _pathlib.Path(_buf.value)
        except Exception:
            pass
    if _docs_dir is None:
        _docs_dir = _pathlib.Path.home() / "Documents"
    _layout_dir = _docs_dir / "InfEngine" / _project_name
    os.makedirs(_layout_dir, exist_ok=True)
    _layout_ver_path = str(_layout_dir / ".layout_version")
    _imgui_ini_path = str(_layout_dir / "imgui.ini")

    # Also clean up old project-local imgui.ini if it exists
    _old_ini = os.path.join(project_path, "imgui.ini")
    if os.path.isfile(_old_ini):
        try:
            os.remove(_old_ini)
        except OSError:
            pass

    _need_reset = True
    if os.path.isfile(_layout_ver_path):
        try:
            with open(_layout_ver_path, "r") as _f:
                if _f.read().strip() == str(_LAYOUT_VERSION):
                    _need_reset = False
        except OSError:
            pass
    if _need_reset:
        if os.path.isfile(_imgui_ini_path):
            os.remove(_imgui_ini_path)
        os.makedirs(os.path.dirname(_layout_ver_path), exist_ok=True)
        with open(_layout_ver_path, "w") as _f:
            _f.write(str(_LAYOUT_VERSION))

    # === Ensure renderstack module is loaded so RenderStack is discoverable ===
    import InfEngine.renderstack  # noqa: F401

    # === Load last scene or create default ===
    scene_file_manager.load_last_scene_or_default()

    engine.show()
    engine.set_window_icon(icon_path)
    engine.run()

__all__ = [
    "Engine",
    "LogLevel",
    "InfGUIRenderable",
    "InfGUIContext",
    "MenuBarPanel",
    "ToolbarPanel",
    "HierarchyPanel",
    "InspectorPanel",
    "ConsolePanel",
    "SceneViewPanel",
    "GameViewPanel",
    "UIEditorPanel",
    "ProjectPanel",
    "WindowManager",
    "TagLayerSettingsPanel",
    "StatusBarPanel",
    "PlayModeManager",
    "PlayModeState",
    "SceneFileManager",
    "TextureLoader",
    "TextureData",
    "release_engine",
    "ResourcesManager",
    "BuildSettingsPanel",
    # Panel framework
    "EditorPanel",
    "EditorServices",
    "EditorEventBus",
    "EditorEvent",
    "PanelRegistry",
    "editor_panel",
]
