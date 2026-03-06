"""
Shader file parsing and lookup utilities for the Inspector panel.

All functions are stateless except for an optional *cache* dict parameter
that callers can provide for per-session caching.
"""

import os


def parse_shader_id(filepath: str) -> str:
    """Parse @shader_id annotation from shader file (new @ format only)."""
    with open(filepath, 'r', encoding='utf-8') as f:
        for i, line in enumerate(f):
            if i > 20:
                break
            line = line.strip()
            if line.startswith('@shader_id:'):
                return line[11:].strip()
    return None


def parse_shader_properties(filepath: str) -> list:
    """Parse @property annotations from shader file.
    Returns list of dicts: [{'name': str, 'type': str, 'default': any}, ...]
    """
    import json
    properties = []
    with open(filepath, 'r', encoding='utf-8') as f:
        for i, line in enumerate(f):
            if i > 50:
                break
            line = line.strip()
            if line.startswith('@property:'):
                prop_str = line[10:].strip()
                parts = prop_str.split(',', 2)
                if len(parts) >= 3:
                    name = parts[0].strip()
                    prop_type = parts[1].strip()
                    default_str = parts[2].strip()
                    default_val = json.loads(default_str)
                    properties.append({
                        'name': name,
                        'type': prop_type,
                        'default': default_val
                    })
    return properties


def is_shader_hidden(filepath: str) -> bool:
    """Check if shader file has @hidden annotation (internal shader)."""
    with open(filepath, 'r', encoding='utf-8') as f:
        for i, line in enumerate(f):
            if i > 20:
                break
            stripped = line.strip().lstrip('/ ')
            if stripped == '@hidden':
                return True
    return False


def get_shader_file_path(shader_id: str, ext: str) -> str:
    """Find the file path for a given shader_id by scanning project and built-in dirs."""
    from InfEngine.engine.project_context import get_project_root

    project_root = get_project_root()
    search_roots = []
    if project_root:
        search_roots.append(os.path.join(project_root, "Basics", "shaders"))
        search_roots.append(os.path.join(project_root, "Assets"))
    builtin_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "resources", "shaders"))
    search_roots.append(builtin_root)

    for root in search_roots:
        if not root or not os.path.isdir(root):
            continue
        for dirpath, _, filenames in os.walk(root):
            for fname in filenames:
                if not fname.lower().endswith(ext):
                    continue
                full_path = os.path.join(dirpath, fname)
                file_shader_id = parse_shader_id(full_path)
                if not file_shader_id:
                    continue
                if file_shader_id == shader_id:
                    return full_path
    return None


def shader_display_from_value(value: str, items):
    """Map a shader value to its display string for UI."""
    for display, v in items:
        if v == value:
            return display
    return value


def get_shader_candidates(ext: str, cache: dict = None):
    """Collect shader files from project and built-in shader folders.
    Only shaders with @shader_id annotations are listed.
    Each unique shader_id appears only once in the list.
    
    If *cache* is provided and already contains entries for *ext*, the
    cached result is returned immediately.
    """
    from InfEngine.engine.project_context import get_project_root

    if cache is not None and cache.get(ext) is not None:
        return cache[ext]

    items = []
    seen_shader_ids = set()

    project_root = get_project_root()
    search_roots = []
    if project_root:
        search_roots.append(os.path.join(project_root, "Basics", "shaders"))
        search_roots.append(os.path.join(project_root, "Assets"))

    builtin_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "resources", "shaders"))
    search_roots.append(builtin_root)

    for root in search_roots:
        if not root or not os.path.isdir(root):
            continue
        for dirpath, _, filenames in os.walk(root):
            for fname in filenames:
                if not fname.lower().endswith(ext):
                    continue
                full_path = os.path.join(dirpath, fname)

                shader_id = parse_shader_id(full_path)
                if not shader_id:
                    continue

                if is_shader_hidden(full_path):
                    continue

                if shader_id in seen_shader_ids:
                    continue
                seen_shader_ids.add(shader_id)

                items.append((shader_id, shader_id))

    if not items:
        items = [("(No shaders found)", "")]

    if cache is not None:
        cache[ext] = items
    return items


def sync_properties_from_shader(mat_data: dict, shader_id: str, ext: str,
                                remove_unknown: bool = False):
    """Sync material properties from shader's @property annotations.
    Adds new properties from shader, keeps existing values if property exists.
    If *remove_unknown* is True, removes properties not defined in shader.
    """
    shader_path = get_shader_file_path(shader_id, ext)
    if not shader_path:
        return

    shader_props = parse_shader_properties(shader_path)
    if not shader_props:
        if remove_unknown:
            mat_data["properties"] = {}
        return

    type_map = {
        'Float': 0,
        'Float2': 1,
        'Float3': 2,
        'Float4': 3,
        'Color': 3,
        'Int': 4,
        'Mat4': 5,
        'Texture2D': 6
    }

    props = mat_data.setdefault("properties", {})
    shader_prop_names = set()

    for sp in shader_props:
        name = sp.get('name', '')
        ptype_str = sp.get('type', 'Float')
        default = sp.get('default')

        if not name:
            continue

        shader_prop_names.add(name)
        ptype = type_map.get(ptype_str, 0)

        if name in props:
            props[name]['type'] = ptype
        else:
            props[name] = {
                'type': ptype,
                'value': default
            }

    if remove_unknown:
        props_to_remove = [k for k in props if k not in shader_prop_names]
        for k in props_to_remove:
            del props[k]
