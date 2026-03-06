"""
Tag & Layer Settings Panel — project-wide tag and layer management.

Provides a closable panel for editing the project's tags and layers.
Built-in tags/layers are shown as read-only; custom ones can be added/removed/renamed.
Changes are persisted via TagLayerManager.save_to_file().
"""

from InfEngine.lib import InfGUIContext
from .closable_panel import ClosablePanel
from .theme import Theme, ImGuiCol


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
        self._focus_collision_matrix = False

    def set_project_path(self, path: str):
        """Set the project path for saving settings."""
        self._project_path = path

    def focus_collision_matrix(self):
        """Ensure the next open/render highlights the collision matrix section."""
        self._focus_collision_matrix = True
        self.open()

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
                self._render_collision_matrix_section(ctx, mgr)
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

            # Add new tag
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

                label = f"{i:2d}:"
                ctx.label(label)
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

    def _render_collision_matrix_section(self, ctx: InfGUIContext, mgr):
        ctx.set_next_item_open(self._focus_collision_matrix, Theme.COND_FIRST_USE_EVER)
        if not ctx.collapsing_header("Physics Collision Matrix"):
            self._focus_collision_matrix = False
            return

        self._focus_collision_matrix = False

        all_layers = list(mgr.get_all_layers())
        visible_layers = []
        for i in range(32):
            name = all_layers[i] if i < len(all_layers) else ""
            if mgr.is_builtin_layer(i) or name:
                visible_layers.append((i, name if name else f"Layer {i}"))

        if not visible_layers:
            ctx.label("No layers available")
            return

        ctx.push_style_color(ImGuiCol.Text, *Theme.TEXT_DIM)
        ctx.label("Upper triangle only. Changes are saved immediately.")
        ctx.pop_style_color(1)
        ctx.spacing()

        for row_idx, (layer_a, name_a) in enumerate(visible_layers):
            ctx.push_id_str(f"collision_row_{layer_a}")
            ctx.label(f"{layer_a:2d} {name_a}")
            for col_idx in range(row_idx, len(visible_layers)):
                layer_b, name_b = visible_layers[col_idx]
                ctx.same_line(160 + (col_idx - row_idx) * 28)
                current = mgr.get_layers_collide(layer_a, layer_b)
                new_value = ctx.checkbox(f"##c_{layer_a}_{layer_b}", current)
                if new_value != current:
                    mgr.set_layers_collide(layer_a, layer_b, new_value)
                    self._auto_save(mgr)
            ctx.pop_id()

    def _render_footer(self, ctx: InfGUIContext, mgr):
        ctx.separator()
        ctx.button("Save Settings", lambda: self._save(mgr))
        ctx.same_line()
        def _reset():
            mgr.deserialize('{"custom_tags":[], "layers":[]}')
            self._auto_save(mgr)
        ctx.button("Reset to Defaults", _reset)

    def _do_remove_tag(self, tag: str):
        """Remove a custom tag and auto-save."""
        mgr = self._get_mgr()
        if mgr:
            mgr.remove_tag(tag)
            self._auto_save(mgr)

    def _do_add_tag(self):
        """Add a new custom tag from the input field."""
        mgr = self._get_mgr()
        name = self._new_tag_name.strip()
        if mgr and name and mgr.get_tag_index(name) < 0:
            mgr.add_tag(name)
            self._new_tag_name = ""
            self._auto_save(mgr)

    def _auto_save(self, mgr):
        """Auto-save after each change if project path is set."""
        if self._project_path:
            import os
            settings_dir = os.path.join(self._project_path, "ProjectSettings")
            os.makedirs(settings_dir, exist_ok=True)
            path = os.path.join(settings_dir, "TagLayerSettings.json")
            mgr.save_to_file(path)

    def _save(self, mgr):
        """Explicit save to file."""
        self._auto_save(mgr)
