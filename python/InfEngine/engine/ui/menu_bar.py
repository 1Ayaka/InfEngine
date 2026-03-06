from InfEngine.lib import InfGUIRenderable, InfGUIContext, InfEngine
from typing import TYPE_CHECKING
from .build_settings_panel import BuildSettingsPanel

if TYPE_CHECKING:
    from .window_manager import WindowManager
    from InfEngine.engine.scene_manager import SceneFileManager

# ImGuiKey constants
KEY_S = 564
KEY_N = 559
KEY_Z = 571
KEY_Y = 570
KEY_LEFT_CTRL = 527
KEY_RIGHT_CTRL = 531

class MenuBarPanel(InfGUIRenderable):
    def __init__(self, app):
        super().__init__()
        self.__app = app
        self.__window_manager = None
        self._dark_mode = True  # default to dark
        self._scene_file_manager = None
        self._build_settings = BuildSettingsPanel()

    def set_window_manager(self, window_manager: 'WindowManager'):
        """Set the window manager for the Window menu."""
        self.__window_manager = window_manager

    def set_scene_file_manager(self, sfm: 'SceneFileManager'):
        """Set the SceneFileManager for File menu operations."""
        self._scene_file_manager = sfm

    def _open_tag_layer_settings(self, focus_collision_matrix: bool = False):
        """Open the shared Tags & Layers window, optionally focusing the collision matrix."""
        if not self.__window_manager:
            return

        panel = self.__window_manager.open_window("tag_layer_settings")
        if focus_collision_matrix and panel and hasattr(panel, "focus_collision_matrix"):
            panel.focus_collision_matrix()

    def on_render(self, ctx: InfGUIContext):
        # Handle global shortcuts (before any menu logic)
        self._handle_shortcuts(ctx)

        # Poll pending save dialog results every frame
        if self._scene_file_manager:
            self._scene_file_manager.poll_pending_save()

        # Check for window close request (SDL_EVENT_QUIT intercepted by C++)
        if self._scene_file_manager and self.__app:
            native = self.__app.get_native_engine() if hasattr(self.__app, 'get_native_engine') else None
            if native and native.is_close_requested():
                self._scene_file_manager.request_close()

        if ctx.begin_main_menu_bar():
            # Project menu - build configuration (leftmost)
            if ctx.begin_menu("项目 Project", True):
                if ctx.menu_item("构建设置  Build Settings", "", self._build_settings.is_open, True):
                    if self._build_settings.is_open:
                        self._build_settings.close()
                    else:
                        self._build_settings.open()

                if self.__window_manager:
                    is_tag_layer_open = self.__window_manager.is_window_open("tag_layer_settings")
                    if ctx.menu_item("物理层交互矩阵  Physics Layer Matrix", "", is_tag_layer_open, True):
                        self._open_tag_layer_settings(True)
                ctx.end_menu()

            # Window menu - show all registered window types
            if ctx.begin_menu("Window", True):
                if self.__window_manager:
                    registered_types = self.__window_manager.get_registered_types()
                    open_windows = self.__window_manager.get_open_windows()
                    
                    if registered_types:
                        for type_id, info in registered_types.items():
                            # Check if window is already open
                            is_open = open_windows.get(type_id, False)
                            # Grayed out if already open (for singletons)
                            can_create = not (info.singleton and is_open)
                            
                            # Show checkmark if window is open
                            label = info.display_name
                            if ctx.menu_item(label, "", is_open, can_create):
                                if is_open:
                                    # Close the window
                                    self.__window_manager.close_window(type_id)
                                else:
                                    # Open the window
                                    self.__window_manager.open_window(type_id)
                    else:
                        ctx.menu_item("(No windows registered)", "", False, False)
                else:
                    ctx.menu_item("(Window manager not set)", "", False, False)
                
                ctx.separator()
                if ctx.menu_item("Reset Layout", "", False, True):
                    print("Reset layout")
                
                ctx.end_menu()

            ctx.end_main_menu_bar()

        # Render Build Settings floating window (not docked)
        self._build_settings.render(ctx)

        # Render save-confirmation modal (if pending)
        if self._scene_file_manager:
            self._scene_file_manager.render_confirmation_popup(ctx)

    def _handle_shortcuts(self, ctx: InfGUIContext):
        """Process global keyboard shortcuts."""
        ctrl = ctx.is_key_down(KEY_LEFT_CTRL) or ctx.is_key_down(KEY_RIGHT_CTRL)
        if not ctrl:
            return

        if ctx.is_key_pressed(KEY_S):
            if self._scene_file_manager:
                self._scene_file_manager.save_current_scene()

        if ctx.is_key_pressed(KEY_N):
            if self._scene_file_manager:
                self._scene_file_manager.new_scene()

        if ctx.is_key_pressed(KEY_Z):
            undo_mgr = self._get_undo_manager()
            if undo_mgr and undo_mgr.can_undo:
                undo_mgr.undo()

        if ctx.is_key_pressed(KEY_Y):
            undo_mgr = self._get_undo_manager()
            if undo_mgr and undo_mgr.can_redo:
                undo_mgr.redo()

    @staticmethod
    def _get_undo_manager():
        """Lazily fetch the UndoManager singleton."""
        from InfEngine.engine.undo import UndoManager
        return UndoManager.instance()
