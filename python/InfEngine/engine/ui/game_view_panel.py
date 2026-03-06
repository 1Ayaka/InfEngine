"""
Unity-style Game View panel — renders the scene through the game camera.

The Game View uses a separate render target from the Scene View.
It displays what the player would see through the scene's main Camera component.
"""

import os
import configparser
from typing import Optional
from InfEngine.lib import InfGUIContext
from InfEngine.input import Input
from InfEngine.engine.play_mode import PlayModeManager
from InfEngine.engine.project_context import get_project_root
from .closable_panel import ClosablePanel
from .theme import Theme, ImGuiCol
from .viewport_utils import capture_viewport_info


class GameViewPanel(ClosablePanel):
    """
    Unity-style Game View panel that renders the scene's main Camera output.
    
    The game camera is automatically bound to the first Camera component found
    in the scene (Scene.main_camera). When no camera is present, a helpful
    message is displayed instead.
    """
    
    WINDOW_TYPE_ID = "game_view"
    WINDOW_DISPLAY_NAME = "游戏 Game"

    _RESOLUTION_PRESETS = [
        ("1920\u00d71080", 1920, 1080),
        ("1280\u00d7720", 1280, 720),
        ("2560\u00d71440", 2560, 1440),
        ("3840\u00d72160", 3840, 2160),
        ("1080\u00d71920 Portrait", 1080, 1920),
        ("Custom", 1920, 1080),
    ]
    
    def __init__(self, title: str = "游戏 Game", engine=None, play_mode_manager: Optional[PlayModeManager] = None):
        super().__init__(title, window_id="game_view")
        self._engine = engine
        self._play_mode_manager = play_mode_manager
        if self._engine and self._play_mode_manager is None:
            self._play_mode_manager = self._engine.get_play_mode_manager()
        self.__is_playing = False
        
        # Game render target size tracking
        self._last_game_width = 0
        self._last_game_height = 0
        self._game_camera_was_enabled = False

        # Focus tracking for auto-exit UI Mode
        self._was_focused: bool = False
        self._on_focus_gained = None   # callback() when panel gains focus

        # Game resolution selection (Unity-like)
        self._selected_resolution_idx = 0
        self._custom_width = 1920
        self._custom_height = 1080
        self._display_scale = 0.5
        self._fit_mode = True            # When True, scale auto-adjusts to fill area
        self._settings_loaded = False
    
    def set_engine(self, engine):
        self._engine = engine
        if self._engine:
            self._play_mode_manager = self._engine.get_play_mode_manager()
    
    def set_play_mode_manager(self, manager: PlayModeManager):
        self._play_mode_manager = manager
    
    def _is_playing(self) -> bool:
        if self._play_mode_manager:
            return self._play_mode_manager.is_playing
        return self.__is_playing
    
    def _is_paused(self) -> bool:
        if self._play_mode_manager:
            return self._play_mode_manager.is_paused
        return False
    
    def _on_play_stop_clicked(self):
        if self._play_mode_manager:
            if self._play_mode_manager.is_playing:
                self._play_mode_manager.exit_play_mode()
            else:
                self._play_mode_manager.enter_play_mode()
        else:
            self.__is_playing = not self.__is_playing
    
    def _on_pause_clicked(self):
        if self._play_mode_manager and self._play_mode_manager.is_playing:
            self._play_mode_manager.toggle_pause()

    def _settings_ini_path(self) -> Optional[str]:
        root = get_project_root()
        if not root:
            return None
        return os.path.join(root, "ProjectSettings", "GameView.ini")

    def _load_resolution_settings(self):
        if self._settings_loaded:
            return
        self._settings_loaded = True

        path = self._settings_ini_path()
        if not path:
            return
        if not os.path.isfile(path):
            self._save_resolution_settings()
            return

        cp = configparser.ConfigParser()
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                cp.read_string(f.read())
        except Exception:
            return
        if "GameView" not in cp:
            return

        section = cp["GameView"]
        self._selected_resolution_idx = max(0, min(len(self._RESOLUTION_PRESETS) - 1,
                                                   section.getint("preset_index", fallback=0)))
        self._custom_width = max(64, section.getint("custom_width", fallback=1920))
        self._custom_height = max(64, section.getint("custom_height", fallback=1080))
        self._display_scale = max(0.1, min(2.0, section.getfloat("display_scale", fallback=0.5)))
        self._fit_mode = section.getboolean("fit_mode", fallback=True)

    def _save_resolution_settings(self):
        path = self._settings_ini_path()
        if not path:
            return

        os.makedirs(os.path.dirname(path), exist_ok=True)
        cp = configparser.ConfigParser()
        cp["GameView"] = {
            "preset_index": str(self._selected_resolution_idx),
            "custom_width": str(max(64, int(self._custom_width))),
            "custom_height": str(max(64, int(self._custom_height))),
            "display_scale": f"{self._display_scale:.3f}",
            "fit_mode": str(self._fit_mode),
        }
        with open(path, "w", encoding="utf-8") as f:
            cp.write(f)

    def _current_target_resolution(self):
        _, w, h = self._RESOLUTION_PRESETS[self._selected_resolution_idx]
        if self._selected_resolution_idx == len(self._RESOLUTION_PRESETS) - 1:
            return max(64, int(self._custom_width)), max(64, int(self._custom_height))
        return int(w), int(h)

    def _fit_scale(self):
        """Toggle Fit mode on."""
        self._fit_mode = True
        self._save_resolution_settings()

    @staticmethod
    def _fit_into_region(src_w: int, src_h: int, region_w: float, region_h: float):
        if src_w <= 0 or src_h <= 0 or region_w <= 0 or region_h <= 0:
            return 0.0, 0.0
        scale = min(region_w / float(src_w), region_h / float(src_h))
        return float(src_w) * scale, float(src_h) * scale
    
    def on_render(self, ctx: InfGUIContext):
        self._load_resolution_settings()

        if not self._is_open:
            # Disable game camera rendering when panel is closed
            if self._game_camera_was_enabled and self._engine:
                self._engine.set_game_camera_enabled(False)
                self._game_camera_was_enabled = False
            return
            
        ctx.set_next_window_size(800, 600, Theme.COND_FIRST_USE_EVER)

        # Viewport flags: don't steal focus + no scrollbars
        window_flags = Theme.WINDOW_FLAGS_VIEWPORT | Theme.WINDOW_FLAGS_NO_SCROLL
        if self._begin_closable_window(ctx, window_flags):
            # Track focus to auto-exit UI Mode
            focused = ctx.is_window_focused(0)
            if focused and not self._was_focused:
                if self._on_focus_gained:
                    self._on_focus_gained()
            self._was_focused = focused

            if not self._engine:
                ctx.label("Engine not initialized")
                ctx.end_window()
                return

            # Enable game camera rendering while panel is visible
            if not self._game_camera_was_enabled:
                self._engine.set_game_camera_enabled(True)
                self._game_camera_was_enabled = True

            # ── Resolution toolbar row ──
            preset_names = [p[0] for p in self._RESOLUTION_PRESETS]
            old_idx = self._selected_resolution_idx
            ctx.set_next_item_width(140)
            self._selected_resolution_idx = ctx.combo("##Resolution", self._selected_resolution_idx, preset_names, -1)
            if self._selected_resolution_idx != old_idx:
                self._save_resolution_settings()

            if self._selected_resolution_idx == len(self._RESOLUTION_PRESETS) - 1:
                ctx.same_line(0, 8)
                w_old = self._custom_width
                h_old = self._custom_height
                ctx.set_next_item_width(56)
                self._custom_width = int(ctx.drag_int("##CW", self._custom_width, 1.0, 64, 8192))
                ctx.same_line(0, 2)
                ctx.label("\u00d7")
                ctx.same_line(0, 2)
                ctx.set_next_item_width(56)
                self._custom_height = int(ctx.drag_int("##CH", self._custom_height, 1.0, 64, 8192))
                if self._custom_width != w_old or self._custom_height != h_old:
                    self._save_resolution_settings()

            # ── Scale slider row ──
            # _display_scale = actual pixel ratio: 1.0 = 100% (1:1 game pixels).
            avail_width = ctx.get_content_region_avail_width()
            avail_height = ctx.get_content_region_avail_height()
            target_w, target_h = self._current_target_resolution()

            # Compute the "fit" scale — the ratio that fills avail area
            fit_scale = 1.0
            if target_w > 0 and target_h > 0 and avail_width > 0 and avail_height > 0:
                fit_scale = min(avail_width / float(target_w), avail_height / float(target_h))
                fit_scale = max(0.01, fit_scale)

            # In Fit mode, always follow the fit_scale
            if self._fit_mode:
                self._display_scale = fit_scale

            ctx.same_line(0, 12)
            pct = int(round(self._display_scale * 100))
            ctx.label(f"{pct}%")
            ctx.same_line(0, 4)
            ctx.set_next_item_width(100)
            old_scale = self._display_scale
            # Slider range: 10% to 200%
            self._display_scale = ctx.float_slider("##Scale", self._display_scale, 0.10, 2.0)
            self._display_scale = round(self._display_scale, 3)
            # If user touched the slider, exit Fit mode
            if abs(old_scale - self._display_scale) > 0.001:
                self._fit_mode = False
                self._save_resolution_settings()
            ctx.same_line(0, 4)
            # Highlight Fit button when active
            if self._fit_mode:
                ctx.push_style_color(ImGuiCol.Button, 0.26, 0.59, 0.98, 0.70)
            ctx.button("Fit", self._fit_scale, width=32, height=0)
            if self._fit_mode:
                ctx.pop_style_color(1)

            # Draw size = target_resolution * display_scale
            draw_w = float(target_w) * self._display_scale
            draw_h = float(target_h) * self._display_scale

            # Canvas design resolution is driven by selected Game resolution.
            # Resize game render target if size changed
            if target_w != self._last_game_width or target_h != self._last_game_height:
                self._engine.resize_game_render_target(target_w, target_h)
                self._last_game_width = target_w
                self._last_game_height = target_h

            # Get and display game texture
            game_texture_id = self._engine.get_game_texture_id()

            # Remember cursor start for canvas preview offset calculation
            cursor_start_x = ctx.get_cursor_pos_x()
            cursor_start_y = ctx.get_cursor_pos_y()

            if game_texture_id != 0:
                # Display game camera render output
                pad_x = max(0.0, (avail_width - draw_w) * 0.5)
                pad_y = max(0.0, (avail_height - draw_h) * 0.5)
                ctx.set_cursor_pos_x(cursor_start_x + pad_x)
                ctx.set_cursor_pos_y(cursor_start_y + pad_y)
                ctx.image(game_texture_id, float(draw_w), float(draw_h), 0.0, 0.0, 1.0, 1.0)

                # Use shared viewport utility to capture image rect; the
                # top-left corner is exactly the viewport origin for Input.
                vp = capture_viewport_info(ctx)
                Input.set_game_viewport_origin(vp.image_min_x, vp.image_min_y)

                # ── Push screen-space UI commands to GPU renderer ──
                self._render_screen_ui(ctx, vp.image_min_x, vp.image_min_y,
                                       float(draw_w), float(draw_h),
                                       vp.image_min_x, vp.image_min_y,
                                       vp.image_min_x + float(draw_w),
                                       vp.image_min_y + float(draw_h))

            else:
                # No game camera — show helper message
                ctx.label("")
                ctx.label("  No Camera")
                ctx.label("  场景中没有 Camera 组件")
                ctx.label("")
                ctx.label("  请在场景中创建一个 GameObject")
                ctx.label("  并添加 Camera 组件以启用 Game View")

            # Track whether the Game window is hovered so Input only fires
            # when the user clicks inside the Game View, not the editor.
            game_hovered = ctx.is_window_hovered()
            Input.set_game_focused(game_hovered and self._is_playing())
        ctx.end_window()

    # ------------------------------------------------------------------
    # Screen-space UI overlay
    # ------------------------------------------------------------------

    def _render_screen_ui(self, ctx: InfGUIContext, vp_x: float, vp_y: float,
                          vp_w: float, vp_h: float,
                          clip_min_x: float = 0.0, clip_min_y: float = 0.0,
                          clip_max_x: float = 1e9, clip_max_y: float = 1e9):
        """Push screen-space UI commands to the GPU ScreenUI renderer.

        Commands are accumulated during BuildFrame and rendered inside the
        scene render graph as proper Vulkan passes:
        - CameraOverlay elements go to the Camera list (before post-process)
        - ScreenOverlay elements go to the Overlay list (after post-process)
        """
        from InfEngine.lib import SceneManager, ScreenUIList
        from InfEngine.ui import UICanvas, UIText
        from InfEngine.ui.enums import RenderMode

        if not self._engine:
            return

        renderer = self._engine.get_screen_ui_renderer()
        if renderer is None:
            return

        scene = SceneManager.instance().get_active_scene()
        if scene is None:
            return

        canvases = []
        for root in scene.get_root_objects():
            self._collect_canvases(root, canvases)

        if not canvases:
            return

        # Tell the renderer frame dimensions (uses game render target size,
        # not the ImGui viewport — coordinates are in game-resolution space).
        game_w = self._last_game_width
        game_h = self._last_game_height
        if game_w < 1 or game_h < 1:
            return
        renderer.begin_frame(game_w, game_h)

        # Sort by sort_order
        canvases.sort(key=lambda c: c.sort_order)

        for canvas in canvases:
            # Map RenderMode to ScreenUIList
            if canvas.render_mode == RenderMode.CameraOverlay:
                ui_list = ScreenUIList.Camera
            elif canvas.render_mode == RenderMode.ScreenOverlay:
                ui_list = ScreenUIList.Overlay
            else:
                continue

            ref_w = float(canvas.reference_width)
            ref_h = float(canvas.reference_height)
            if ref_w < 1 or ref_h < 1:
                continue
            # Scale from design resolution to actual game resolution
            scale_x = float(game_w) / ref_w
            scale_y = float(game_h) / ref_h

            for elem in canvas.iter_ui_elements():
                ex, ey, ew, eh = elem.get_rect()
                sx = ex * scale_x
                sy = ey * scale_y
                sw = ew * scale_x
                sh = eh * scale_y

                if isinstance(elem, UIText):
                    font_size = max(6.0, elem.font_size * scale_y)
                    from InfEngine.ui.enums import TextAlignH, TextAlignV
                    ah = getattr(elem, 'text_align_h', TextAlignH.Left)
                    av = getattr(elem, 'text_align_v', TextAlignV.Top)
                    ax = 0.0 if ah == TextAlignH.Left else (0.5 if ah == TextAlignH.Center else 1.0)
                    ay = 0.0 if av == TextAlignV.Top else (0.5 if av == TextAlignV.Center else 1.0)
                    cr, cg, cb, ca = elem.color
                    renderer.add_text(
                        ui_list,
                        sx, sy, sx + sw, sy + sh,
                        elem.text,
                        cr, cg, cb, ca,
                        ax, ay, font_size)

    @staticmethod
    def _collect_canvases(go, out):
        from InfEngine.ui import UICanvas
        for comp in go.get_py_components():
            if isinstance(comp, UICanvas):
                out.append(comp)
        for child in go.get_children():
            GameViewPanel._collect_canvases(child, out)