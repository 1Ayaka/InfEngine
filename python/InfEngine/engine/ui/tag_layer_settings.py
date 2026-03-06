"""
Tag & Layer Settings Panel — project-wide tag/layer management and physics settings.

Tags/Layers remain a dockable editor panel.
Physics settings are exposed through a separate standalone floating window.
"""

import json

from InfEngine.lib import InfGUIContext
from .closable_panel import ClosablePanel
from .theme import Theme, ImGuiCol, ImGuiWindowFlags


def _save_mgr_to_project(mgr, project_path: str):
    """Persist tag/layer settings if a project path is available."""
    if not project_path:
        return
    import os
    settings_dir = os.path.join(project_path, "ProjectSettings")
    os.makedirs(settings_dir, exist_ok=True)
    path = os.path.join(settings_dir, "TagLayerSettings.json")
    mgr.save_to_file(path)


_PHYSICS_SETTINGS_FILE = "PhysicsSettings.json"
_DEFAULT_PHYSICS_SETTINGS = {
    "gravity": [0.0, -9.81, 0.0],
    "fixed_delta_time": 0.02,
    "max_fixed_delta_time": 0.1,
}


def _physics_settings_path(project_path: str) -> str:
    import os
    return os.path.join(project_path, "ProjectSettings", _PHYSICS_SETTINGS_FILE)


def _load_physics_settings(project_path: str) -> dict:
    if not project_path:
        return dict(_DEFAULT_PHYSICS_SETTINGS)

    import os

    path = _physics_settings_path(project_path)
    if not os.path.isfile(path):
        return dict(_DEFAULT_PHYSICS_SETTINGS)

    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            data = json.load(f)
    except Exception:
        return dict(_DEFAULT_PHYSICS_SETTINGS)

    result = dict(_DEFAULT_PHYSICS_SETTINGS)
    gravity = data.get("gravity")
    if isinstance(gravity, list) and len(gravity) >= 3:
        result["gravity"] = [float(gravity[0]), float(gravity[1]), float(gravity[2])]
    if "fixed_delta_time" in data:
        result["fixed_delta_time"] = max(0.001, float(data["fixed_delta_time"]))
    if "max_fixed_delta_time" in data:
        result["max_fixed_delta_time"] = max(result["fixed_delta_time"], float(data["max_fixed_delta_time"]))
    return result


def _save_physics_settings(project_path: str, settings: dict):
    if not project_path:
        return

    import os

    path = _physics_settings_path(project_path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(settings, f, indent=2, ensure_ascii=False)


def _apply_physics_settings(settings: dict):
    from InfEngine.lib import SceneManager, vec3f
    from InfEngine.physics import Physics
    from InfEngine.timing import Time

    gravity = settings.get("gravity", _DEFAULT_PHYSICS_SETTINGS["gravity"])
    fixed_dt = max(0.001, float(settings.get("fixed_delta_time", _DEFAULT_PHYSICS_SETTINGS["fixed_delta_time"])))
    max_fixed_dt = max(fixed_dt, float(settings.get("max_fixed_delta_time", _DEFAULT_PHYSICS_SETTINGS["max_fixed_delta_time"])))

    Physics.set_gravity(vec3f(float(gravity[0]), float(gravity[1]), float(gravity[2])))
    sm = SceneManager.instance()
    sm.set_fixed_time_step(fixed_dt)
    sm.set_max_fixed_delta_time(max_fixed_dt)
    Time.fixed_delta_time = fixed_dt


class TagLayerSettingsPanel(ClosablePanel):
    """Inspector-style panel for managing project-wide tags and layers."""

    WINDOW_TYPE_ID = "tag_layer_settings"
    WINDOW_DISPLAY_NAME = "标签与图层 Tags & Layers"

    def __init__(self):
        super().__init__(title="标签与图层 Tags & Layers", window_id="tag_layer_settings")
        self._new_tag_name = ""
        self._new_layer_idx = -1
        self._new_layer_name = ""
        self._project_path = ""
        self._show_tags = True
        self._show_layers = True
        self._mgr = None

    def set_project_path(self, path: str):
        """Set the project path for saving settings."""
        self._project_path = path

    def _get_mgr(self):
        if self._mgr is None:
            from InfEngine.lib import TagLayerManager
            self._mgr = TagLayerManager.instance()
        return self._mgr

    def on_render(self, ctx: InfGUIContext):
        if not self._is_open:
            return

        ctx.set_next_window_size(400, 600, Theme.COND_FIRST_USE_EVER)
        if self._begin_closable_window(ctx, 0):
            mgr = self._get_mgr()
            if mgr is None:
                ctx.label("TagLayerManager not available")
            else:
                self._render_tags_section(ctx, mgr)
                self._render_layers_section(ctx, mgr)
                self._render_footer(ctx, mgr)
        ctx.end_window()

    def _render_tags_section(self, ctx: InfGUIContext, mgr):
        ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
        if ctx.collapsing_header("Tags"):
            all_tags = list(mgr.get_all_tags())

            for i, tag in enumerate(all_tags):
                is_builtin = mgr.is_builtin_tag(tag)
                ctx.push_id_str(f"tag_{i}")

                if is_builtin:
                    ctx.push_style_color(ImGuiCol.Text, *Theme.TEXT_DIM)
                    ctx.label(f"  {tag}")
                    ctx.same_line(ctx.get_window_width() - 80)
                    ctx.label("(built-in)")
                    ctx.pop_style_color(1)
                else:
                    ctx.label(f"  {tag}")
                    ctx.same_line(ctx.get_window_width() - 30)
                    ctx.button(" X ##rm", lambda t=tag: self._do_remove_tag(t))

                ctx.pop_id()

            ctx.separator()
            ctx.label("Add Tag:")
            ctx.same_line(70)
            ctx.set_next_item_width(ctx.get_content_region_avail_width() - 60)
            self._new_tag_name = ctx.text_input("##new_tag", self._new_tag_name, 128)
            ctx.same_line()
            ctx.button(" + ##add_tag", lambda: self._do_add_tag())
            ctx.spacing()

    def _render_layers_section(self, ctx: InfGUIContext, mgr):
        ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
        if ctx.collapsing_header("Layers"):
            all_layers = list(mgr.get_all_layers())

            for i in range(32):
                name = all_layers[i] if i < len(all_layers) else ""
                is_builtin = mgr.is_builtin_layer(i)
                ctx.push_id_str(f"layer_{i}")

                ctx.label(f"{i:2d}:")
                ctx.same_line(36)

                if is_builtin:
                    ctx.push_style_color(ImGuiCol.Text, *Theme.TEXT_DIM)
                    ctx.label(name if name else "---")
                    ctx.same_line(ctx.get_window_width() - 80)
                    ctx.label("(built-in)")
                    ctx.pop_style_color(1)
                else:
                    ctx.set_next_item_width(ctx.get_content_region_avail_width() - 10)
                    new_name = ctx.text_input("##layer_name", name, 64)
                    if new_name != name:
                        mgr.set_layer_name(i, new_name)
                        self._auto_save(mgr)

                ctx.pop_id()

            ctx.spacing()

    def _render_footer(self, ctx: InfGUIContext, mgr):
        ctx.separator()
        ctx.button("Save Settings", lambda: self._save(mgr))
        ctx.same_line()

        def _reset():
            mgr.deserialize('{"custom_tags":[], "layers":[]}')
            self._auto_save(mgr)

        ctx.button("Reset to Defaults", _reset)

    def _do_remove_tag(self, tag: str):
        mgr = self._get_mgr()
        if mgr:
            mgr.remove_tag(tag)
            self._auto_save(mgr)

    def _do_add_tag(self):
        mgr = self._get_mgr()
        name = self._new_tag_name.strip()
        if mgr and name and mgr.get_tag_index(name) < 0:
            mgr.add_tag(name)
            self._new_tag_name = ""
            self._auto_save(mgr)

    def _auto_save(self, mgr):
        _save_mgr_to_project(mgr, self._project_path)

    def _save(self, mgr):
        self._auto_save(mgr)


class PhysicsLayerMatrixPanel:
    """Standalone floating panel for project-wide physics settings and collision matrix."""

    _WIN_FLAGS = Theme.WINDOW_FLAGS_FLOATING | ImGuiWindowFlags.NoDocking

    def __init__(self):
        self._visible = False
        self._first_open = True
        self._request_focus = False
        self._project_path = ""
        self._mgr = None
        self._gravity = list(_DEFAULT_PHYSICS_SETTINGS["gravity"])
        self._fixed_delta_time = float(_DEFAULT_PHYSICS_SETTINGS["fixed_delta_time"])
        self._max_fixed_delta_time = float(_DEFAULT_PHYSICS_SETTINGS["max_fixed_delta_time"])

    def set_project_path(self, path: str):
        self._project_path = path
        self._reload_project_settings()

    @property
    def is_open(self) -> bool:
        return self._visible

    def open(self):
        self._visible = True
        self._request_focus = False
        self._reload_project_settings()

    def close(self):
        self._visible = False

    def _get_mgr(self):
        if self._mgr is None:
            from InfEngine.lib import TagLayerManager
            self._mgr = TagLayerManager.instance()
        return self._mgr

    def _reload_project_settings(self):
        settings = _load_physics_settings(self._project_path)
        self._gravity = list(settings["gravity"])
        self._fixed_delta_time = float(settings["fixed_delta_time"])
        self._max_fixed_delta_time = float(settings["max_fixed_delta_time"])
        _apply_physics_settings(settings)

    def _save_project_settings(self):
        settings = {
            "gravity": [float(self._gravity[0]), float(self._gravity[1]), float(self._gravity[2])],
            "fixed_delta_time": float(self._fixed_delta_time),
            "max_fixed_delta_time": float(self._max_fixed_delta_time),
        }
        _apply_physics_settings(settings)
        _save_physics_settings(self._project_path, settings)

    @staticmethod
    def _draw_vertical_text(ctx: InfGUIContext, child_id: str, text: str, width: float, height: float):
        child_visible = ctx.begin_child(child_id, width, height, False)
        if child_visible:
            min_x = ctx.get_window_pos_x()
            min_y = ctx.get_window_pos_y()
            ctx.draw_text_aligned(
                min_x,
                min_y,
                min_x + width,
                min_y + height,
                text,
                *Theme.TEXT_DIM,
                0.5,
                0.5,
                0.0,
                True,
            )
        ctx.end_child()

    def render(self, ctx: InfGUIContext):
        if not self._visible:
            return

        if self._first_open:
            x0, y0, dw, dh = ctx.get_main_viewport_bounds()
            cx = x0 + (dw - 980) * 0.5
            cy = y0 + (dh - 720) * 0.5
            ctx.set_next_window_pos(cx, cy, Theme.COND_FIRST_USE_EVER, 0.0, 0.0)
            self._first_open = False

        ctx.set_next_window_size(980, 720, Theme.COND_FIRST_USE_EVER)
        visible, still_open = ctx.begin_window_closable(
            "物理设置 Physics Settings", self._visible, self._WIN_FLAGS
        )

        if not still_open:
            self._visible = False
            ctx.end_window()
            return

        if visible:
            mgr = self._get_mgr()
            if mgr is None:
                ctx.label("TagLayerManager not available")
            else:
                self._render_body(ctx, mgr)

        ctx.end_window()

    def _render_body(self, ctx: InfGUIContext, mgr):
        self._render_settings_section(ctx)
        ctx.separator()
        ctx.push_style_color(ImGuiCol.Text, *Theme.TEXT_DIM)
        ctx.label("Collision Matrix — upper triangle only. Changes are saved immediately.")
        ctx.pop_style_color(1)
        ctx.spacing()

        all_layers = list(mgr.get_all_layers())
        visible_layers = []
        for i in range(32):
            name = all_layers[i] if i < len(all_layers) else ""
            if mgr.is_builtin_layer(i) or name:
                visible_layers.append((i, name if name else f"Layer {i}"))

        if not visible_layers:
            ctx.label("No layers available")
            return

        name_col_w = 180.0
        cell_w = 32.0
        header_h = 24.0

        if ctx.begin_child("##physics_matrix_scroll", 0, 0, True):
            ctx.push_style_color(ImGuiCol.Text, *Theme.TEXT_DIM)
            ctx.begin_child("##physics_matrix_spacer", name_col_w, header_h, False)
            ctx.end_child()
            for col_idx, (layer_idx, _) in enumerate(visible_layers):
                ctx.same_line(name_col_w + col_idx * cell_w)
                self._draw_vertical_text(
                    ctx,
                    f"##physics_header_{layer_idx}",
                    f"{layer_idx:02d}",
                    cell_w,
                    header_h,
                )
            ctx.pop_style_color(1)
            ctx.separator()

            for row_idx, (layer_a, name_a) in enumerate(visible_layers):
                ctx.push_id_str(f"physics_matrix_row_{layer_a}")
                ctx.begin_child(f"##physics_matrix_label_{layer_a}", name_col_w, 24, False)
                ctx.label(f"{layer_a:2d} {name_a}")
                ctx.end_child()

                for col_idx in range(len(visible_layers)):
                    layer_b, _ = visible_layers[col_idx]
                    ctx.same_line(name_col_w + col_idx * cell_w)
                    if col_idx < row_idx:
                        ctx.label("·")
                        continue
                    current = mgr.get_layers_collide(layer_a, layer_b)
                    new_value = ctx.checkbox(f"##pm_{layer_a}_{layer_b}", current)
                    if new_value != current:
                        mgr.set_layers_collide(layer_a, layer_b, new_value)
                        _save_mgr_to_project(mgr, self._project_path)
                ctx.pop_id()
        ctx.end_child()

    def _render_settings_section(self, ctx: InfGUIContext):
        ctx.label("Simulation")

        hz = 1.0 / max(self._fixed_delta_time, 0.001)
        new_hz = ctx.drag_float("Physics Iteration Rate (Hz)", hz, 0.5, 1.0, 1000.0)
        if abs(new_hz - hz) > 1e-6:
            self._fixed_delta_time = max(0.001, 1.0 / max(new_hz, 1.0))
            self._max_fixed_delta_time = max(self._max_fixed_delta_time, self._fixed_delta_time)
            self._save_project_settings()

        new_fixed_dt = ctx.input_float("Fixed Time Step (s)", self._fixed_delta_time, 0.001, 0.01, 0)
        if abs(new_fixed_dt - self._fixed_delta_time) > 1e-6:
            self._fixed_delta_time = max(0.001, float(new_fixed_dt))
            self._max_fixed_delta_time = max(self._max_fixed_delta_time, self._fixed_delta_time)
            self._save_project_settings()

        new_max_dt = ctx.input_float("Max Catch-up Delta (s)", self._max_fixed_delta_time, 0.01, 0.05, 0)
        if abs(new_max_dt - self._max_fixed_delta_time) > 1e-6:
            self._max_fixed_delta_time = max(self._fixed_delta_time, float(new_max_dt))
            self._save_project_settings()

        ctx.spacing()
        ctx.label("Gravity")
        gx = ctx.input_float("Gravity X", float(self._gravity[0]), 0.1, 1.0, 0)
        gy = ctx.input_float("Gravity Y", float(self._gravity[1]), 0.1, 1.0, 0)
        gz = ctx.input_float("Gravity Z", float(self._gravity[2]), 0.1, 1.0, 0)
        if abs(gx - self._gravity[0]) > 1e-6 or abs(gy - self._gravity[1]) > 1e-6 or abs(gz - self._gravity[2]) > 1e-6:
            self._gravity = [float(gx), float(gy), float(gz)]
            self._save_project_settings()

