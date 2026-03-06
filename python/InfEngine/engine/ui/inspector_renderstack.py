"""Custom Inspector renderer for the RenderStack component.

Displays the pipeline topology with injection points, each having an
'Add Effect' button to add post-processing effects via a categorised popup menu
(similar to Unity's GlobalVolume system).

Mounted effects are displayed as collapsible sections with editable
parameters, regardless of whether the effect is enabled or disabled.
"""

from __future__ import annotations

from typing import Dict, List, TYPE_CHECKING

from InfEngine.lib import InfGUIContext
from .inspector_utils import max_label_w, field_label
from .theme import ImGuiCol, ImGuiTreeNodeFlags, ImGuiStyleVar

if TYPE_CHECKING:
    from InfEngine.renderstack.render_stack import RenderStack, PassEntry


_COL_DIM = (0.55, 0.55, 0.55, 1.0)


def _get_effect_candidates(ip_name: str) -> Dict[str, type]:
    """Return FullScreenEffect classes valid for this injection point."""
    from InfEngine.renderstack.discovery import discover_passes
    from InfEngine.renderstack.fullscreen_effect import FullScreenEffect

    candidates: Dict[str, type] = {}
    for name, cls in discover_passes().items():
        if not issubclass(cls, FullScreenEffect):
            continue
        if cls.injection_point != ip_name:
            continue
        candidates[name] = cls
    return candidates


def _get_addable_effect_candidates(stack: "RenderStack", ip_name: str) -> Dict[str, type]:
    """Return candidates that are not already mounted on the stack."""
    mounted_names = {e.render_pass.name for e in stack.pass_entries}
    return {
        name: cls
        for name, cls in _get_effect_candidates(ip_name).items()
        if name not in mounted_names
    }


def _render_pass_bar(ctx: InfGUIContext, label: str, uid: int) -> None:
    """Render a standard pipeline pass as a simple read-only bullet."""
    ctx.push_style_color(ImGuiCol.Text, *_COL_DIM)
    ctx.tree_node_ex(
        f"{label}##pass_{uid}",
        ImGuiTreeNodeFlags.NoTreePushOnOpen
        | ImGuiTreeNodeFlags.Leaf
        | ImGuiTreeNodeFlags.Bullet
        | ImGuiTreeNodeFlags.SpanAvailWidth,
    )
    ctx.pop_style_color(1)


def _pretty_field_name(name: str) -> str:
    """Convert snake_case field names to a readable inspector label."""
    return name.replace("_", " ").strip().title()


def _effect_field_label(ctx: InfGUIContext, label: str, width: float) -> None:
    """Render a left label that respects the current tree indentation."""
    start_x = ctx.get_cursor_pos_x()
    ctx.align_text_to_frame_padding()
    ctx.label(label)
    ctx.same_line(start_x + width)
    ctx.set_next_item_width(-1)


def render_renderstack_inspector(ctx: InfGUIContext, stack: "RenderStack") -> None:
    _render_pipeline(ctx, stack)
    _render_pipeline_params(ctx, stack)
    ctx.separator()
    _render_topology_with_effects(ctx, stack)


# -- Pipeline selector ---------------------------------------------------

def _render_pipeline(ctx: InfGUIContext, stack: "RenderStack") -> None:
    lw = max_label_w(ctx, ["Pipeline"])
    pipelines = stack.discover_pipelines()
    names = ["Default Forward"] + sorted(
        n for n in pipelines if n != "Default Forward"
    )
    cur = stack.pipeline_class_name or "Default Forward"
    if cur not in names:
        stack.set_pipeline("")
        cur = "Default Forward"
    idx = names.index(cur)
    field_label(ctx, "Pipeline", lw)
    new_idx = ctx.combo("##rs_pipeline", idx, names, -1)
    if new_idx != idx:
        sel = names[new_idx]
        stack.set_pipeline("" if sel == "Default Forward" else sel)


# -- Pipeline parameters -------------------------------------------------

def _render_pipeline_params(ctx: InfGUIContext, stack: "RenderStack") -> None:
    """Render editable serialized fields exposed by the current pipeline."""
    from InfEngine.components.serialized_field import get_serialized_fields, FieldType

    pipeline = stack.pipeline
    fields = get_serialized_fields(pipeline.__class__)
    if not fields:
        return

    ctx.separator()
    ctx.label("Pipeline Settings")

    lw = max_label_w(ctx, list(fields.keys())) if fields else 0.0

    _current_group: str = ""
    _group_visible: bool = True

    for field_name, metadata in fields.items():
        # ── Collapsible group management ──
        field_group = getattr(metadata, 'group', "") or ""
        if field_group != _current_group:
            _current_group = field_group
            if field_group:
                _group_visible = ctx.collapsing_header(field_group)
            else:
                _group_visible = True

        if not _group_visible:
            continue

        if metadata.header:
            ctx.separator()
            ctx.label(metadata.header)
        if metadata.space > 0:
            ctx.dummy(0, metadata.space)

        current_value = getattr(pipeline, field_name, metadata.default)
        new_value = current_value

        if metadata.field_type == FieldType.FLOAT:
            field_label(ctx, field_name, lw)
            _speed = getattr(metadata, 'drag_speed', None) or 0.1
            _slider = getattr(metadata, 'slider', True)
            if metadata.range:
                if _slider:
                    new_value = ctx.float_slider(
                        f"##pp_{field_name}", float(current_value),
                        metadata.range[0], metadata.range[1],
                    )
                else:
                    new_value = ctx.drag_float(
                        f"##pp_{field_name}", float(current_value), _speed,
                        metadata.range[0], metadata.range[1],
                    )
            else:
                new_value = ctx.drag_float(
                    f"##pp_{field_name}", float(current_value), _speed, -1e6, 1e6,
                )

        elif metadata.field_type == FieldType.INT:
            field_label(ctx, field_name, lw)
            _speed = getattr(metadata, 'drag_speed', None) or 1.0
            _slider = getattr(metadata, 'slider', True)
            if metadata.range:
                if _slider:
                    new_value = int(ctx.float_slider(
                        f"##pp_{field_name}", float(current_value),
                        metadata.range[0], metadata.range[1],
                    ))
                else:
                    new_value = int(ctx.drag_float(
                        f"##pp_{field_name}", float(current_value), _speed,
                        metadata.range[0], metadata.range[1],
                    ))
            else:
                new_value = int(ctx.drag_float(
                    f"##pp_{field_name}", float(current_value), _speed, -1e6, 1e6,
                ))

        elif metadata.field_type == FieldType.BOOL:
            new_value = ctx.checkbox(field_name, bool(current_value))

        elif metadata.field_type == FieldType.STRING:
            field_label(ctx, field_name, lw)
            _multiline = getattr(metadata, 'multiline', False)
            if _multiline:
                new_value = ctx.input_text_multiline(
                    f"##pp_{field_name}",
                    str(current_value) if current_value else "",
                    buffer_size=4096, width=-1, height=80,
                )
            else:
                new_value = ctx.text_input(
                    f"##pp_{field_name}",
                    str(current_value) if current_value else "", 256,
                )

        elif metadata.field_type == FieldType.ENUM:
            enum_cls = metadata.enum_type
            if enum_cls is not None:
                members = list(enum_cls)
                names_list = [m.name for m in members]
                current_idx = members.index(current_value) if current_value in members else 0
                field_label(ctx, field_name, lw)
                new_idx = ctx.combo(f"##pp_{field_name}", current_idx, names_list, -1)
                if new_idx != current_idx:
                    new_value = members[new_idx]
            else:
                ctx.label(f"{field_name}: {current_value}")

        elif metadata.field_type == FieldType.COLOR:
            if current_value is not None:
                r, g, b, a = current_value[0], current_value[1], current_value[2], current_value[3]
            else:
                r, g, b, a = 1.0, 1.0, 1.0, 1.0
            field_label(ctx, field_name, lw)
            nr, ng, nb, na = ctx.color_edit(f"##pp_{field_name}", r, g, b, a)
            if (nr, ng, nb, na) != (r, g, b, a):
                new_value = [nr, ng, nb, na]

        else:
            ctx.label(f"{field_name}: {current_value}")

        # Apply change + invalidate graph
        _changed = False
        if metadata.field_type == FieldType.FLOAT:
            _changed = abs(float(new_value) - float(current_value)) > 1e-6
        else:
            _changed = new_value != current_value

        if _changed and not metadata.readonly:
            setattr(pipeline, field_name, new_value)
            stack.invalidate_graph()

        if metadata.tooltip and ctx.is_item_hovered():
            ctx.set_tooltip(metadata.tooltip)

        # Show info text if available
        info = getattr(metadata, 'info_text', "")
        if info:
            ctx.push_style_color(ImGuiCol.Text, 0.55, 0.55, 0.55, 1.0)
            ctx.label(f"  {info}")
            ctx.pop_style_color(1)


# =====================================================================
# Topology + Effects (main section)
# =====================================================================

def _render_topology_with_effects(ctx: InfGUIContext, stack: "RenderStack") -> None:
    """Render topology sequence as thin coloured bars.

    Each injection point gets a [+] button that opens a popup for adding
    effects.  Mounted effects appear as collapsible sections below the
    injection point, with parameters and enable/disable toggle.
    """
    g = stack._build_full_topology_probe()
    seq = g.topology_sequence

    if not seq:
        ctx.push_style_color(ImGuiCol.Text, *_COL_DIM)
        ctx.label("  (empty topology)")
        ctx.pop_style_color(1)
        return

    # Build mounted-effects lookup: injection_point → [PassEntry]
    entries = stack.pass_entries
    ip_entries: Dict[str, List] = {}
    for e in entries:
        ip = e.render_pass.injection_point
        ip_entries.setdefault(ip, []).append(e)

    # Map display labels back to injection point names
    ip_list = g.injection_points
    display_to_name = {}
    for ip in ip_list:
        display_to_name[ip.display_name] = ip.name

    # Reduce vertical spacing between bars
    ctx.push_style_var_vec2(ImGuiStyleVar.ItemSpacing, 4.0, 2.0)

    # Running counter to guarantee unique IDs even when labels collide
    _uid_counter = 0

    for kind, label in seq:
        _uid_counter += 1
        if kind == "ip":
            ip_name = display_to_name.get(label, label)
            _render_injection_point_row(ctx, stack, ip_name, label, _uid_counter, ip_entries.get(ip_name, []))
        else:
            # Regular pipeline pass — thin bar
            _render_pass_bar(ctx, label, _uid_counter)

    ctx.pop_style_var(1)


def _render_injection_point_row(
    ctx: InfGUIContext,
    stack: "RenderStack",
    ip_name: str,
    display_label: str,
    uid: int,
    mounted: List,
) -> None:
    """Render an injection point as a collapsible bar."""
    popup_id = f"Popup_{uid}_{ip_name}"
    has_addable_effects = bool(_get_addable_effect_candidates(stack, ip_name))

    ctx.push_style_color(ImGuiCol.Text, 0.9, 0.9, 0.9, 1.0)
    flags = ImGuiTreeNodeFlags.Framed | ImGuiTreeNodeFlags.DefaultOpen | ImGuiTreeNodeFlags.SpanAvailWidth
    header_open = ctx.tree_node_ex(f"[{display_label}]##ip_hdr_{uid}_{ip_name}", flags)
    ctx.pop_style_color(1)

    if header_open:
        if mounted:
            ctx.dummy(0, 2)
            ctx.push_style_var_vec2(ImGuiStyleVar.ItemSpacing, 4.0, 4.0)
            for idx, entry in enumerate(mounted):
                _render_mounted_effect(ctx, stack, entry, uid * 100 + idx)
            ctx.pop_style_var(1)
            ctx.dummy(0, 4)

        # "Add Effect" button at the bottom of this injection point
        ctx.push_style_var_vec2(ImGuiStyleVar.FramePadding, 0.0, 4.0)
        _btn_x = ctx.get_cursor_pos_x()
        ctx.set_cursor_pos_x(0.0)
        if not has_addable_effects:
            ctx.begin_disabled(True)
        ctx.button(
            f"Add Effect...##add_{uid}_{ip_name}",
            lambda: ctx.open_popup(popup_id),
            -1,
            0,
        )
        if not has_addable_effects:
            ctx.end_disabled()
        ctx.set_cursor_pos_x(_btn_x)
        ctx.pop_style_var(1)

        # Popup for adding effects
        if ctx.begin_popup(popup_id):
            _render_add_effect_popup(ctx, stack, ip_name, uid)
            ctx.end_popup()

        ctx.tree_pop()


def _render_add_effect_popup(
    ctx: InfGUIContext,
    stack: "RenderStack",
    ip_name: str,
    uid: int,
) -> None:
    """Render the categorised effect-selection popup (like Unity Volume)."""
    candidates = _get_effect_candidates(ip_name)

    if not candidates:
        ctx.push_style_color(ImGuiCol.Text, *_COL_DIM)
        ctx.label("  No effects available")
        ctx.pop_style_color(1)
        return

    # Already mounted names (don't allow duplicates)
    mounted_names = {e.render_pass.name for e in stack.pass_entries}

    # Group by menu_path category
    categorized: Dict[str, List] = {}  # category → [(leaf_name, full_name, cls)]
    uncategorized: List = []

    for name, cls in sorted(candidates.items()):
        menu_path = getattr(cls, 'menu_path', '') or ''
        if menu_path:
            parts = menu_path.split('/')
            category = parts[0]
            leaf = parts[-1] if len(parts) > 1 else name
            categorized.setdefault(category, []).append((leaf, name, cls))
        else:
            uncategorized.append((name, name, cls))

    for cat in sorted(categorized.keys()):
        ctx.label(cat)
        ctx.separator()
        for leaf, full_name, cls in categorized[cat]:
            already = full_name in mounted_names
            if already:
                ctx.begin_disabled(True)
            if ctx.selectable(f"  {leaf}##add_{uid}_{full_name}"):
                _add_effect(stack, cls)
                ctx.close_current_popup()
            if already:
                ctx.end_disabled()

    if uncategorized:
        if categorized:
            ctx.dummy(0, 4)
        ctx.label("Other")
        ctx.separator()
        for leaf, full_name, cls in uncategorized:
            already = full_name in mounted_names
            if already:
                ctx.begin_disabled(True)
            if ctx.selectable(f"  {leaf}##add_{uid}_{full_name}"):
                _add_effect(stack, cls)
                ctx.close_current_popup()
            if already:
                ctx.end_disabled()


def _add_effect(stack: "RenderStack", cls: type) -> None:
    """Instantiate and mount an effect onto the stack."""
    inst = cls()
    success = stack.add_pass(inst)
    if not success:
        import sys
        print(f"[RenderStack] add_pass failed for '{inst.name}' at injection point '{inst.injection_point}'", file=sys.stderr)


# =====================================================================
# Mounted effect rendering (collapsible section with parameters)
# =====================================================================

def _render_mounted_effect(
    ctx: InfGUIContext,
    stack: "RenderStack",
    entry,
    uid: int = 0,
) -> None:
    """Render a single mounted effect as a standard InfComponent-like section."""
    from InfEngine.renderstack.fullscreen_effect import FullScreenEffect

    rp = entry.render_pass
    effect_name = rp.name
    is_effect = isinstance(rp, FullScreenEffect)

    ctx.push_id_str(f"fx_{uid}_{effect_name}")

    new_enabled = ctx.checkbox(f"##en_{uid}_{effect_name}", entry.enabled)
    if new_enabled != entry.enabled:
        stack.set_pass_enabled(effect_name, new_enabled)

    ctx.same_line(0, 6)
    flags = (
        ImGuiTreeNodeFlags.DefaultOpen
        | ImGuiTreeNodeFlags.Framed
        | ImGuiTreeNodeFlags.SpanFullWidth
    )
    header_open = ctx.tree_node_ex(f"{effect_name}##hdr_{uid}", flags)

    # Right-click context menu for removal
    if ctx.begin_popup_context_item(f"ctx_{uid}_{effect_name}"):
        if ctx.selectable(f"Remove##{uid}"):
            stack.remove_pass(effect_name)
            ctx.close_current_popup()
        ctx.end_popup()

    if header_open:
        if not entry.enabled:
            ctx.begin_disabled(True)

        if is_effect:
            _render_effect_params(ctx, stack, rp, uid)
        else:
            ctx.push_style_color(ImGuiCol.Text, *_COL_DIM)
            ctx.label(f"  injection: {rp.injection_point}")
            ctx.label(f"  order: {entry.order}")
            ctx.pop_style_color(1)

        if not entry.enabled:
            ctx.end_disabled()

        ctx.tree_pop()

    ctx.pop_id()


# =====================================================================
# Effect parameter rendering
# =====================================================================

def _render_effect_params(
    ctx: InfGUIContext,
    stack: "RenderStack",
    effect,
    uid: int = 0,
) -> None:
    """Render editable serialized fields for a FullScreenEffect instance."""
    from InfEngine.components.serialized_field import FieldType

    fields = dict(getattr(effect.__class__, '_serialized_fields_', {}))
    if not fields:
        return

    labels = [_pretty_field_name(name) for name in fields.keys()]
    lw = max(170.0, max_label_w(ctx, labels)) if fields else 170.0

    for field_name, metadata in fields.items():
        wid = f"##ef_{uid}_{field_name}"
        display_name = _pretty_field_name(field_name)
        if metadata.header:
            ctx.separator()
            ctx.label(metadata.header)
        if getattr(metadata, 'space', 0) > 0:
            ctx.dummy(0, metadata.space)

        current_value = getattr(effect, field_name, metadata.default)
        new_value = current_value

        if metadata.field_type == FieldType.FLOAT:
            _effect_field_label(ctx, display_name, lw)
            _speed = getattr(metadata, 'drag_speed', None) or 0.1
            _slider = getattr(metadata, 'slider', True)
            if metadata.range:
                if _slider:
                    new_value = ctx.float_slider(
                        wid, float(current_value),
                        metadata.range[0], metadata.range[1],
                    )
                else:
                    new_value = ctx.drag_float(
                        wid, float(current_value), _speed,
                        metadata.range[0], metadata.range[1],
                    )
            else:
                new_value = ctx.drag_float(
                    wid, float(current_value), _speed, -1e6, 1e6,
                )

        elif metadata.field_type == FieldType.INT:
            _effect_field_label(ctx, display_name, lw)
            _speed = getattr(metadata, 'drag_speed', None) or 1.0
            _slider = getattr(metadata, 'slider', True)
            if metadata.range:
                if _slider:
                    new_value = int(ctx.float_slider(
                        wid, float(current_value),
                        metadata.range[0], metadata.range[1],
                    ))
                else:
                    new_value = int(ctx.drag_float(
                        wid, float(current_value), _speed,
                        metadata.range[0], metadata.range[1],
                    ))
            else:
                new_value = int(ctx.drag_float(
                    wid, float(current_value), _speed, -1e6, 1e6,
                ))

        elif metadata.field_type == FieldType.BOOL:
            new_value = ctx.checkbox(display_name, bool(current_value))

        elif metadata.field_type == FieldType.STRING:
            _effect_field_label(ctx, display_name, lw)
            new_value = ctx.text_input(
                wid,
                str(current_value) if current_value else "", 256,
            )

        elif metadata.field_type == FieldType.ENUM:
            enum_cls = metadata.enum_type
            if enum_cls is not None:
                members = list(enum_cls)
                names_list = [m.name for m in members]
                current_idx = members.index(current_value) if current_value in members else 0
                _effect_field_label(ctx, display_name, lw)
                new_idx = ctx.combo(wid, current_idx, names_list, -1)
                if new_idx != current_idx:
                    new_value = members[new_idx]
            else:
                ctx.label(f"{display_name}: {current_value}")

        elif metadata.field_type == FieldType.COLOR:
            if current_value is not None:
                r, g, b, a = current_value[0], current_value[1], current_value[2], current_value[3]
            else:
                r, g, b, a = 1.0, 1.0, 1.0, 1.0
            _effect_field_label(ctx, display_name, lw)
            nr, ng, nb, na = ctx.color_edit(wid, r, g, b, a)
            if (nr, ng, nb, na) != (r, g, b, a):
                new_value = [nr, ng, nb, na]

        else:
            ctx.label(f"{display_name}: {current_value}")

        # Apply change + invalidate graph
        _changed = False
        if metadata.field_type == FieldType.FLOAT:
            _changed = abs(float(new_value) - float(current_value)) > 1e-6
        else:
            _changed = new_value != current_value

        if _changed and not metadata.readonly:
            setattr(effect, field_name, new_value)
            stack.invalidate_graph()

        if metadata.tooltip and ctx.is_item_hovered():
            ctx.set_tooltip(metadata.tooltip)
