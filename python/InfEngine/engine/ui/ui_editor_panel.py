"""UI Editor panel — Figma-style 2D canvas editor for screen-space UI layout.

Displays the selected UICanvas at its reference resolution and lets users
visually position UI elements via drag.  Max zoom is 100% (1:1 pixels).

Docked alongside Scene / Game views.
"""

from typing import Optional
from InfEngine.lib import InfGUIContext
from .closable_panel import ClosablePanel
from .theme import Theme, ImGuiCol, ImGuiStyleVar
from .ui_editor_shortcuts import UIEditorInput


# ── Constants ─────────────────────────────────────────────────────────
_CANVAS_BG = (0.18, 0.18, 0.20, 1.0)        # Dark grey canvas background
_CANVAS_BORDER = (0.45, 0.45, 0.50, 1.0)     # Canvas border
_ELEMENT_HOVER = (0.26, 0.59, 0.98, 0.30)    # Hovered element fill
_ELEMENT_SELECT = (0.26, 0.59, 0.98, 0.60)   # Selected element fill
_ELEMENT_SELECT_BORDER = (0.26, 0.59, 0.98, 1.0)
_HANDLE_COLOR = (1.0, 1.0, 1.0, 0.9)
_HANDLE_SIZE = 5.0                            # Half-size of resize handle
_TOOLBAR_HEIGHT = 32.0
_MIN_ZOOM = 0.05
_MAX_ZOOM = 1.0                               # Cap at 100% (1:1 pixels)


class UIEditorPanel(ClosablePanel):
    """Figma-style 2D UI editor panel."""

    WINDOW_TYPE_ID = "ui_editor"
    WINDOW_DISPLAY_NAME = "UI编辑器 UI Editor"

    def __init__(self, title: str = "UI编辑器 UI Editor"):
        super().__init__(title, window_id="ui_editor")

        # ── Canvas navigation ──
        self._zoom: float = 1.0
        self._pan_x: float = 0.0       # Pan offset in screen pixels
        self._pan_y: float = 0.0
        self._is_panning: bool = False

        # ── Selection state ──
        self._selected_element_comp = None   # Currently selected screen-space UI component
        self._dragging: bool = False
        self._drag_start_x: float = 0.0
        self._drag_start_y: float = 0.0
        self._drag_elem_start_x: float = 0.0
        self._drag_elem_start_y: float = 0.0

        # ── Resize handle state ──
        self._resizing: bool = False
        self._resize_handle_idx: int = -1     # Which handle is being dragged
        self._resize_start_mx: float = 0.0    # Mouse pos at resize start (screen)
        self._resize_start_my: float = 0.0
        self._resize_start_rect = (0.0, 0.0, 0.0, 0.0)  # (x, y, w, h) at start

        # ── External references ──
        self._engine = None                  # Engine instance (for game texture)
        self._on_selection_changed = None    # Callback(go_or_None)
        self._hierarchy_panel = None
        self._on_request_ui_mode = None      # Callback(bool) to toggle hierarchy UI mode

        # ── Background mode ──
        self._bg_mode: int = 0               # 0 = solid colour, 1 = Game camera

        # ── Focus tracking ──
        self._was_focused: bool = False

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def set_on_selection_changed(self, callback):
        """Set callback when a UI element is selected.  Receives the GameObject."""
        self._on_selection_changed = callback

    def set_hierarchy_panel(self, panel):
        self._hierarchy_panel = panel

    def set_engine(self, engine):
        """Set engine instance (needed for Game background mode)."""
        self._engine = engine

    def set_on_request_ui_mode(self, callback):
        """callback(enter: bool) — ask hierarchy to enter/exit UI mode."""
        self._on_request_ui_mode = callback

    # ------------------------------------------------------------------
    # Helpers — canvas / element discovery
    # ------------------------------------------------------------------

    def _get_all_canvases(self):
        """Return list of (GameObject, UICanvas) for every Canvas in the scene."""
        from InfEngine.lib import SceneManager
        from InfEngine.ui import UICanvas
        scene = SceneManager.instance().get_active_scene()
        if scene is None:
            return []
        result = []
        for root in scene.get_root_objects():
            self._collect_canvases(root, result)
        return result

    def _collect_canvases(self, go, out):
        from InfEngine.ui import UICanvas
        for comp in go.get_py_components():
            if isinstance(comp, UICanvas):
                out.append((go, comp))
        for child in go.get_children():
            self._collect_canvases(child, out)

    def _get_active_canvas(self):
        """Return (go, UICanvas) for the first canvas, or (None, None)."""
        canvases = self._get_all_canvases()
        if not canvases:
            return None, None
        # If hierarchy has a selected object, prefer canvas that is ancestor
        if self._hierarchy_panel:
            sel_id = getattr(self._hierarchy_panel, '_selected_object_id', 0)
            if sel_id:
                for go, canvas in canvases:
                    if self._is_descendant_of(sel_id, go):
                        return go, canvas
        return canvases[0]

    def _is_descendant_of(self, obj_id, ancestor_go):
        """Check if obj_id is the ancestor or one of its descendants."""
        if ancestor_go.id == obj_id:
            return True
        for child in ancestor_go.get_children():
            if self._is_descendant_of(obj_id, child):
                return True
        return False

    # ------------------------------------------------------------------
    # Coordinate transforms
    # ------------------------------------------------------------------

    def _canvas_to_screen(self, cx, cy, origin_x, origin_y):
        """Canvas-space (pixels in reference resolution) → screen-space (window coords)."""
        return (origin_x + cx * self._zoom + self._pan_x,
                origin_y + cy * self._zoom + self._pan_y)

    def _screen_to_canvas(self, sx, sy, origin_x, origin_y):
        """Screen-space → canvas-space."""
        return ((sx - origin_x - self._pan_x) / self._zoom,
                (sy - origin_y - self._pan_y) / self._zoom)

    # ------------------------------------------------------------------
    # Rendering
    # ------------------------------------------------------------------

    def on_render(self, ctx: InfGUIContext):
        if not self._is_open:
            return

        ctx.set_next_window_size(800, 600, Theme.COND_FIRST_USE_EVER)
        window_flags = Theme.WINDOW_FLAGS_VIEWPORT | Theme.WINDOW_FLAGS_NO_SCROLL

        if self._begin_closable_window(ctx, window_flags):
            # ── Focus tracking → request hierarchy UI mode ──
            focused = ctx.is_window_focused(0)
            if focused and not self._was_focused:
                if self._on_request_ui_mode:
                    self._on_request_ui_mode(True)
            self._was_focused = focused

            canvas_go, canvas = self._get_active_canvas()
            if canvas is None:
                self._render_no_canvas(ctx)
            else:
                self._render_toolbar(ctx, canvas_go, canvas)
                self._render_canvas_area(ctx, canvas_go, canvas)
        ctx.end_window()

    # ── No Canvas placeholder ────────────────────────────────────────

    def _render_no_canvas(self, ctx: InfGUIContext):
        ctx.label("")
        ctx.label("  场景中没有 Canvas")
        ctx.label("  No UICanvas in scene")
        ctx.label("")
        ctx.label("  在 Hierarchy 中右键 → 创建 → UI → Canvas")
        ctx.label("  或使用下方按钮快速创建")
        ctx.label("")
        ctx.button("创建 Canvas  Create Canvas", self._create_canvas, width=220, height=28)

    # ── Toolbar ──────────────────────────────────────────────────────

    def _render_toolbar(self, ctx: InfGUIContext, canvas_go, canvas):
        """Top toolbar with creation buttons, zoom control, and background toggle."""
        ctx.label(f"Canvas: {canvas_go.name}")
        ctx.same_line(0, 16)

        ctx.button("T 文本", lambda: self._create_text_element(canvas_go), width=64)

        ctx.same_line(0, 8)
        zoom_pct = int(self._zoom * 100)
        ctx.label(f"缩放: {zoom_pct}%")
        ctx.same_line(0, 8)
        ctx.button("适应 Fit", lambda: self._fit_zoom(ctx, canvas), width=56)

        # Background mode toggle
        ctx.same_line(0, 16)
        _BG_LABELS = ["Solid", "Game"]
        ctx.set_next_item_width(64)
        self._bg_mode = ctx.combo("##BG", self._bg_mode, _BG_LABELS, -1)

        ctx.separator()

    # ── Canvas area ──────────────────────────────────────────────────

    def _render_canvas_area(self, ctx: InfGUIContext, canvas_go, canvas):
        """Main area: zoomable canvas with UI element previews."""
        # Content region (below toolbar)
        region_w = ctx.get_content_region_avail_width()
        region_h = ctx.get_content_region_avail_height()
        if region_w < 1 or region_h < 1:
            return

        # Use invisible button to capture input over the whole region
        ctx.invisible_button("##ui_canvas_area", region_w, region_h)
        area_hovered = ctx.is_item_hovered()

        # Window-space origin of the canvas area
        area_min_x = ctx.get_item_rect_min_x()
        area_min_y = ctx.get_item_rect_min_y()
        area_max_x = area_min_x + region_w
        area_max_y = area_min_y + region_h

        # ── Input snapshot ──
        inp = UIEditorInput(ctx, area_hovered)

        # ── Handle zoom (mouse wheel) ──
        if abs(inp.wheel_delta) > 0.01:
            old_zoom = self._zoom
            self._zoom = max(_MIN_ZOOM, min(_MAX_ZOOM, self._zoom * (1.0 + inp.wheel_delta * 0.1)))
            # Zoom towards mouse position
            factor = self._zoom / old_zoom
            self._pan_x = inp.mouse_x - area_min_x - factor * (inp.mouse_x - area_min_x - self._pan_x)
            self._pan_y = inp.mouse_y - area_min_y - factor * (inp.mouse_y - area_min_y - self._pan_y)

        # ── Handle pan (Figma-like: Space+LMB or MMB drag) ──
        if inp.wants_pan:
            if not self._is_panning:
                self._is_panning = True
            drag_btn = inp.pan_drag_button
            dx = ctx.get_mouse_drag_delta_x(drag_btn)
            dy = ctx.get_mouse_drag_delta_y(drag_btn)
            self._pan_x += dx
            self._pan_y += dy
            ctx.reset_mouse_drag_delta(drag_btn)
        else:
            self._is_panning = False

        # ── Draw canvas rectangle ──
        ref_w = float(canvas.reference_width)
        ref_h = float(canvas.reference_height)
        c_tl_x, c_tl_y = self._canvas_to_screen(0, 0, area_min_x, area_min_y)
        c_br_x, c_br_y = self._canvas_to_screen(ref_w, ref_h, area_min_x, area_min_y)

        # Snap canvas rect to integer screen pixels to prevent jitter
        c_tl_x = round(c_tl_x)
        c_tl_y = round(c_tl_y)
        c_br_x = round(c_br_x)
        c_br_y = round(c_br_y)

        # Visible (clamped) canvas rect — prevents drawing over toolbar
        v_tl_x = max(c_tl_x, area_min_x)
        v_tl_y = max(c_tl_y, area_min_y)
        v_br_x = min(c_br_x, area_max_x)
        v_br_y = min(c_br_y, area_max_y)
        canvas_visible = (v_br_x > v_tl_x and v_br_y > v_tl_y)

        # Canvas fill — either solid colour or Game camera texture
        if canvas_visible:
            if self._bg_mode == 1 and self._engine is not None:
                tex_id = self._engine.get_game_texture_id()
                if tex_id != 0:
                    # Compute UV coords for the visible portion
                    full_w = max(c_br_x - c_tl_x, 1)
                    full_h = max(c_br_y - c_tl_y, 1)
                    uv0_x = (v_tl_x - c_tl_x) / full_w
                    uv0_y = (v_tl_y - c_tl_y) / full_h
                    uv1_x = (v_br_x - c_tl_x) / full_w
                    uv1_y = (v_br_y - c_tl_y) / full_h
                    ctx.draw_image_rect(tex_id, v_tl_x, v_tl_y, v_br_x, v_br_y,
                                        uv0_x, uv0_y, uv1_x, uv1_y)
                else:
                    ctx.draw_filled_rect(v_tl_x, v_tl_y, v_br_x, v_br_y,
                                         *_CANVAS_BG, 0.0)
            else:
                ctx.draw_filled_rect(v_tl_x, v_tl_y, v_br_x, v_br_y,
                                     *_CANVAS_BG, 0.0)

        # Canvas border (clamped)
        if canvas_visible:
            ctx.draw_rect(v_tl_x, v_tl_y, v_br_x, v_br_y,
                          *_CANVAS_BORDER, 1.0, 0.0)

        # ── Draw resolution label (only if above canvas is visible) ──
        label_y = c_tl_y - 16
        if label_y >= area_min_y:
            ctx.draw_text(c_tl_x + 4, label_y,
                          f"{int(ref_w)}×{int(ref_h)}", 0.6, 0.6, 0.6, 0.7, 0.0)

        # ── Draw UI elements (clipped to canvas area) ──
        from InfEngine.ui import UIText
        elements = list(canvas.iter_ui_elements())

        # Helper to clamp a rect to the canvas area
        def _clamp(x0, y0, x1, y1):
            return (max(x0, area_min_x), max(y0, area_min_y),
                    min(x1, area_max_x), min(y1, area_max_y))

        hovered_elem = None
        for elem in elements:
            ex, ey, ew, eh = elem.get_rect()
            s_x, s_y = self._canvas_to_screen(ex, ey, area_min_x, area_min_y)
            s_w = ew * self._zoom
            s_h = eh * self._zoom

            # Snap to integer screen coords for crisp rendering
            s_x = round(s_x)
            s_y = round(s_y)
            s_w = round(s_w)
            s_h = round(s_h)

            # Hit-test (for hover/click) — only within canvas area
            is_hovered = (area_hovered and
                          s_x <= inp.mouse_x <= s_x + s_w and
                          s_y <= inp.mouse_y <= s_y + s_h)
            is_selected = (elem is self._selected_element_comp)

            if is_hovered:
                hovered_elem = elem

            # Clamp drawing rect to canvas area
            cx0, cy0, cx1, cy1 = _clamp(s_x, s_y, s_x + s_w, s_y + s_h)
            if cx1 <= cx0 or cy1 <= cy0:
                continue  # Fully outside visible area

            # Draw element background
            if is_selected:
                ctx.draw_filled_rect(cx0, cy0, cx1, cy1,
                                     *_ELEMENT_SELECT, 0.0)
                ctx.draw_rect(cx0, cy0, cx1, cy1,
                              *_ELEMENT_SELECT_BORDER, 2.0, 0.0)
            elif is_hovered:
                ctx.draw_filled_rect(cx0, cy0, cx1, cy1,
                                     *_ELEMENT_HOVER, 0.0)
                # Hover outline (Figma-style thin blue border)
                ctx.draw_rect(cx0, cy0, cx1, cy1,
                              0.26, 0.59, 0.98, 0.6, 1.0, 0.0)

            # Draw text preview (clipped to canvas area)
            if isinstance(elem, UIText):
                text_size = max(8.0, min(256.0, elem.font_size * self._zoom))
                from InfEngine.ui.enums import TextAlignH, TextAlignV
                ah = getattr(elem, 'text_align_h', TextAlignH.Left)
                av = getattr(elem, 'text_align_v', TextAlignV.Top)
                ax = 0.0 if ah == TextAlignH.Left else (0.5 if ah == TextAlignH.Center else 1.0)
                ay = 0.0 if av == TextAlignV.Top else (0.5 if av == TextAlignV.Center else 1.0)
                # Use the CLAMPED rect as clip region but the UNCLAMPED
                # element rect for alignment so text doesn't shift when
                # partially scrolled off-screen.
                ctx.push_draw_list_clip_rect(cx0, cy0, cx1, cy1)
                ctx.draw_text_aligned(
                    s_x, s_y, s_x + s_w, s_y + s_h,
                    elem.text,
                    elem.color[0], elem.color[1], elem.color[2], elem.color[3],
                    ax, ay, text_size, False)
                ctx.pop_draw_list_clip_rect()
            else:
                # Generic element — just show type name (only if visible)
                tx = max(s_x + 2, area_min_x)
                ty = max(s_y + 2, area_min_y)
                if tx < area_max_x and ty < area_max_y:
                    ctx.draw_text(tx, ty,
                                  elem.type_name, 0.7, 0.7, 0.7, 1.0, 0.0)

        # ── Draw Figma-style selection handles on selected element ──
        self._handle_positions = []  # Reset each frame
        if self._selected_element_comp is not None:
            sel = self._selected_element_comp
            sx, sy, sw, sh = sel.get_rect()
            hx, hy = self._canvas_to_screen(sx, sy, area_min_x, area_min_y)
            hw = sw * self._zoom
            hh = sh * self._zoom
            hx, hy, hw, hh = round(hx), round(hy), round(hw), round(hh)
            hs = _HANDLE_SIZE
            # 8 handles: 4 corners + 4 edge midpoints
            # Index mapping: 0=TL, 1=TR, 2=BL, 3=BR, 4=top-mid, 5=bot-mid, 6=left-mid, 7=right-mid
            self._handle_positions = [
                (hx - hs, hy - hs),                   # 0 top-left
                (hx + hw - hs, hy - hs),               # 1 top-right
                (hx - hs, hy + hh - hs),               # 2 bottom-left
                (hx + hw - hs, hy + hh - hs),          # 3 bottom-right
                (hx + hw / 2 - hs, hy - hs),           # 4 top-mid
                (hx + hw / 2 - hs, hy + hh - hs),      # 5 bottom-mid
                (hx - hs, hy + hh / 2 - hs),           # 6 left-mid
                (hx + hw - hs, hy + hh / 2 - hs),      # 7 right-mid
            ]
            for px, py in self._handle_positions:
                # Only draw handles that are within the canvas area
                h_x1 = px + hs * 2
                h_y1 = py + hs * 2
                if h_x1 > area_min_x and h_y1 > area_min_y and px < area_max_x and py < area_max_y:
                    ctx.draw_filled_rect(px, py, h_x1, h_y1,
                                         *_HANDLE_COLOR, 0.0)
                    ctx.draw_rect(px, py, h_x1, h_y1,
                                  *_ELEMENT_SELECT_BORDER, 1.0, 0.0)

        # ── Distance guides while dragging ──
        if self._dragging and self._selected_element_comp is not None:
            sel = self._selected_element_comp
            # Show distance from element to canvas edges
            sel_left = sel.x
            sel_top = sel.y
            sel_right = ref_w - (sel.x + sel.width)
            sel_bottom = ref_h - (sel.y + sel.height)

            el_sx, el_sy = self._canvas_to_screen(sel.x, sel.y, area_min_x, area_min_y)
            el_ex, el_ey = self._canvas_to_screen(sel.x + sel.width, sel.y + sel.height, area_min_x, area_min_y)
            el_sx, el_sy, el_ex, el_ey = round(el_sx), round(el_sy), round(el_ex), round(el_ey)

            guide_col = (1.0, 0.3, 0.3, 0.7)
            _fs = 11.0
            # Clamp guide coords to canvas area
            def _gc(x0, y0, x1, y1):
                return (max(x0, area_min_x), max(y0, area_min_y),
                        min(x1, area_max_x), min(y1, area_max_y))
            # Left distance
            if sel_left > 0:
                gx0, gy0, gx1, gy1 = _gc(c_tl_x, el_sy, el_sx, el_sy + 1)
                if gx1 > gx0 and gy1 > gy0:
                    ctx.draw_filled_rect(gx0, gy0, gx1, gy1, *guide_col, 0.0)
                lx, ly = max(c_tl_x + 2, area_min_x), el_sy - 14
                if ly >= area_min_y:
                    ctx.draw_text(lx, ly, f"{int(sel_left)}", *guide_col, _fs)
            # Top distance
            if sel_top > 0:
                gx0, gy0, gx1, gy1 = _gc(el_sx, c_tl_y, el_sx + 1, el_sy)
                if gx1 > gx0 and gy1 > gy0:
                    ctx.draw_filled_rect(gx0, gy0, gx1, gy1, *guide_col, 0.0)
                lx, ly = el_sx + 3, max(c_tl_y + 2, area_min_y)
                if ly >= area_min_y:
                    ctx.draw_text(lx, ly, f"{int(sel_top)}", *guide_col, _fs)
            # Right distance
            if sel_right > 0:
                gx0, gy0, gx1, gy1 = _gc(el_ex, el_ey, c_br_x, el_ey + 1)
                if gx1 > gx0 and gy1 > gy0:
                    ctx.draw_filled_rect(gx0, gy0, gx1, gy1, *guide_col, 0.0)
                lx, ly = el_ex + 2, el_ey - 14
                if ly >= area_min_y:
                    ctx.draw_text(lx, ly, f"{int(sel_right)}", *guide_col, _fs)
            # Bottom distance
            if sel_bottom > 0:
                gx0, gy0, gx1, gy1 = _gc(el_ex, el_ey, el_ex + 1, c_br_y)
                if gx1 > gx0 and gy1 > gy0:
                    ctx.draw_filled_rect(gx0, gy0, gx1, gy1, *guide_col, 0.0)
                lx, ly = el_ex + 3, max(el_ey + 2, area_min_y)
                if ly >= area_min_y and ly < area_max_y:
                    ctx.draw_text(lx, ly, f"{int(sel_bottom)}", *guide_col, _fs)

        # ── Keyboard shortcuts ──
        if inp.wants_deselect():
            self._select_element(None)
        if inp.wants_delete() and self._selected_element_comp is not None:
            self._delete_selected_element()

        # ── Handle click/select/drag (Figma-like: click-drag directly) ──
        if inp.lmb_clicked and not inp.space_down:
            # Check if a resize handle was clicked first
            clicked_handle = self._hit_test_handle(inp.mouse_x, inp.mouse_y)
            if clicked_handle >= 0 and self._selected_element_comp is not None:
                self._resizing = True
                self._resize_handle_idx = clicked_handle
                self._resize_start_mx = inp.mouse_x
                self._resize_start_my = inp.mouse_y
                sel = self._selected_element_comp
                self._resize_start_rect = (sel.x, sel.y, sel.width, sel.height)
            elif hovered_elem is not None:
                self._select_element(hovered_elem)
                self._dragging = True
                self._drag_start_x = inp.mouse_x
                self._drag_start_y = inp.mouse_y
                self._drag_elem_start_x = hovered_elem.x
                self._drag_elem_start_y = hovered_elem.y
            else:
                self._select_element(None)

        # ── Resize drag ──
        if self._resizing:
            if inp.lmb_down:
                self._apply_resize(inp)
            else:
                self._resizing = False
                self._resize_handle_idx = -1

        # ── Move drag ──
        if self._dragging:
            if inp.lmb_down:
                dx = (inp.mouse_x - self._drag_start_x) / self._zoom
                dy = (inp.mouse_y - self._drag_start_y) / self._zoom

                # Ctrl held → axis-lock (Figma-like: constrain to H or V)
                if inp.ctrl_down:
                    if abs(dx) >= abs(dy):
                        dy = 0.0
                    else:
                        dx = 0.0

                # Zoom-adaptive snap: smaller zoom → larger grid step
                snap = self._drag_snap_step()
                new_x = round((self._drag_elem_start_x + dx) / snap) * snap
                new_y = round((self._drag_elem_start_y + dy) / snap) * snap
                self._selected_element_comp.x = new_x
                self._selected_element_comp.y = new_y
            else:
                self._dragging = False

    # ------------------------------------------------------------------
    # Snap helpers
    # ------------------------------------------------------------------

    def _drag_snap_step(self) -> float:
        """Return a 'nice' snap step in canvas pixels based on current zoom.

        With zoom capped to 100%, keep 1px precision at 100% and gradually
        increase snap size as we zoom out so dragging remains controllable.

        Thresholds:
            zoom >= 1.0  →  1
            zoom >= 0.75 →  2
            zoom >= 0.5  →  5
            zoom >= 0.35 →  10
            zoom >= 0.2  →  20
            zoom >= 0.1  →  50
            else         → 100
        """
        z = self._zoom
        if z >= 1.0:
            return 1
        if z >= 0.75:
            return 2
        if z >= 0.5:
            return 5
        if z >= 0.35:
            return 10
        if z >= 0.2:
            return 20
        if z >= 0.1:
            return 50
        return 100

    # ------------------------------------------------------------------
    # Resize handle helpers
    # ------------------------------------------------------------------

    def _hit_test_handle(self, mx: float, my: float) -> int:
        """Return the index of the handle under (mx, my), or -1."""
        hs = _HANDLE_SIZE + 2  # Slightly larger hit area for usability
        for idx, (px, py) in enumerate(getattr(self, '_handle_positions', [])):
            cx = px + _HANDLE_SIZE  # Center of handle
            cy = py + _HANDLE_SIZE
            if abs(mx - cx) <= hs and abs(my - cy) <= hs:
                return idx
        return -1

    def _apply_resize(self, inp):
        """Update element rect based on current resize handle drag.

        Handle index mapping:
            0=TL, 1=TR, 2=BL, 3=BR, 4=top-mid, 5=bot-mid, 6=left-mid, 7=right-mid
        """
        elem = self._selected_element_comp
        if elem is None:
            return

        dx_canvas = (inp.mouse_x - self._resize_start_mx) / self._zoom
        dy_canvas = (inp.mouse_y - self._resize_start_my) / self._zoom

        snap = self._drag_snap_step()
        dx_canvas = round(dx_canvas / snap) * snap
        dy_canvas = round(dy_canvas / snap) * snap

        sx, sy, sw, sh = self._resize_start_rect
        idx = self._resize_handle_idx
        MIN_SIZE = 4.0  # Minimum element dimension

        # Determine which axes each handle controls
        # left edge  → affects x and width (dx_canvas moves x right, shrinks width)
        # right edge → affects width only
        # top edge   → affects y and height
        # bottom edge→ affects height only
        new_x, new_y, new_w, new_h = sx, sy, sw, sh

        if idx == 0:    # TL: left + top
            new_x = sx + dx_canvas
            new_y = sy + dy_canvas
            new_w = sw - dx_canvas
            new_h = sh - dy_canvas
        elif idx == 1:  # TR: right + top
            new_y = sy + dy_canvas
            new_w = sw + dx_canvas
            new_h = sh - dy_canvas
        elif idx == 2:  # BL: left + bottom
            new_x = sx + dx_canvas
            new_w = sw - dx_canvas
            new_h = sh + dy_canvas
        elif idx == 3:  # BR: right + bottom
            new_w = sw + dx_canvas
            new_h = sh + dy_canvas
        elif idx == 4:  # top-mid
            new_y = sy + dy_canvas
            new_h = sh - dy_canvas
        elif idx == 5:  # bot-mid
            new_h = sh + dy_canvas
        elif idx == 6:  # left-mid
            new_x = sx + dx_canvas
            new_w = sw - dx_canvas
        elif idx == 7:  # right-mid
            new_w = sw + dx_canvas

        # Clamp minimum size
        if new_w < MIN_SIZE:
            if idx in (0, 2, 6):  # Left edge handles
                new_x = sx + sw - MIN_SIZE
            new_w = MIN_SIZE
        if new_h < MIN_SIZE:
            if idx in (0, 1, 4):  # Top edge handles
                new_y = sy + sh - MIN_SIZE
            new_h = MIN_SIZE

        elem.x = new_x
        elem.y = new_y
        elem.width = new_w
        elem.height = new_h

    # ------------------------------------------------------------------
    # Selection
    # ------------------------------------------------------------------

    def _select_element(self, elem_comp):
        """Select a UI element and sync with hierarchy/inspector."""
        self._selected_element_comp = elem_comp
        if self._on_selection_changed:
            if elem_comp is not None:
                go = elem_comp.game_object
                self._on_selection_changed(go)
                # Auto-expand hierarchy to reveal this object
                if self._hierarchy_panel and go is not None:
                    self._hierarchy_panel.expand_to_object(go)
            else:
                self._on_selection_changed(None)

    def _delete_selected_element(self):
        """Delete the currently selected UI element's GameObject."""
        elem = self._selected_element_comp
        if elem is None:
            return
        go = elem.game_object
        self._selected_element_comp = None
        self._dragging = False
        if self._on_selection_changed:
            self._on_selection_changed(None)
        if go is not None:
            from InfEngine.lib import SceneManager
            scene = SceneManager.instance().get_active_scene()
            if scene is not None:
                scene.destroy_game_object(go)

    # ------------------------------------------------------------------
    # Creation helpers
    # ------------------------------------------------------------------

    def _create_canvas(self):
        """Create a new Canvas GameObject in the scene."""
        from InfEngine.lib import SceneManager
        from InfEngine.ui import UICanvas as UICanvasCls
        scene = SceneManager.instance().get_active_scene()
        if scene is None:
            return
        go = scene.create_game_object("Canvas")
        if go:
            go.add_py_component(UICanvasCls())
            # Select the new canvas in hierarchy
            if self._hierarchy_panel:
                self._hierarchy_panel.set_selected_object_by_id(go.id)

    def _create_text_element(self, canvas_go):
        """Create a UIText child under the given canvas GameObject."""
        from InfEngine.lib import SceneManager
        from InfEngine.ui import UIText as UITextCls
        scene = SceneManager.instance().get_active_scene()
        if scene is None:
            return
        go = scene.create_game_object("Text")
        if go:
            go.set_parent(canvas_go)
            text_comp = UITextCls()
            go.add_py_component(text_comp)
            # Center in canvas
            canvas_comp = None
            for c in canvas_go.get_py_components():
                from InfEngine.ui import UICanvas
                if isinstance(c, UICanvas):
                    canvas_comp = c
                    break
            if canvas_comp:
                text_comp.x = canvas_comp.reference_width / 2 - 80
                text_comp.y = canvas_comp.reference_height / 2 - 20
            self._select_element(text_comp)
            if self._hierarchy_panel:
                self._hierarchy_panel.set_selected_object_by_id(go.id)
                self._hierarchy_panel._pending_expand_id = canvas_go.id

    # ------------------------------------------------------------------
    # Zoom helpers
    # ------------------------------------------------------------------

    def _fit_zoom(self, ctx: InfGUIContext, canvas):
        """Fit the canvas into the available area."""
        avail_w = ctx.get_content_region_avail_width()
        avail_h = ctx.get_content_region_avail_height()
        if avail_w < 1 or avail_h < 1:
            return
        ref_w = float(canvas.reference_width)
        ref_h = float(canvas.reference_height)
        zoom_w = (avail_w - 40) / ref_w
        zoom_h = (avail_h - 40) / ref_h
        self._zoom = max(_MIN_ZOOM, min(_MAX_ZOOM, min(zoom_w, zoom_h)))
        # Center canvas
        self._pan_x = (avail_w - ref_w * self._zoom) / 2
        self._pan_y = (avail_h - ref_h * self._zoom) / 2
