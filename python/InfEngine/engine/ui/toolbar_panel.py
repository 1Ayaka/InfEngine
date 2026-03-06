"""Compact Notion-style toolbar above the scene viewport."""

from typing import Optional, TYPE_CHECKING
from InfEngine.lib import InfGUIRenderable, InfGUIContext
from InfEngine.engine.play_mode import PlayModeManager, PlayModeState
from .closable_panel import ClosablePanel
from .theme import Theme, ImGuiCol, ImGuiStyleVar

if TYPE_CHECKING:
    from InfEngine.engine import Engine


class ToolbarPanel(ClosablePanel):
    WINDOW_TYPE_ID = "toolbar"
    WINDOW_DISPLAY_NAME = "工具栏 Toolbar"

    def __init__(self, title: str = "工具栏 Toolbar", engine: 'Engine' = None,
                 play_mode_manager: Optional[PlayModeManager] = None):
        super().__init__(title, window_id="toolbar")
        self._engine = engine
        self._play_mode_manager = play_mode_manager
        if self._engine and self._play_mode_manager is None:
            self._play_mode_manager = self._engine.get_play_mode_manager()

    def set_engine(self, engine: 'Engine'):
        self._engine = engine
        if engine:
            self._play_mode_manager = engine.get_play_mode_manager()

    def set_play_mode_manager(self, manager: PlayModeManager):
        self._play_mode_manager = manager

    # ------------------------------------------------------------------
    def on_render(self, ctx: InfGUIContext):
        if not self._is_open:
            return

        # NoScrollbar | NoScrollWithMouse
        flags = Theme.WINDOW_FLAGS_NO_SCROLL
        ctx.set_next_window_size(800, 32, Theme.COND_FIRST_USE_EVER)

        # Compact padding for the whole toolbar
        Theme.push_toolbar_vars(ctx)  # 5 style vars

        if self._begin_closable_window(ctx, flags):
            win_w = ctx.get_window_width()
            state = self._get_state()
            is_playing = state in (PlayModeState.PLAYING, PlayModeState.PAUSED)
            is_paused  = state == PlayModeState.PAUSED

            # --- Right-aligned dropdowns (measure from right edge) ---
            right_x = win_w - 200.0
            if right_x < 300.0:
                right_x = 300.0

            # --- Centered play controls ---
            btn_w = 160.0
            cx = (win_w - btn_w) * 0.5
            if cx < 6.0:
                cx = 6.0
            ctx.set_cursor_pos_x(cx)

            # Play / Stop
            if is_playing and not is_paused:
                Theme.push_flat_button_style(ctx, *Theme.PLAY_ACTIVE)
            else:
                Theme.push_flat_button_style(ctx, *Theme.BTN_IDLE)
            ctx.button("Stop" if is_playing else "Play", self._on_play)
            ctx.pop_style_color(3)

            ctx.same_line(0, 2)

            # Pause / Resume
            if not is_playing:
                Theme.push_flat_button_style(ctx, *Theme.BTN_DISABLED)
            elif is_paused:
                Theme.push_flat_button_style(ctx, *Theme.PAUSE_ACTIVE)
            else:
                Theme.push_flat_button_style(ctx, *Theme.BTN_IDLE)
            ctx.button("Resume" if is_paused else "Pause",
                       self._on_pause if is_playing else lambda: None)
            ctx.pop_style_color(3)

            ctx.same_line(0, 2)

            # Step
            if is_paused:
                Theme.push_flat_button_style(ctx, *Theme.BTN_IDLE)
            else:
                Theme.push_flat_button_style(ctx, *Theme.BTN_DISABLED)
            ctx.button("Step", self._on_step if is_paused else lambda: None)
            ctx.pop_style_color(3)

            # Time label while playing
            if is_playing:
                ctx.same_line(0, 8)
                tag = "PAUSED" if is_paused else "PLAYING"
                ctx.label(f"{tag}  {self._time_str()}")

            # --- Right dropdowns ---
            ctx.same_line(right_x)
            self._ghost_btn(ctx, "Gizmos  v", "##giz")
            if ctx.begin_popup("##giz"):
                Theme.push_popup_vars(ctx)  # 3 style vars
                self._popup_gizmos(ctx)
                ctx.pop_style_var(3)
                ctx.end_popup()

            ctx.same_line(0, 4)
            self._ghost_btn(ctx, "Camera  v", "##cam")
            if ctx.begin_popup("##cam"):
                Theme.push_popup_vars(ctx)  # 3 style vars
                self._popup_camera(ctx)
                ctx.pop_style_var(3)
                ctx.end_popup()

        ctx.end_window()
        ctx.pop_style_var(5)

    # ---------- ghost button (Notion-style: transparent bg, subtle hover) ----
    @staticmethod
    def _ghost_btn(ctx: InfGUIContext, label: str, popup_id: str):
        Theme.push_ghost_button_style(ctx)
        ctx.button(label, lambda pid=popup_id: ctx.open_popup(pid))
        ctx.pop_style_color(3)

    # ---------- flat button helper ----
    @staticmethod
    def _flat_btn(ctx: InfGUIContext, base: tuple):
        Theme.push_flat_button_style(ctx, *base)

    # ---------- popup contents ----
    def _popup_gizmos(self, ctx: InfGUIContext):
        if not self._engine:
            ctx.label("Engine not available")
            return
        ctx.dummy(200, 0)  # force minimum popup width
        ctx.label("Gizmos")
        ctx.separator()
        ctx.dummy(0, 4)
        g = self._engine.is_show_grid()
        ng = ctx.checkbox("Show Grid", g)
        if ng != g:
            self._engine.set_show_grid(ng)
        ctx.dummy(0, 4)

    def _popup_camera(self, ctx: InfGUIContext):
        if not self._engine:
            ctx.label("Engine not available")
            return
        ctx.dummy(240, 0)  # force minimum popup width
        ctx.label("Scene Camera")
        ctx.separator()
        ctx.dummy(0, 4)
        fov = self._engine.get_editor_camera_fov()
        ctx.label("Field of View")
        ctx.same_line(120)
        ctx.set_next_item_width(120)
        nf = ctx.float_slider("##fov", fov, 10.0, 120.0)
        if abs(nf - fov) > 0.1:
            self._engine.set_editor_camera_fov(nf)
        ctx.dummy(0, 4)

    # ---------- state helpers ----
    def _get_state(self) -> PlayModeState:
        return self._play_mode_manager.state if self._play_mode_manager else PlayModeState.EDIT

    def _time_str(self) -> str:
        if not self._play_mode_manager:
            return "00:00.000"
        t = self._play_mode_manager.total_play_time
        return f"{int(t//60):02d}:{t%60:06.3f}"

    def _on_play(self):
        if not self._play_mode_manager:
            return
        if self._play_mode_manager.is_playing:
            self._play_mode_manager.exit_play_mode()
        else:
            self._play_mode_manager.enter_play_mode()

    def _on_pause(self):
        if not self._play_mode_manager:
            return
        self._play_mode_manager.toggle_pause()

    def _on_step(self):
        if not self._play_mode_manager:
            return
        self._play_mode_manager.step_frame()
