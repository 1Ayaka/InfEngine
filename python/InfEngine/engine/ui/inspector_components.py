"""
Component rendering functions for the Inspector panel.

Each function renders the ImGui inspector UI for a specific component type.
Functions take the GUI context and component as arguments — they do not depend
on InspectorPanel state.

New components get a usable Inspector UI automatically via the generic
serialize→edit→deserialize renderer.  To provide a custom renderer, call
``register_component_renderer("TypeName", my_render_fn)`` at module level.
"""

import json
import math
from enum import Enum
from InfEngine.lib import InfGUIContext
from .inspector_utils import max_label_w, field_label
from .theme import Theme, ImGuiCol


# ---------------------------------------------------------------------------
# Drag-float helpers — constant speed & tolerance-based comparison
# ---------------------------------------------------------------------------

# Constant drag speed — matches Unity feel: same for all values.
# Unity uses ~0.1 for general float fields, independent of the current value.
DRAG_SPEED_DEFAULT = 0.1
DRAG_SPEED_FINE    = 0.01    # scale, small-precision fields
DRAG_SPEED_INT     = 1.0     # integer fields


def _float_close(a: float, b: float, rel_tol: float = 1e-5,
                 abs_tol: float = 1e-7) -> bool:
    """Return True if *a* and *b* are close enough to be treated as equal.

    Avoids phantom change-detection caused by float32↔float64 round-trips
    through JSON & pybind11.
    """
    return math.isclose(a, b, rel_tol=rel_tol, abs_tol=abs_tol)


def _is_in_play_mode() -> bool:
    """Return True if the engine is currently in runtime (play/pause) mode."""
    from InfEngine.engine.play_mode import PlayModeManager, PlayModeState
    pm = PlayModeManager.get_instance()
    if pm and pm.state != PlayModeState.EDIT:
        return True
    return False


def _get_enum_members(enum_cls):
    """Return members for Python Enum or pybind11 enum-like types."""
    if enum_cls is None:
        return []

    try:
        return list(enum_cls)
    except TypeError:
        pass

    members_dict = getattr(enum_cls, "__members__", None)
    if isinstance(members_dict, dict):
        return list(members_dict.values())

    return []


def _get_enum_member_name(member) -> str:
    name = getattr(member, "name", None)
    if name:
        return str(name)
    return str(member)


def _get_enum_member_value(member):
    if hasattr(member, "value"):
        return member.value
    return member


def _find_enum_index(members, current_value) -> int:
    """Find the best matching member index for Python or pybind11 enums."""
    if not members:
        return 0

    for idx, member in enumerate(members):
        if member == current_value:
            return idx

    current_raw = _get_enum_member_value(current_value)
    for idx, member in enumerate(members):
        if _get_enum_member_value(member) == current_raw:
            return idx

    try:
        current_int = int(current_raw)
    except Exception:
        current_int = None
    if current_int is not None:
        for idx, member in enumerate(members):
            try:
                if int(_get_enum_member_value(member)) == current_int:
                    return idx
            except Exception:
                continue

    return 0


def _notify_scene_modified():
    """Mark the active scene as dirty (unsaved) in SceneFileManager.

    Skipped in play mode — runtime changes are transient and should not
    dirty the scene file.
    """
    if _is_in_play_mode():
        return
    from InfEngine.engine.scene_manager import SceneFileManager
    sfm = SceneFileManager.instance()
    if sfm:
        sfm.mark_dirty()


def _record_property(target, prop_name: str, old_value, new_value,
                     description: str = ""):
    """Record a property change through the undo system.

    Falls back to direct ``setattr`` + dirty-mark if UndoManager is
    unavailable.
    """
    from InfEngine.engine.undo import UndoManager, SetPropertyCommand
    mgr = UndoManager.instance()
    if mgr:
        mgr.execute(SetPropertyCommand(
            target, prop_name, old_value, new_value,
            description or f"Set {prop_name}"))
        return
    # Fallback
    setattr(target, prop_name, new_value)
    _notify_scene_modified()


def _record_generic_component(comp, old_json: str, new_json: str):
    """Record a generic C++ component JSON edit through the undo system."""
    from InfEngine.engine.undo import UndoManager, GenericComponentCommand
    mgr = UndoManager.instance()
    if mgr:
        mgr.execute(GenericComponentCommand(
            comp, old_json, new_json, f"Edit {comp.type_name}"))
        return
    # Fallback
    comp.deserialize(new_json)
    _notify_scene_modified()


def _record_add_component(obj, type_name: str, comp_ref,
                          is_py: bool = False):
    """Record the addition of a component through the undo system."""
    from InfEngine.engine.undo import (
        UndoManager, AddNativeComponentCommand, AddPyComponentCommand)
    mgr = UndoManager.instance()
    if mgr:
        if is_py:
            mgr.record(AddPyComponentCommand(
                obj.id, comp_ref,
                f"Add {getattr(comp_ref, 'type_name', type_name)}"))
        else:
            mgr.record(AddNativeComponentCommand(
                obj.id, type_name, comp_ref, f"Add {type_name}"))
        return
    _notify_scene_modified()

# ============================================================================
# Component renderer registry
# ============================================================================

_COMPONENT_RENDERERS: dict = {}   # type_name -> render_fn(ctx, comp)
_PY_COMPONENT_RENDERERS: dict = {}  # type_name -> render_fn(ctx, py_comp)
_COMPONENT_EXTRA_RENDERERS: dict = {}  # type_name -> render_fn(ctx, comp) appended after generic


def register_component_renderer(type_name: str, render_fn):
    """Register a custom Inspector renderer for a C++ component type.

    Args:
        type_name: The value returned by ``comp.type_name`` (e.g. "Camera").
        render_fn: ``fn(ctx: InfGUIContext, comp) -> None``
    """
    _COMPONENT_RENDERERS[type_name] = render_fn


def register_component_extra_renderer(type_name: str, render_fn):
    """Register extra Inspector UI appended after generic CppProperty rendering.

    Unlike ``register_component_renderer`` (which *replaces* the entire renderer),
    this appends additional UI *after* the generic CppProperty fields.  Use this
    when a component's standard properties can be handled generically but it needs
    extra custom sections (e.g. AudioSource per-track UI).

    Args:
        type_name: The value returned by ``comp.type_name``.
        render_fn: ``fn(ctx: InfGUIContext, comp) -> None``
    """
    _COMPONENT_EXTRA_RENDERERS[type_name] = render_fn


def register_py_component_renderer(type_name: str, render_fn):
    """Register a custom Inspector renderer for a Python component type.

    When registered, ``render_py_component()`` will use the custom renderer
    instead of the generic serialize-based renderer.

    Args:
        type_name: The ``type_name`` of the InfComponent (e.g. "RenderStack").
        render_fn: ``fn(ctx: InfGUIContext, py_comp) -> None``
    """
    _PY_COMPONENT_RENDERERS[type_name] = render_fn


def render_component(ctx: InfGUIContext, comp):
    """Unified entry point — dispatches to a custom renderer, then tries
    a BuiltinComponent property-setter renderer, and finally falls back
    to the generic serialize-based renderer."""
    renderer = _COMPONENT_RENDERERS.get(comp.type_name)
    if renderer:
        renderer(ctx, comp)
        return

    # If a BuiltinComponent wrapper class with CppProperty descriptors
    # exists for this component type, use the property-setter path.
    # This is more reliable than serialize→edit→deserialize because
    # setters (e.g. SetSize) directly call RebuildShape / physics sync.
    from InfEngine.components.builtin_component import BuiltinComponent
    wrapper_cls = BuiltinComponent._builtin_registry.get(comp.type_name)
    if wrapper_cls:
        render_builtin_via_setters(ctx, comp, wrapper_cls)
        return

    render_cpp_component_generic(ctx, comp)


# ============================================================================
# Built-in component renderers
# ============================================================================


def render_transform_component(ctx: InfGUIContext, trans):
    """Render Transform component fields (Position, Rotation, Scale).
    
    Displays LOCAL values in the inspector (matching Unity convention where
    the Inspector shows localPosition / localEulerAngles / localScale).
    """
    from InfEngine.lib import vec3f
    lw = max_label_w(ctx, ["Position", "Rotation", "Scale"])

    # Position (local space — offset from parent)
    pos = trans.local_position
    px, py, pz = pos[0], pos[1], pos[2]
    npx, npy, npz = ctx.vector3("Position", px, py, pz, DRAG_SPEED_DEFAULT, lw)
    if any(not _float_close(a, b) for a, b in [(npx, px), (npy, py), (npz, pz)]):
        _record_property(trans, "local_position", pos, vec3f(npx, npy, npz), "Set Position")

    # Rotation (local euler angles)
    rot = trans.local_euler_angles
    rx, ry, rz = rot[0], rot[1], rot[2]
    nrx, nry, nrz = ctx.vector3("Rotation", rx, ry, rz, DRAG_SPEED_DEFAULT, lw)
    if any(not _float_close(a, b) for a, b in [(nrx, rx), (nry, ry), (nrz, rz)]):
        _record_property(trans, "local_euler_angles", rot, vec3f(nrx, nry, nrz), "Set Rotation")

    # Scale (local — Unity convention: scale is always local)
    scl = trans.local_scale
    sx, sy, sz = scl[0], scl[1], scl[2]
    nsx, nsy, nsz = ctx.vector3("Scale", sx, sy, sz, DRAG_SPEED_FINE, lw)
    if any(not _float_close(a, b) for a, b in [(nsx, sx), (nsy, sy), (nsz, sz)]):
        _record_property(trans, "local_scale", scl, vec3f(nsx, nsy, nsz), "Set Scale")



# ============================================================================
# BuiltinComponent property-setter renderer
# ============================================================================

def _collect_cpp_properties(wrapper_cls):
    """Collect CppProperty descriptors from *wrapper_cls* MRO (top→base).

    Returns a list of ``(python_attr_name, CppProperty)`` in definition
    order, skipping duplicates.
    """
    seen = set()
    result = []
    # Walk the MRO in reverse so that the most-derived class wins
    for cls in reversed(wrapper_cls.__mro__):
        for attr_name, attr in cls.__dict__.items():
            if attr_name.startswith("_"):
                continue
            if getattr(attr, "_is_cpp_property", False) and attr_name not in seen:
                seen.add(attr_name)
                result.append((attr_name, attr))
    return result


def render_builtin_via_setters(ctx: InfGUIContext, comp, wrapper_cls):
    """Render a C++ component by iterating CppProperty descriptors.

    Unlike the generic JSON serialize→deserialize path, this renderer
    reads/writes properties directly through pybind11 property getters &
    setters.  The C++ setters (e.g. ``SetSize``) immediately call
    ``RebuildShape`` / physics sync, which makes the changes take effect
    in real-time during play mode.
    """
    from InfEngine.components.serialized_field import FieldType

    props = _collect_cpp_properties(wrapper_cls)
    if not props:
        # Fallback — no descriptors found
        render_cpp_component_generic(ctx, comp)
        return

    labels = [name for name, _ in props]
    lw = max_label_w(ctx, labels)

    for py_name, cpp_prop in props:
        meta = cpp_prop.metadata  # FieldMetadata
        cpp_attr = cpp_prop.cpp_attr

        # Conditional visibility
        if meta.visible_when is not None:
            try:
                if not meta.visible_when(comp):
                    continue
            except Exception:
                pass  # On error, show the field

        # Headers / spacing
        if meta.header:
            ctx.separator()
            ctx.label(meta.header)
        if meta.space and meta.space > 0:
            ctx.dummy(0, meta.space)

        # Read current value from C++ component
        current = getattr(comp, cpp_attr)

        # Read-only fields: render as label and skip editing
        if meta.readonly:
            ctx.label(f"{py_name}: {current}")
            continue

        new_value = current

        # ----- FLOAT -----
        if meta.field_type == FieldType.FLOAT:
            field_label(ctx, py_name, lw)
            if meta.range:
                new_value = ctx.float_slider(
                    f"##{py_name}", float(current), meta.range[0], meta.range[1]
                )
            else:
                new_value = ctx.drag_float(
                    f"##{py_name}", float(current), DRAG_SPEED_DEFAULT, -1e6, 1e6
                )
            if not _float_close(float(new_value), float(current)):
                _record_builtin_property(comp, cpp_attr, current, float(new_value),
                                         f"Set {py_name}")

        # ----- INT -----
        elif meta.field_type == FieldType.INT:
            field_label(ctx, py_name, lw)
            if meta.range:
                new_value = int(ctx.float_slider(
                    f"##{py_name}", float(current), meta.range[0], meta.range[1]
                ))
            else:
                new_value = int(ctx.drag_float(
                    f"##{py_name}", float(current), DRAG_SPEED_INT, -1e6, 1e6
                ))
            if int(new_value) != int(current):
                _record_builtin_property(comp, cpp_attr, int(current), int(new_value),
                                         f"Set {py_name}")

        # ----- BOOL -----
        elif meta.field_type == FieldType.BOOL:
            new_value = ctx.checkbox(py_name, bool(current))
            if bool(new_value) != bool(current):
                _record_builtin_property(comp, cpp_attr, bool(current), bool(new_value),
                                         f"Set {py_name}")

        # ----- STRING -----
        elif meta.field_type == FieldType.STRING:
            field_label(ctx, py_name, lw)
            new_value = ctx.text_input(f"##{py_name}", str(current) if current else "", 256)
            if new_value != str(current or ""):
                _record_builtin_property(comp, cpp_attr, str(current or ""), new_value,
                                         f"Set {py_name}")

        # ----- VEC3 -----
        elif meta.field_type == FieldType.VEC3:
            x, y, z = current.x, current.y, current.z
            nx, ny, nz = ctx.vector3(py_name, x, y, z, DRAG_SPEED_DEFAULT, lw)
            if any(not _float_close(a, b) for a, b in [(nx, x), (ny, y), (nz, z)]):
                from InfEngine.lib import vec3f
                _record_builtin_property(comp, cpp_attr, current, vec3f(nx, ny, nz),
                                         f"Set {py_name}")

        # ----- VEC2 -----
        elif meta.field_type == FieldType.VEC2:
            x, y = current.x, current.y
            nx, ny = ctx.vector2(py_name, float(x), float(y), DRAG_SPEED_DEFAULT, lw)
            if any(not _float_close(a, b) for a, b in [(nx, x), (ny, y)]):
                from InfEngine.lib import vec2f
                _record_builtin_property(comp, cpp_attr, current, vec2f(nx, ny),
                                         f"Set {py_name}")

        # ----- VEC4 -----
        elif meta.field_type == FieldType.VEC4:
            x, y, z, w = current.x, current.y, current.z, current.w
            nx, ny, nz, nw = ctx.vector4(py_name, x, y, z, w, DRAG_SPEED_DEFAULT, lw)
            if any(not _float_close(a, b) for a, b in [(nx, x), (ny, y), (nz, z), (nw, w)]):
                from InfEngine.lib import vec4f
                _record_builtin_property(comp, cpp_attr, current, vec4f(nx, ny, nz, nw),
                                         f"Set {py_name}")

        # ----- ENUM -----
        elif meta.field_type == FieldType.ENUM:
            # Resolve enum class (supports lazy str → type via InfEngine.lib)
            enum_cls = meta.enum_type
            if isinstance(enum_cls, str):
                import InfEngine.lib as _lib
                enum_cls = getattr(_lib, enum_cls, None)
            if enum_cls is not None:
                members = _get_enum_members(enum_cls)
                if not members:
                    ctx.label(f"{py_name}: {current}")
                    continue
                if meta.enum_labels and len(meta.enum_labels) == len(members):
                    member_names = meta.enum_labels
                else:
                    member_names = [_get_enum_member_name(m) for m in members]
                current_idx = _find_enum_index(members, current)
                field_label(ctx, py_name, lw)
                new_idx = ctx.combo(f"##{py_name}", current_idx, member_names)
                if new_idx != current_idx:
                    _record_builtin_property(comp, cpp_attr, current, members[new_idx],
                                             f"Set {py_name}")
            else:
                ctx.label(f"{py_name}: {current}")

        # ----- ASSET (AudioClip for built-in AudioSource.clip) -----
        elif meta.field_type == FieldType.ASSET and cpp_attr == "clip":
            display_name = "None"
            if current is not None and hasattr(current, "name"):
                try:
                    display_name = current.name or "None"
                except Exception:
                    display_name = "None"

            field_label(ctx, py_name, lw)
            render_object_field(
                ctx,
                f"audio_clip_{py_name}",
                display_name,
                "AudioClip",
                accept_drag_type="AUDIO_FILE",
                on_drop_callback=lambda payload, _comp=comp, _attr=cpp_attr: _apply_builtin_audio_clip_drop(_comp, _attr, payload),
            )

        # ----- Fallback -----
        else:
            ctx.label(f"{py_name}: {current}")

    # Append extra renderer if registered (e.g. AudioSource per-track section)
    extra = _COMPONENT_EXTRA_RENDERERS.get(getattr(comp, 'type_name', ''))
    if extra:
        extra(ctx, comp)


def _record_builtin_property(comp, cpp_attr: str, old_value, new_value,
                             description: str):
    """Apply a property change to a C++ component via direct setter, with undo.

    The setter path (e.g. ``comp.size = …``) goes through the pybind11
    property → C++ ``SetSize()`` → ``RebuildShape()`` → physics sync,
    which is exactly what we need for runtime changes.
    """
    from InfEngine.engine.undo import UndoManager, BuiltinPropertyCommand
    mgr = UndoManager.instance()
    if mgr:
        cmd = BuiltinPropertyCommand(comp, cpp_attr, old_value, new_value,
                                     description)
        mgr.execute(cmd)
        return
    # Fallback — just set the property directly
    setattr(comp, cpp_attr, new_value)
    _notify_scene_modified()


def _apply_builtin_audio_clip_drop(comp, cpp_attr: str, payload):
    """Handle an AUDIO_FILE drag-drop onto a built-in component AudioClip field."""
    try:
        file_path = str(payload) if not isinstance(payload, str) else payload
        from InfEngine.core.audio_clip import AudioClip as PyAudioClip

        clip = PyAudioClip.load(file_path)
        if clip is None:
            return

        old_val = getattr(comp, cpp_attr)
        _record_builtin_property(comp, cpp_attr, old_val, clip.native, f"Set {cpp_attr}")
    except Exception as e:
        from InfEngine.debug import Debug
        Debug.log_error(f"Audio clip drop failed: {e}")


def render_cpp_component_generic(ctx: InfGUIContext, comp):
    """Render generic fields for a C++ component based on its serialized JSON."""
    original_json = comp.serialize()
    data = json.loads(original_json)

    ignore_keys = {"schema_version", "type", "enabled", "component_id"}
    changed = False

    visible_keys = [k for k in data if k not in ignore_keys]
    lw = max_label_w(ctx, visible_keys) if visible_keys else 0.0

    for key, value in data.items():
        if key in ignore_keys:
            continue

        new_value = value
        if isinstance(value, bool):
            new_value = ctx.checkbox(key, bool(value))
        elif isinstance(value, int):
            field_label(ctx, key, lw)
            new_value = int(ctx.drag_float(f"##{key}", float(value), DRAG_SPEED_INT, -1e6, 1e6))
        elif isinstance(value, float):
            field_label(ctx, key, lw)
            new_value = float(ctx.drag_float(f"##{key}", float(value), DRAG_SPEED_DEFAULT, -1e6, 1e6))
        elif isinstance(value, str):
            field_label(ctx, key, lw)
            new_value = ctx.text_input(f"##{key}", value, 256)
        elif isinstance(value, list):
            if len(value) == 2 and all(isinstance(v, (int, float)) for v in value):
                nx, ny = ctx.vector2(key, float(value[0]), float(value[1]), DRAG_SPEED_DEFAULT, lw)
                new_value = [nx, ny]
            elif len(value) == 3 and all(isinstance(v, (int, float)) for v in value):
                nx, ny, nz = ctx.vector3(key, float(value[0]), float(value[1]), float(value[2]), DRAG_SPEED_DEFAULT, lw)
                new_value = [nx, ny, nz]
            elif len(value) == 4 and all(isinstance(v, (int, float)) for v in value):
                nx, ny, nz, nw = ctx.vector4(key, float(value[0]), float(value[1]), float(value[2]), float(value[3]), DRAG_SPEED_DEFAULT, lw)
                new_value = [nx, ny, nz, nw]
            else:
                ctx.label(f"{key}: {value}")
        else:
            ctx.label(f"{key}: {value}")

        # Detect change — use tolerance for floats to avoid phantom edits
        if isinstance(value, float) and isinstance(new_value, float):
            value_changed = not _float_close(new_value, value)
        elif isinstance(value, list) and isinstance(new_value, list):
            value_changed = any(
                not _float_close(float(a), float(b))
                for a, b in zip(new_value, value)
                if isinstance(a, (int, float)) and isinstance(b, (int, float))
            ) or (len(new_value) != len(value))
        else:
            value_changed = (new_value != value)

        if value_changed:
            data[key] = new_value
            changed = True

    if changed:
        new_json = json.dumps(data)
        _record_generic_component(comp, original_json, new_json)


def _render_info_text(ctx: InfGUIContext, text: str):
    """Render a dimmed, non-editable info line below a field."""
    ctx.push_style_color(ImGuiCol.Text, 0.55, 0.55, 0.55, 1.0)
    ctx.label(f"  {text}")
    ctx.pop_style_color(1)


def render_py_component(ctx: InfGUIContext, py_comp):
    """Render a Python InfComponent's serialized fields.

    If a custom renderer is registered via ``register_py_component_renderer``,
    it is used instead of the generic field-based renderer.

    Supports:
    - ``group``: fields with the same group name are wrapped in a
      ``collapsing_header`` section.
    - ``info_text``: dimmed description line rendered after the field.
    """
    renderer = _PY_COMPONENT_RENDERERS.get(py_comp.type_name)
    if renderer:
        renderer(ctx, py_comp)
        return
    from InfEngine.components.serialized_field import get_serialized_fields, FieldType

    fields = get_serialized_fields(py_comp.__class__)
    lw = max_label_w(ctx, list(fields.keys())) if fields else 0.0

    # Track which collapsible group is currently open so we can close it
    _current_group: str = ""
    _group_visible: bool = True

    for field_name, metadata in fields.items():
        # ── Collapsible group management ──
        field_group = metadata.group or ""
        if field_group != _current_group:
            # Close previous group (no explicit pop needed for collapsing_header)
            _current_group = field_group
            if field_group:
                _group_visible = ctx.collapsing_header(field_group)
            else:
                _group_visible = True

        if not _group_visible:
            continue

        # Add header if specified (simple label, NOT collapsible)
        if metadata.header:
            ctx.separator()
            ctx.label(metadata.header)

        # Add space if specified
        if metadata.space > 0:
            ctx.dummy(0, metadata.space)

        # Get current value
        current_value = getattr(py_comp, field_name, metadata.default)

        # Render based on field type
        new_value = current_value

        if metadata.field_type == FieldType.FLOAT:
            field_label(ctx, field_name, lw)
            _speed = metadata.drag_speed if metadata.drag_speed is not None else DRAG_SPEED_DEFAULT
            if metadata.range:
                if metadata.slider:
                    new_value = ctx.float_slider(f"##{field_name}", float(current_value), metadata.range[0], metadata.range[1])
                else:
                    new_value = ctx.drag_float(f"##{field_name}", float(current_value), _speed, metadata.range[0], metadata.range[1])
            else:
                new_value = ctx.drag_float(f"##{field_name}", float(current_value), _speed, -1e6, 1e6)

        elif metadata.field_type == FieldType.INT:
            field_label(ctx, field_name, lw)
            _speed = metadata.drag_speed if metadata.drag_speed is not None else DRAG_SPEED_INT
            if metadata.range:
                if metadata.slider:
                    new_value = int(ctx.float_slider(f"##{field_name}", float(current_value), metadata.range[0], metadata.range[1]))
                else:
                    new_value = int(ctx.drag_float(f"##{field_name}", float(current_value), _speed, metadata.range[0], metadata.range[1]))
            else:
                new_value = int(ctx.drag_float(f"##{field_name}", float(current_value), _speed, -1e6, 1e6))

        elif metadata.field_type == FieldType.BOOL:
            new_value = ctx.checkbox(field_name, bool(current_value))

        elif metadata.field_type == FieldType.STRING:
            field_label(ctx, field_name, lw)
            if metadata.multiline:
                new_value = ctx.input_text_multiline(f"##{field_name}", str(current_value) if current_value else "", buffer_size=4096, width=-1, height=80)
            else:
                new_value = ctx.text_input(f"##{field_name}", str(current_value) if current_value else "", 256)

        elif metadata.field_type == FieldType.VEC3:
            if current_value is not None:
                x, y, z = current_value.x, current_value.y, current_value.z
            else:
                x, y, z = 0.0, 0.0, 0.0
            nx, ny, nz = ctx.vector3(field_name, x, y, z, DRAG_SPEED_DEFAULT, lw)
            if any(not _float_close(a, b) for a, b in [(nx, x), (ny, y), (nz, z)]):
                from InfEngine.math import vec3f
                new_value = vec3f(nx, ny, nz)

        elif metadata.field_type == FieldType.VEC2:
            if current_value is not None:
                x, y = current_value.x, current_value.y
            else:
                x, y = 0.0, 0.0
            nx, ny = ctx.vector2(field_name, float(x), float(y), DRAG_SPEED_DEFAULT, lw)
            if any(not _float_close(a, b) for a, b in [(nx, x), (ny, y)]):
                from InfEngine.math import vec2f
                new_value = vec2f(nx, ny)

        elif metadata.field_type == FieldType.VEC4:
            if current_value is not None:
                x, y, z, w = current_value.x, current_value.y, current_value.z, current_value.w
            else:
                x, y, z, w = 0.0, 0.0, 0.0, 0.0
            nx, ny, nz, nw = ctx.vector4(field_name, x, y, z, w, DRAG_SPEED_DEFAULT, lw)
            if any(not _float_close(a, b) for a, b in [(nx, x), (ny, y), (nz, z), (nw, w)]):
                from InfEngine.math import vec4f
                new_value = vec4f(nx, ny, nz, nw)

        elif metadata.field_type == FieldType.ENUM:
            enum_cls = metadata.enum_type
            if enum_cls is not None:
                members = _get_enum_members(enum_cls)
                if not members:
                    ctx.label(f"{field_name}: {current_value}")
                    continue
                names = [ _get_enum_member_name(m) for m in members ]
                current_idx = _find_enum_index(members, current_value)
                field_label(ctx, field_name, lw)
                new_idx = ctx.combo(f"##{field_name}", current_idx, names, -1)
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
            nr, ng, nb, na = ctx.color_edit(f"##{field_name}", r, g, b, a)
            if (nr, ng, nb, na) != (r, g, b, a):
                new_value = [nr, ng, nb, na]

        elif metadata.field_type == FieldType.GAME_OBJECT:
            # ── GameObject reference field (drag from Hierarchy) ──
            # Supports both raw GameObject and GameObjectRef wrapper
            _display_obj = current_value
            if hasattr(current_value, 'resolve'):
                _display_obj = current_value.resolve()
            display = _display_obj.name if _display_obj and hasattr(_display_obj, 'name') else "None"
            # Show required component hint
            _type_hint = "GameObject"
            _req_comp = metadata.required_component
            if _req_comp:
                _type_hint = f"GameObject:{_req_comp}"
            field_label(ctx, field_name, lw)
            clicked = render_object_field(
                ctx, f"go_ref_{field_name}", display, _type_hint,
                accept_drag_type="HIERARCHY_GAMEOBJECT",
                on_drop_callback=lambda payload, _fn=field_name, _comp=py_comp, _rc=_req_comp: _apply_gameobject_drop(_comp, _fn, payload, _rc),
            )

        elif metadata.field_type == FieldType.MATERIAL:
            # ── Material reference field (drag from Project Panel) ──
            # Supports both raw Material and MaterialRef wrapper
            _display_mat = current_value
            if hasattr(current_value, 'resolve'):
                _display_mat = current_value.resolve()
            display = _display_mat.name if _display_mat and hasattr(_display_mat, 'name') else "None"
            field_label(ctx, field_name, lw)
            clicked = render_object_field(
                ctx, f"mat_ref_{field_name}", display, "Material",
                accept_drag_type="MATERIAL_FILE",
                on_drop_callback=lambda payload, _fn=field_name, _comp=py_comp: _apply_material_drop(_comp, _fn, payload),
            )

        elif metadata.field_type == FieldType.TEXTURE:
            # ── Texture reference field (drag from Project Panel) ──
            _display_name = "None"
            if hasattr(current_value, 'display_name'):
                _display_name = current_value.display_name
            elif current_value and hasattr(current_value, 'name'):
                _display_name = current_value.name
            field_label(ctx, field_name, lw)
            clicked = render_object_field(
                ctx, f"tex_ref_{field_name}", _display_name, "Texture",
                accept_drag_type="TEXTURE_FILE",
                on_drop_callback=lambda payload, _fn=field_name, _comp=py_comp: _apply_texture_drop(_comp, _fn, payload),
            )
            # Also accept generic image file drops
            if not clicked:
                render_object_field(
                    ctx, f"tex_img_{field_name}", "", "",
                    clickable=False,
                    accept_drag_type="IMAGE_FILE",
                    on_drop_callback=lambda payload, _fn=field_name, _comp=py_comp: _apply_texture_drop(_comp, _fn, payload),
                )

        elif metadata.field_type == FieldType.SHADER:
            # ── Shader reference field (drag from Project Panel) ──
            _display_name = "None"
            if hasattr(current_value, 'display_name'):
                _display_name = current_value.display_name
            elif current_value and hasattr(current_value, 'source_path'):
                _display_name = current_value.source_path
            field_label(ctx, field_name, lw)
            clicked = render_object_field(
                ctx, f"shd_ref_{field_name}", _display_name, "Shader",
                accept_drag_type="SHADER_FILE",
                on_drop_callback=lambda payload, _fn=field_name, _comp=py_comp: _apply_shader_drop(_comp, _fn, payload),
            )

        else:
            ctx.label(f"{field_name}: {current_value}")

        # Update value if changed and not readonly — use tolerance for floats
        if metadata.field_type == FieldType.FLOAT:
            _changed = not _float_close(float(new_value), float(current_value)) if isinstance(new_value, (int, float)) and isinstance(current_value, (int, float)) else (new_value != current_value)
        else:
            _changed = (new_value != current_value)
        if _changed and not metadata.readonly:
            _record_property(py_comp, field_name, current_value, new_value, f"Set {field_name}")
            # Notify component so it can react (like Unity's OnValidate)
            if hasattr(py_comp, '_call_on_validate'):
                py_comp._call_on_validate()

        # Show tooltip if available
        if metadata.tooltip and ctx.is_item_hovered():
            ctx.set_tooltip(metadata.tooltip)

        # Show info text if available
        if metadata.info_text:
            _render_info_text(ctx, metadata.info_text)



def _apply_gameobject_drop(comp, field_name: str, payload, required_component: str = None):
    """Handle a HIERARCHY_GAMEOBJECT drag-drop onto a GAME_OBJECT field.

    Wraps the result in a ``GameObjectRef`` for null safety.
    If *required_component* is set, rejects objects that lack that C++ component.
    """
    try:
        from InfEngine.lib import SceneManager as _SM
        sm = _SM.instance()
        scene = sm.get_active_scene()
        if scene is None:
            return
        obj_id = int(payload) if not isinstance(payload, int) else payload
        game_object = scene.find_by_id(obj_id)
        if game_object is None:
            return
        # ── Type filtering ──
        if required_component:
            cpp_comp = game_object.get_cpp_component(required_component)
            if cpp_comp is None:
                import logging
                logging.getLogger("InfEngine.inspector").info(
                    "Rejected drop: '%s' has no %s component", game_object.name, required_component)
                return
        from InfEngine.components.ref_wrappers import GameObjectRef
        ref = GameObjectRef(game_object)
        old_val = getattr(comp, field_name, None)
        _record_property(comp, field_name, old_val, ref, f"Set {field_name}")
    except Exception as e:
        from InfEngine.debug import Debug
        Debug.log_error(f"GameObject drop failed: {e}")


def _apply_material_drop(comp, field_name: str, payload):
    """Handle a MATERIAL_FILE drag-drop onto a MATERIAL field.

    Wraps the result in a ``MaterialRef`` for null safety and GUID persistence.
    """
    try:
        file_path = str(payload) if not isinstance(payload, str) else payload
        from InfEngine.core.material import Material
        mat = Material.load(file_path)
        if mat is not None:
            from InfEngine.components.ref_wrappers import MaterialRef
            ref = MaterialRef(mat, file_path=file_path)
            old_val = getattr(comp, field_name, None)
            _record_property(comp, field_name, old_val, ref, f"Set {field_name}")
    except Exception as e:
        from InfEngine.debug import Debug
        Debug.log_error(f"Material drop failed: {e}")


def _apply_texture_drop(comp, field_name: str, payload):
    """Handle a TEXTURE_FILE / IMAGE_FILE drag-drop onto a TEXTURE field.

    Creates a ``TextureRef`` that stores the GUID for scene serialization.
    """
    try:
        file_path = str(payload) if not isinstance(payload, str) else payload
        from InfEngine.core.asset_ref import TextureRef
        from InfEngine.core.assets import AssetManager
        guid = ""
        adb = getattr(AssetManager, '_asset_database', None)
        if adb:
            try:
                guid = adb.get_guid_from_path(file_path) or ""
            except Exception:
                pass
        ref = TextureRef(guid=guid, path_hint=file_path)
        old_val = getattr(comp, field_name, None)
        _record_property(comp, field_name, old_val, ref, f"Set {field_name}")
    except Exception as e:
        from InfEngine.debug import Debug
        Debug.log_error(f"Texture drop failed: {e}")


def _apply_shader_drop(comp, field_name: str, payload):
    """Handle a SHADER_FILE drag-drop onto a SHADER field.

    Creates a ``ShaderRef`` that stores the GUID for scene serialization.
    """
    try:
        file_path = str(payload) if not isinstance(payload, str) else payload
        from InfEngine.core.asset_ref import ShaderRef
        from InfEngine.core.assets import AssetManager
        guid = ""
        adb = getattr(AssetManager, '_asset_database', None)
        if adb:
            try:
                guid = adb.get_guid_from_path(file_path) or ""
            except Exception:
                pass
        ref = ShaderRef(guid=guid, path_hint=file_path)
        old_val = getattr(comp, field_name, None)
        _record_property(comp, field_name, old_val, ref, f"Set {field_name}")
    except Exception as e:
        from InfEngine.debug import Debug
        Debug.log_error(f"Shader drop failed: {e}")


def render_object_field(ctx: InfGUIContext, field_id: str, display_text: str,
                        type_hint: str, selected: bool = False, clickable: bool = True,
                        accept_drag_type: str = None, on_drop_callback=None) -> bool:
    """Render a Unity-style object field (selectable box showing an object reference).

    Args:
        ctx: GUI context
        field_id: Unique ID for the field
        display_text: Text to display in the field
        type_hint: Type hint shown in parentheses (e.g., "Material", "Mesh")
        selected: Whether this field is currently selected
        clickable: Whether the field responds to clicks
        accept_drag_type: Optional drag-drop type to accept
        on_drop_callback: Callback function called with the dropped file path

    Returns:
        True if the field was clicked
    """
    clicked = False
    ctx.push_id_str(field_id)

    full_text = f"{display_text} ({type_hint})"
    if len(full_text) > 35:
        full_text = full_text[:32] + "..."

    avail_width = ctx.get_content_region_avail_width()
    if clickable:
        if ctx.selectable(full_text, selected, 0, avail_width, 0):
            clicked = True
    else:
        ctx.label(f"[{full_text}]")

    # Drop target for drag-drop
    if accept_drag_type and on_drop_callback:
        Theme.push_drag_drop_target_style(ctx)  # 1 colour
        if ctx.begin_drag_drop_target():
            payload = ctx.accept_drag_drop_payload(accept_drag_type)
            if payload is not None:
                on_drop_callback(payload)
            ctx.end_drag_drop_target()
        ctx.pop_style_color(1)

    ctx.pop_id()
    return clicked


# ============================================================================
# Camera extra renderer ("Set as Main Camera" button)
# ============================================================================


def _render_camera_extra(ctx: InfGUIContext, comp):
    """Extra Inspector section for Camera: 'Set as Main Camera' button."""
    ctx.separator()
    from InfEngine.lib import SceneManager as _SM
    sm = _SM.instance()
    scene = sm.get_active_scene()
    if scene:
        is_main = (scene.main_camera is comp)
        if is_main:
            ctx.label("\u2713 Main Camera")
        else:
            if ctx.button("Set as Main Camera##set_main_cam"):
                _record_property(scene, "main_camera", scene.main_camera, comp, "Set Main Camera")


# ============================================================================
# AudioSource extra renderer (per-track section only)
# ============================================================================


def _render_audio_source_extra(ctx: InfGUIContext, comp):
    """Extra Inspector section for AudioSource: per-track clip & volume.

    Source-level properties (volume, pitch, mute, spatial, etc.) are handled
    by the generic CppProperty renderer.  This function only renders the
    dynamic per-track section that cannot be expressed as CppProperty.
    """
    track_count = comp.track_count

    ctx.separator()
    ctx.label("Tracks")

    track_labels = ["Clip", "Volume"]
    track_lw = max_label_w(ctx, track_labels)

    for i in range(track_count):
        if ctx.collapsing_header(f"Track {i}", True):
            # Track clip
            clip = comp.get_track_clip(i)
            clip_name = "None"
            if clip is not None:
                try:
                    clip_name = clip.name or "None"
                except Exception:
                    clip_name = "None"

            field_label(ctx, "Clip", track_lw)
            render_object_field(
                ctx,
                f"audio_track_clip_{i}",
                clip_name,
                "AudioClip",
                accept_drag_type="AUDIO_FILE",
                on_drop_callback=lambda payload, _c=comp, _i=i: _apply_track_audio_clip_drop(_c, _i, payload),
            )

            # Track volume
            tv = comp.get_track_volume(i)
            field_label(ctx, "Volume", track_lw)
            new_tv = ctx.float_slider(f"##track_vol_{i}", float(tv), 0.0, 1.0)
            if not _float_close(float(new_tv), float(tv)):
                comp.set_track_volume(i, float(new_tv))
                _notify_scene_modified()

            # Play / Stop buttons (only in play mode for feedback)
            if _is_in_play_mode():
                is_playing = comp.is_track_playing(i)
                if is_playing:
                    if ctx.button(f"Stop##track_stop_{i}"):
                        comp.stop(i)
                else:
                    if ctx.button(f"Play##track_play_{i}"):
                        comp.play(i)
                ctx.same_line()
                status = "Playing" if is_playing else ("Paused" if comp.is_track_paused(i) else "Stopped")
                ctx.push_style_color(ImGuiCol.Text, 0.55, 0.55, 0.55, 1.0)
                ctx.label(status)
                ctx.pop_style_color(1)


def _apply_track_audio_clip_drop(comp, track_index: int, payload):
    """Handle an AUDIO_FILE drag-drop onto a track clip field."""
    try:
        file_path = str(payload) if not isinstance(payload, str) else payload
        from InfEngine.core.audio_clip import AudioClip as PyAudioClip

        clip = PyAudioClip.load(file_path)
        if clip is None:
            return

        comp.set_track_clip(track_index, clip.native)
        _notify_scene_modified()
    except Exception as e:
        from InfEngine.debug import Debug
        Debug.log_error(f"Audio clip drop failed: {e}")


# ============================================================================
# Auto-register built-in component renderers
# ============================================================================
register_component_renderer("Transform", render_transform_component)
register_component_extra_renderer("Camera", _render_camera_extra)
register_component_extra_renderer("AudioSource", _render_audio_source_extra)
# MeshRenderer is registered by InspectorPanel.__init__ (bound method) because
# its renderer needs panel-level state (material selection, drag-drop).

# ============================================================================
# Auto-register Python component renderers
# ============================================================================
from .inspector_renderstack import render_renderstack_inspector
register_py_component_renderer("RenderStack", render_renderstack_inspector)
