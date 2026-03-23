"""
Unity-style status bar — a fixed, non-draggable bar at the very bottom of the
editor window that shows the latest console log entry and error/warning counts.
"""

from InfEngine.lib import InfGUIRenderable, InfGUIContext
from .theme import Theme, ImGuiCol, ImGuiStyleVar
from .engine_status import EngineStatus

# ── ImGui window flags ────────────────────────────────────────────────────────
_FLAGS = Theme.WINDOW_FLAGS_NO_DECOR

_BASE_HEIGHT = 24.0     # base pixel height (scaled by DPI at render time)


class StatusBarPanel(InfGUIRenderable):
    """
    Fixed-position status bar rendered at the very bottom of the display.

    Subscribes to the DebugConsole — same filter as ConsolePanel — so only
    user-visible messages appear here.

    Wire to ConsolePanel after creation::

        status_bar.set_console_panel(console)
    """

    # Aliases to the central Theme palette
    _CLR_TEXT  = Theme.LOG_INFO
    _CLR_WARN  = Theme.LOG_WARNING
    _CLR_ERROR = Theme.LOG_ERROR
    _CLR_BG    = Theme.STATUS_BAR_BG
    _CLR_DIM   = Theme.LOG_DIM

    def __init__(self):
        super().__init__()
        self._latest_msg: str = ""
        self._latest_level: str = "info"
        self._latest_source_file: str = ""
        self._latest_source_line: int = 0
        self._warn_count: int = 0
        self._error_count: int = 0
        self._console_panel = None
        self._register_debug_listener()

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def set_console_panel(self, console_panel) -> None:
        """Wire to the ConsolePanel so the bar can mirror its clear action."""
        self._console_panel = console_panel

    def clear_counts(self) -> None:
        """Reset warning/error counters (called when Console is cleared)."""
        self._warn_count = 0
        self._error_count = 0
        self._latest_msg = ""
        self._latest_level = "info"
        self._latest_source_file = ""
        self._latest_source_line = 0

    # ------------------------------------------------------------------
    # Debug listener
    # ------------------------------------------------------------------

    def _register_debug_listener(self) -> None:
        from InfEngine.debug import DebugConsole
        console = DebugConsole.instance()
        for entry in console.get_entries():
            self._process_entry(entry)
        console.add_listener(self._process_entry)

    def _process_entry(self, entry) -> None:
        from InfEngine.debug import LogType
        from .console_panel import ConsolePanel

        if ConsolePanel._is_internal(entry):
            return

        msg = ConsolePanel._sanitize_text(getattr(entry, 'message', ''))

        level_map = {
            LogType.LOG:       "info",
            LogType.WARNING:   "warning",
            LogType.ERROR:     "error",
            LogType.ASSERT:    "error",
            LogType.EXCEPTION: "error",
        }
        level = level_map.get(entry.log_type, "info")

        self._latest_msg = msg
        self._latest_level = level
        self._latest_source_file = ConsolePanel._sanitize_text(getattr(entry, "source_file", ""))
        self._latest_source_line = getattr(entry, "source_line", 0)

        if level == "warning":
            self._warn_count += 1
        elif level == "error":
            self._error_count += 1

    # ------------------------------------------------------------------
    # Rendering
    # ------------------------------------------------------------------

    def on_render(self, ctx: InfGUIContext) -> None:
        x0, y0, disp_w, disp_h = ctx.get_main_viewport_bounds()

        if disp_w <= 0 or disp_h <= 0:
            return

        _HEIGHT = _BASE_HEIGHT * ctx.get_dpi_scale()

        # Pin to bottom edge every frame (ImGuiCond_Always, pivot = (0,0))
        ctx.set_next_window_pos(x0, y0 + disp_h - _HEIGHT, Theme.COND_ALWAYS, 0.0, 0.0)
        ctx.set_next_window_size(disp_w, _HEIGHT, Theme.COND_ALWAYS)

        # Style overrides that affect the window chrome (must be before Begin)
        ctx.push_style_color(ImGuiCol.WindowBg, *Theme.STATUS_BAR_BG)
        ctx.push_style_var_float(ImGuiStyleVar.WindowBorderSize, Theme.BORDER_SIZE_NONE)
        ctx.push_style_var_vec2(ImGuiStyleVar.WindowPadding, *Theme.STATUS_BAR_WIN_PAD)
        ctx.push_style_var_vec2(ImGuiStyleVar.ItemSpacing, *Theme.STATUS_BAR_ITEM_SPC)
        ctx.push_style_var_vec2(ImGuiStyleVar.FramePadding, *Theme.STATUS_BAR_FRAME_PAD)

        visible, _ = ctx.begin_window_closable("##InfStatusBar", True, _FLAGS)
        if visible:
            self._render_content(ctx, disp_w)
        ctx.end_window()

        ctx.pop_style_var(4)
        ctx.pop_style_color(1)

    def _render_content(self, ctx: InfGUIContext, disp_w: float) -> None:
        # ── Compute region split (3/4 left, 1/4 right) ───────────────
        status_text, status_progress = EngineStatus.get()
        status_active = bool(status_text)
        # When progress is active, strictly reserve the right 1/4 for it.
        # Otherwise the console zone uses the full width.
        if status_active:
            left_zone_w = disp_w * (1.0 - Theme.STATUS_PROGRESS_FRACTION)
        else:
            left_zone_w = disp_w

        bar_h = _BASE_HEIGHT * ctx.get_dpi_scale() - 8.0

        # ── Left zone: clickable area → opens console ────────────────
        # Span the entire left zone so clicks anywhere open the console.
        click_w = max(left_zone_w - 8.0, 100.0)
        Theme.push_status_bar_button_style(ctx)  # 3 colours
        if ctx.invisible_button("##StatusBarClick", click_w, bar_h):
            if self._console_panel is not None:
                self._console_panel.select_latest_entry()
        ctx.pop_style_color(3)

        # Overlay: draw text on top of the invisible button at left edge
        ctx.same_line(6.0)

        # ── Left: level icon + message ───────────────────────────────
        clr = self._level_color()
        ctx.push_style_color(ImGuiCol.Text, *clr)   # level colour

        if self._latest_level == "error":
            icon = "● "
        elif self._latest_level == "warning":
            icon = "▲ "
        else:
            icon = ""

        # Show only the first line; truncate if still too long for the bar
        msg = self._latest_msg.split('\n', 1)[0]
        max_chars = max(10, int((left_zone_w - 160) / 8))
        if len(msg) > max_chars:
            msg = msg[:max_chars - 1] + "…"

        ctx.label(icon + msg)
        ctx.pop_style_color(1)

        # ── Right counters (within left zone): [W] N  [E] N ─────────────
        counter_x = left_zone_w - 130.0
        if counter_x > 0:
            ctx.same_line(counter_x)

            if self._warn_count > 0:
                ctx.push_style_color(ImGuiCol.Text, *Theme.LOG_WARNING)
                ctx.label(f"{Theme.ICON_WARNING} {self._warn_count}")
                ctx.pop_style_color(1)
                ctx.same_line(0, 12)
            else:
                ctx.push_style_color(ImGuiCol.Text, *Theme.LOG_DIM)
                ctx.label(f"{Theme.ICON_WARNING} 0")
                ctx.pop_style_color(1)
                ctx.same_line(0, 12)

            if self._error_count > 0:
                ctx.push_style_color(ImGuiCol.Text, *Theme.LOG_ERROR)
                ctx.label(f"{Theme.ICON_ERROR} {self._error_count}")
                ctx.pop_style_color(1)
            else:
                ctx.push_style_color(ImGuiCol.Text, *Theme.LOG_DIM)
                ctx.label(f"{Theme.ICON_ERROR} 0")
                ctx.pop_style_color(1)

        # \u2500\u2500 Right zone: engine status + progress \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
        if status_active:
            self._render_engine_status(ctx, disp_w, left_zone_w, status_text, status_progress)

    def _render_engine_status(self, ctx: InfGUIContext,
                              disp_w: float, left_zone_w: float,
                              text: str, progress: float) -> None:
        """Render engine-activity indicator in the right portion of the bar."""
        # Position to the right zone
        ctx.same_line(left_zone_w + 8.0)

        zone_w = disp_w - left_zone_w - 16.0

        # If determinate progress (0..1), show progress bar first then text
        if progress >= 0.0:
            bar_w = min(zone_w * 0.4, 80.0)
            ctx.push_style_color(ImGuiCol.FrameBg, *Theme.STATUS_PROGRESS_BG)
            ctx.push_style_color(ImGuiCol.PlotHistogram, *Theme.STATUS_PROGRESS_CLR)
            ctx.set_next_item_width(bar_w)
            ctx.progress_bar(min(max(progress, 0.0), 1.0), bar_w, 0.0, "")
            ctx.pop_style_color(2)
            ctx.same_line(0, 6.0)

        # Truncate text
        remaining_w = zone_w - (90.0 if progress >= 0.0 else 0.0)
        max_chars = max(6, int(remaining_w / 8))
        if len(text) > max_chars:
            text = text[:max_chars - 1] + "\u2026"

        ctx.push_style_color(ImGuiCol.Text, *Theme.STATUS_PROGRESS_LABEL_CLR)
        ctx.label(text)
        ctx.pop_style_color(1)

    def _level_color(self):
        if self._latest_level == "error":
            return self._CLR_ERROR
        if self._latest_level == "warning":
            return self._CLR_WARN
        return self._CLR_TEXT
