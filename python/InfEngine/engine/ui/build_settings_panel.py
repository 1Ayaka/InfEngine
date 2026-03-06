"""
Build Settings — Unity-style floating window for managing scenes in builds.

NOT a dockable panel.  Rendered by MenuBarPanel each frame when visible;
never registered through WindowManager / engine.register_gui().

Scenes can be added via drag-and-drop (SCENE_FILE payload from the Project
panel) or through the "Add Open Scene" button.  Each scene receives an
auto-incremented build index (starting at 0).  Persisted to
``ProjectSettings/BuildSettings.json``.
"""

import os
import json
from typing import List, Optional

from InfEngine.debug import Debug
from InfEngine.engine.project_context import get_project_root
from .theme import Theme, ImGuiCol, ImGuiStyleVar


# ---------------------------------------------------------------------------
# Persistence helpers
# ---------------------------------------------------------------------------

BUILD_SETTINGS_FILE = "BuildSettings.json"


def _settings_path() -> Optional[str]:
    root = get_project_root()
    if not root:
        return None
    return os.path.join(root, "ProjectSettings", BUILD_SETTINGS_FILE)


def load_build_settings() -> dict:
    """Load BuildSettings.json, returning ``{"scenes": [...]}``."""
    path = _settings_path()
    if not path or not os.path.isfile(path):
        return {"scenes": []}
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            data = json.load(f)
    except Exception:
        data = {"scenes": []}
    if "scenes" not in data:
        data["scenes"] = []
    return data


def save_build_settings(settings: dict):
    path = _settings_path()
    if not path:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(settings, f, indent=2, ensure_ascii=False)


# ---------------------------------------------------------------------------
# Drag-drop type & style constants
# ---------------------------------------------------------------------------

DRAG_DROP_SCENE = "SCENE_FILE"
DRAG_DROP_REORDER = "BUILD_REORDER"

# Colour from central Theme for drag-drop highlight
_DRAG_TARGET_COLOR = Theme.DRAG_DROP_TARGET

# Window flags:  NoCollapse | NoSavedSettings  (avoid dock contamination)
_WIN_FLAGS = Theme.WINDOW_FLAGS_FLOATING


class BuildSettingsPanel:
    """Standalone floating Build Settings window (NOT a dockable panel).

    Usage (from MenuBarPanel):
        self._build_settings = BuildSettingsPanel()
        # in on_render():
        self._build_settings.render(ctx)
        # to open:
        self._build_settings.open()
    """

    def __init__(self):
        self._visible: bool = False
        self._first_open: bool = True   # centre on first open
        self._scenes: List[str] = []
        self._load()

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def open(self):
        self._visible = True
        self._first_open = True
        self._load()             # refresh from disk

    def close(self):
        self._visible = False

    @property
    def is_open(self) -> bool:
        return self._visible

    def get_scene_list(self) -> List[str]:
        return list(self._scenes)

    # ------------------------------------------------------------------
    # Persistence
    # ------------------------------------------------------------------

    def _load(self):
        data = load_build_settings()
        self._scenes = list(data.get("scenes", []))

    def _save(self):
        save_build_settings({"scenes": self._scenes})

    # ------------------------------------------------------------------
    # Rendering  (called every frame by MenuBarPanel)
    # ------------------------------------------------------------------

    def render(self, ctx):
        """Draw the window if visible.  Must be called every frame."""
        if not self._visible:
            return

        # Centre the window on first open
        if self._first_open:
            x0, y0, dw, dh = ctx.get_main_viewport_bounds()
            cx = x0 + (dw - 580) * 0.5
            cy = y0 + (dh - 440) * 0.5
            ctx.set_next_window_pos(cx, cy, Theme.COND_FIRST_USE_EVER, 0.0, 0.0)
            self._first_open = False

        ctx.set_next_window_size(580, 440, Theme.COND_FIRST_USE_EVER)

        visible, still_open = ctx.begin_window_closable(
            "构建设置 Build Settings", self._visible, _WIN_FLAGS
        )

        if not still_open:
            self._visible = False
            ctx.end_window()
            return

        if visible:
            self._render_body(ctx)

        ctx.end_window()

    # ------------------------------------------------------------------

    def _render_body(self, ctx):
        ctx.label("Scenes in Build")
        ctx.separator()

        # "Add Open Scene" button
        def _add_current():
            from InfEngine.engine.scene_manager import SceneFileManager
            sfm = SceneFileManager.instance()
            if sfm and sfm.current_scene_path:
                self._add_scene(sfm.current_scene_path)

        ctx.button("  添加当前场景  Add Open Scene  ", _add_current)
        ctx.separator()

        # Scene list inside a child region (scrollable + drop target)
        if ctx.begin_child("##build_scene_list", 0, 0, False):
            self._render_scene_list(ctx)
        ctx.end_child()

        # Whole-child drop target for new scenes
        Theme.push_drag_drop_target_style(ctx)  # 1 colour
        if ctx.begin_drag_drop_target():
            payload = ctx.accept_drag_drop_payload(DRAG_DROP_SCENE)
            if payload is not None:
                self._add_scene(str(payload))
            ctx.end_drag_drop_target()
        ctx.pop_style_color(1)

    # ------------------------------------------------------------------

    def _render_scene_list(self, ctx):
        remove_idx: Optional[int] = None
        swap_pair: Optional[tuple] = None

        for i, scene_path in enumerate(self._scenes):
            ctx.push_id(i)

            name = os.path.splitext(os.path.basename(scene_path))[0]
            root = get_project_root() or ""
            rel = os.path.relpath(scene_path, root)

            # ── Row ──
            # Pad rows so buttons fit comfortably
            ctx.push_style_var_vec2(ImGuiStyleVar.ItemSpacing, *Theme.BUILD_SETTINGS_ROW_SPC)

            # Use AllowItemOverlap (1<<4=16) + SpanAllColumns (1<<11=2048)
            ctx.selectable(f"  {i}    {name}    ({rel})##row", False, 16, 0, 0)

            # Drag source — reorder
            if ctx.begin_drag_drop_source(0):
                ctx.set_drag_drop_payload(DRAG_DROP_REORDER, i)
                ctx.label(f"{i}: {name}")
                ctx.end_drag_drop_source()

            # Drop target — reorder or insert
            Theme.push_drag_drop_target_style(ctx)  # 1 colour
            if ctx.begin_drag_drop_target():
                reorder = ctx.accept_drag_drop_payload(DRAG_DROP_REORDER)
                if reorder is not None:
                    swap_pair = (int(reorder), i)
                scene_drop = ctx.accept_drag_drop_payload(DRAG_DROP_SCENE)
                if scene_drop is not None:
                    self._add_scene(str(scene_drop))
                ctx.end_drag_drop_target()
            ctx.pop_style_color(1)

            # Action buttons on the right side of the same row
            btn_area = 160 if i > 0 and i < len(self._scenes) - 1 else 110
            ctx.same_line(max(ctx.get_window_width() - btn_area, 200))
            if i > 0:
                def _up(idx=i):
                    self._scenes[idx - 1], self._scenes[idx] = self._scenes[idx], self._scenes[idx - 1]
                    self._save()
                ctx.button(f"Up##{i}", _up)
                ctx.same_line()

            if i < len(self._scenes) - 1:
                def _down(idx=i):
                    self._scenes[idx], self._scenes[idx + 1] = self._scenes[idx + 1], self._scenes[idx]
                    self._save()
                ctx.button(f"Down##{i}", _down)
                ctx.same_line()

            def _rm(idx=i):
                nonlocal remove_idx
                remove_idx = idx
            ctx.button(f"Remove##{i}", _rm)

            ctx.pop_style_var(1)  # ItemSpacing
            ctx.pop_id()

        # Deferred removal
        if remove_idx is not None:
            del self._scenes[remove_idx]
            self._save()

        # Deferred reorder
        if swap_pair is not None:
            src, dst = swap_pair
            if 0 <= src < len(self._scenes) and 0 <= dst < len(self._scenes) and src != dst:
                item = self._scenes.pop(src)
                self._scenes.insert(dst, item)
                self._save()

        if not self._scenes:
            ctx.label("")
            ctx.label("  (构建列表为空 — Build list is empty)")
            ctx.label("  将场景从项目面板拖入此处。")

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------

    def _add_scene(self, path: str):
        abs_path = os.path.abspath(path)
        if not abs_path.lower().endswith(".scene"):
            return
        for existing in self._scenes:
            if os.path.normcase(os.path.abspath(existing)) == os.path.normcase(abs_path):
                return
        self._scenes.append(abs_path)
        self._save()
        Debug.log_internal(f"Added scene to build list: {os.path.basename(path)}")
