"""Type stubs for InfEngine.engine.engine."""

from __future__ import annotations

from typing import Optional, Tuple

from InfEngine.lib._InfEngine import (
    AssetDatabase,
    InfEngine as NativeInfEngine,
    InfGUIRenderable,
    LogLevel,
    ResourcePreviewManager,
)
from InfEngine.engine.play_mode import PlayModeManager
from InfEngine.rendering.render_pipeline import RenderPipeline, RenderPipelineAsset


class Engine:
    """High-level Python wrapper around the C++ InfEngine.

    Example::

        engine = Engine(LogLevel.Info)
        engine.init_renderer(width=1600, height=900, project_path="./MyProject")
        engine.show()
        engine.run()
    """

    def __init__(self, engine_log_level: LogLevel = ...) -> None: ...

    def init_renderer(self, width: int, height: int, project_path: str) -> None:
        """Initialize the Vulkan renderer and load project resources."""
        ...

    def run(self) -> None:
        """Start the main engine loop (blocking)."""
        ...

    def tick_play_mode(self) -> float:
        """Called each frame; returns delta_time in seconds."""
        ...

    def get_play_mode_manager(self) -> PlayModeManager:
        """Get the play mode manager (play/pause/stop)."""
        ...

    def exit(self) -> None:
        """Clean up and exit the engine."""
        ...

    def set_gui_font(self, font_path: str, font_size: int = ...) -> None: ...
    def set_log_level(self, engine_log_level: LogLevel) -> None: ...

    def register_gui(self, name: str, gui_object: InfGUIRenderable) -> None:
        """Register a GUI renderable panel."""
        ...
    def unregister_gui(self, name: str) -> None:
        """Unregister a GUI renderable panel."""
        ...

    def show(self) -> None: ...
    def hide(self) -> None: ...

    def get_native_engine(self) -> NativeInfEngine:
        """Get the underlying C++ InfEngine instance."""
        ...
    def get_resource_preview_manager(self) -> Optional[ResourcePreviewManager]: ...
    def get_asset_database(self) -> Optional[AssetDatabase]: ...

    # Scene camera control
    def process_scene_view_input(
        self,
        delta_time: float,
        right_mouse_down: bool,
        middle_mouse_down: bool,
        mouse_delta_x: float,
        mouse_delta_y: float,
        scroll_delta: float,
        key_w: bool,
        key_a: bool,
        key_s: bool,
        key_d: bool,
        key_q: bool,
        key_e: bool,
        key_shift: bool,
    ) -> None: ...
    def get_editor_camera_position(self) -> Tuple[float, float, float]: ...
    def get_editor_camera_rotation(self) -> Tuple[float, float]: ...
    def reset_editor_camera(self) -> None: ...
    def focus_editor_camera_on(
        self, x: float, y: float, z: float, distance: float = ...
    ) -> None: ...

    # Scene render target
    def get_scene_texture_id(self) -> int: ...
    def resize_scene_render_target(self, width: int, height: int) -> None: ...

    # Scene picking
    def pick_scene_object_id(
        self,
        screen_x: float,
        screen_y: float,
        viewport_width: float,
        viewport_height: float,
    ) -> int:
        """Pick a scene object by screen-space coordinates; returns object ID or 0."""
        ...

    # Editor tools (gizmo highlight + ray)
    def set_editor_tool_highlight(self, axis: int) -> None:
        """Set highlighted gizmo axis. 0=None, 1=X, 2=Y, 3=Z."""
        ...
    def screen_to_world_ray(
        self,
        screen_x: float,
        screen_y: float,
        viewport_width: float,
        viewport_height: float,
    ) -> tuple[float, float, float, float, float, float]:
        """Build a world-space ray from screen coords."""
        ...
    def get_selected_object_id(self) -> int:
        """Get the currently selected object ID (0 if none)."""
        ...

    # Editor gizmos
    def set_show_grid(self, show: bool) -> None: ...
    def is_show_grid(self) -> bool: ...
    def set_window_icon(self, icon_path: str) -> None:
        """Set the window icon from an image file."""
        ...

    # Camera settings
    def get_editor_camera_fov(self) -> float: ...
    def set_editor_camera_fov(self, fov: float) -> None: ...
    def get_editor_camera_near_clip(self) -> float: ...
    def set_editor_camera_near_clip(self, near_clip: float) -> None: ...
    def get_editor_camera_far_clip(self) -> float: ...
    def set_editor_camera_far_clip(self, far_clip: float) -> None: ...

    # Render pipeline (SRP)
    def set_render_pipeline(
        self, asset_or_pipeline: Optional[RenderPipelineAsset | RenderPipeline] = ...
    ) -> None:
        """Set a custom render pipeline (or ``None`` for default C++ path)."""
        ...
