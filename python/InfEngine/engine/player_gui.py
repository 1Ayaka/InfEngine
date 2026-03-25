"""
PlayerGUI — fullscreen-borderless ImGui GUI for standalone game playback.

Registered as a single InfGUIRenderable that fills the entire window with
the game camera render target.  No editor chrome, no docking, no menus.

Optionally shows a **splash sequence** before revealing the game.  During
splash the game scene loads and starts in the background; when the sequence
finishes the game view is made visible instantly.
"""

from __future__ import annotations

import time
from typing import Dict, List, Optional

from InfEngine.lib import InfGUIRenderable, InfGUIContext
from InfEngine.input import Input, KeyCode
from InfEngine.engine.ui.viewport_utils import capture_viewport_info
from InfEngine.ui.ui_texture_cache import get_shared_cache as _get_tex_cache
from InfEngine.ui.ui_render_dispatch import dispatch as _ui_dispatch
from InfEngine.ui.ui_event_system import UIEventProcessor
from InfEngine.ui.ui_canvas_utils import collect_sorted_canvases
from InfEngine.ui.inf_ui_screen_component import clear_rect_cache


class PlayerGUI(InfGUIRenderable):
    """Renders the game camera output fullscreen with screen-space UI overlay."""

    def __init__(self, engine, *,
                 splash_items: Optional[List[Dict]] = None,
                 data_root: str = ""):
        super().__init__()
        self._engine = engine
        self._last_w = 0
        self._last_h = 0
        self._ui_event_processor = UIEventProcessor()
        self._last_frame_time = time.time()

        # Splash
        self._splash = None
        if splash_items:
            from InfEngine.engine.splash_player import SplashPlayer
            self._splash = SplashPlayer(splash_items, data_root)

    # ------------------------------------------------------------------
    # InfGUIRenderable interface
    # ------------------------------------------------------------------

    def on_render(self, ctx: InfGUIContext):
        # Per-frame tick (play-mode timing + deferred tasks) — always run,
        # even during splash so the game world initialises behind the scenes.
        self._tick(ctx)

        # Full main viewport
        x0, y0, vp_w, vp_h = ctx.get_main_viewport_bounds()
        ctx.set_next_window_pos(x0, y0, 0, 0.0, 0.0)
        ctx.set_next_window_size(vp_w, vp_h, 0)

        # ImGui flags: NoTitleBar|NoResize|NoMove|NoScrollbar|NoCollapse
        #              |NoSavedSettings|NoNavInputs|NoNavFocus|NoDocking
        flags = (
            (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 5)
            | (1 << 8) | (1 << 16) | (1 << 17) | (1 << 19)
        )
        ctx.push_style_var_vec2(2, 0.0, 0.0)   # WindowPadding = (0,0)
        ctx.push_style_var_float(4, 0.0)        # WindowBorderSize = 0

        # ── Splash mode ───────────────────────────────────────────────
        if self._splash and not self._splash.is_finished:
            # Black background during splash
            ctx.push_style_color(2, 0.0, 0.0, 0.0, 1.0)  # ImGuiCol_WindowBg
            visible = ctx.begin_window("##PlayerFullscreen", True, flags)
            if visible:
                native = self._engine.get_native_engine()
                if native:
                    self._splash.update(ctx, native, x0, y0, vp_w, vp_h)
            ctx.end_window()
            ctx.pop_style_color(1)
            ctx.pop_style_var(2)

            if self._splash.is_finished:
                native = self._engine.get_native_engine()
                if native:
                    self._splash.cleanup(native)
                self._splash = None
            return

        # ── Normal game mode ──────────────────────────────────────────
        visible = ctx.begin_window("##PlayerFullscreen", True, flags)
        if visible:
            self._render_game(ctx, vp_w, vp_h)
        ctx.end_window()
        ctx.pop_style_var(2)  # WindowPadding + WindowBorderSize

    # ------------------------------------------------------------------

    def _tick(self, ctx):
        """Drive play-mode timing and deferred tasks each frame."""
        # DeferredTaskRunner is now ticked by InfRenderer's pre-GUI callback
        # (before BuildFrame) so scene mutations complete before panels render.

        if self._engine:
            # In player mode there's no MenuBarPanel, so we must handle
            # close requests (Alt+F4 / window X) directly.
            native = self._engine.get_native_engine()
            if native and native.is_close_requested():
                native.confirm_close()
                return
            self._engine.tick_play_mode()

    def _render_game(self, ctx: InfGUIContext, vp_w: float, vp_h: float):
        target_w = max(1, int(vp_w))
        target_h = max(1, int(vp_h))

        if target_w != self._last_w or target_h != self._last_h:
            self._engine.resize_game_render_target(target_w, target_h)
            self._last_w = target_w
            self._last_h = target_h

        game_tex = self._engine.get_game_texture_id()
        if game_tex == 0:
            ctx.label("Waiting for camera...")
            return

        ctx.image(game_tex, float(target_w), float(target_h), 0.0, 0.0, 1.0, 1.0)
        vp = capture_viewport_info(ctx)
        Input.set_game_viewport_origin(vp.image_min_x, vp.image_min_y)

        # Screen-space UI overlay
        self._render_screen_ui(ctx, vp.image_min_x, vp.image_min_y,
                               float(target_w), float(target_h))

        # Input: always game-focused in player mode.
        # Cursor lock is script-driven (Input.set_cursor_locked).
        game_hovered = ctx.is_window_hovered()

        # ESC safety: allow user to unlock cursor even if scripts forgot
        cursor_locked = Input.is_cursor_locked()
        if cursor_locked:
            if Input.get_key_down(KeyCode.ESCAPE):
                Input.set_cursor_locked(False)
                cursor_locked = False

        Input.set_game_focused(
            game_hovered or cursor_locked
        )

        # Process UI events
        if game_hovered:
            self._process_ui_events(target_w, target_h)
        else:
            self._ui_event_processor.reset()

    # ------------------------------------------------------------------
    # Screen-space UI (same as GameViewPanel but simplified)
    # ------------------------------------------------------------------

    def _render_screen_ui(self, ctx: InfGUIContext, vp_x: float, vp_y: float,
                          vp_w: float, vp_h: float):
        from InfEngine.lib import SceneManager, ScreenUIList
        from InfEngine.ui.enums import RenderMode

        if not self._engine:
            return

        renderer = self._engine.get_screen_ui_renderer()
        if renderer is None:
            return

        scene = SceneManager.instance().get_active_scene()
        if scene is None:
            return

        game_w = self._last_w
        game_h = self._last_h
        if game_w < 1 or game_h < 1:
            return

        canvases = collect_sorted_canvases(scene, allow_stale_empty=True)
        if canvases:
            clear_rect_cache(time.perf_counter())

        renderer.begin_frame(game_w, game_h)
        if not canvases:
            return
        use_overlay = not renderer.is_enabled()

        for canvas in canvases:
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

            scale_x = float(game_w) / ref_w
            scale_y = float(game_h) / ref_h

            _tex_cache = _get_tex_cache()
            _get_tid = lambda tp: _tex_cache.get(self._engine, tp)

            for elem in canvas._get_elements():
                ex, ey, ew, eh = elem.get_rect(ref_w, ref_h)

                if use_overlay:
                    ovl_scale_x = vp_w / ref_w
                    ovl_scale_y = vp_h / ref_h
                    base_sx = vp_x + ex * ovl_scale_x
                    base_sy = vp_y + ey * ovl_scale_y
                    base_sw = ew * ovl_scale_x
                    base_sh = eh * ovl_scale_y
                    ovl_zoom = min(ovl_scale_x, ovl_scale_y)
                    _ui_dispatch(
                        elem, "editor",
                        ctx=ctx,
                        base_sx=base_sx, base_sy=base_sy,
                        base_sw=base_sw, base_sh=base_sh,
                        zoom=ovl_zoom,
                        get_tex_id=_get_tid,
                    )
                else:
                    sx = ex * scale_x
                    sy = ey * scale_y
                    sw = ew * scale_x
                    sh = eh * scale_y
                    text_scale = min(scale_x, scale_y)
                    _ui_dispatch(
                        elem, "runtime",
                        renderer=renderer,
                        ui_list=ui_list,
                        sx=sx, sy=sy, sw=sw, sh=sh,
                        ref_w=ref_w, ref_h=ref_h,
                        scale_x=scale_x, scale_y=scale_y,
                        text_scale=text_scale,
                        get_tex_id=_get_tid,
                    )

    def _process_ui_events(self, game_w: int, game_h: int):
        """Convert Input mouse state to per-canvas pointer events."""
        from InfEngine.lib import SceneManager

        scene = SceneManager.instance().get_active_scene()
        if scene is None:
            return

        canvases = collect_sorted_canvases(scene, allow_stale_empty=True)
        if not canvases:
            self._ui_event_processor.reset()
            return

        # Mouse position in viewport pixels (relative to game image top-left).
        # In player mode display_scale is 1.0 (render target == viewport).
        gx, gy, scroll_x, scroll_y, mouse_held, mouse_down, mouse_up = Input.get_game_mouse_frame_state(0)

        # Build per-canvas positions in design (canvas) pixels
        canvas_positions = []
        for canvas in canvases:
            ref_w = float(canvas.reference_width)
            ref_h = float(canvas.reference_height)
            if ref_w < 1 or ref_h < 1:
                canvas_positions.append((0.0, 0.0))
                continue
            cx = gx * ref_w / float(game_w)
            cy = gy * ref_h / float(game_h)
            canvas_positions.append((cx, cy))

        scroll = (scroll_x, scroll_y)

        from InfEngine.timing import Time
        dt = Time.unscaled_delta_time

        self._ui_event_processor.process(
            canvases, canvas_positions,
            mouse_down, mouse_up, mouse_held,
            scroll, dt,
        )
