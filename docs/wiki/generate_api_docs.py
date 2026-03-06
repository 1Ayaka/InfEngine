#!/usr/bin/env python3
"""
InfEngine API Reference — Automated Documentation Generator
============================================================

Introspects the InfEngine Python package (via .pyi stubs and source files)
and generates per-class / per-module Markdown pages in the Unity Scripting
API Reference style.

Usage:
    python docs/wiki/generate_api_docs.py

Output:
    docs/wiki/docs/en/api/*.md   (English)
    docs/wiki/docs/zh/api/*.md   (Chinese)
    docs/wiki/mkdocs_api_nav.yml (YAML fragment to paste into mkdocs.yml nav)

Design:
    • Each public class/function/enum gets its own page
    • Pages list Properties, Methods, Static Methods, Enums, Constructors
    • Description / Example sections are left as placeholders for manual editing
    • A "namespace → class" hierarchy mirrors the Python package layout
    • Bi-lingual: generates both en/ and zh/ pages

Merge strategy:
    If a generated .md file already has user-written content inside
    <!-- USER CONTENT START --> / <!-- USER CONTENT END --> markers,
    the generator preserves that content on re-generation.
"""

from __future__ import annotations

import ast
import inspect
import os
import re
import sys
import textwrap
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

WIKI_ROOT = Path(__file__).resolve().parent          # docs/wiki
DOCS_ROOT = WIKI_ROOT / "docs"
PROJECT_ROOT = WIKI_ROOT.parent.parent               # InfEngine repo root
PYTHON_ROOT = PROJECT_ROOT / "python"
STUB_ROOT = PYTHON_ROOT / "InfEngine"

EN_API = DOCS_ROOT / "en" / "api"
ZH_API = DOCS_ROOT / "zh" / "api"

# Marker for user-editable content blocks
USER_START = "<!-- USER CONTENT START -->"
USER_END   = "<!-- USER CONTENT END -->"

# Languages
LANG_EN = "en"
LANG_ZH = "zh"

# Translation table for section headings
I18N = {
    "description":          {"en": "Description",           "zh": "描述"},
    "properties":           {"en": "Properties",            "zh": "属性"},
    "constructors":         {"en": "Constructors",          "zh": "构造函数"},
    "public_methods":       {"en": "Public Methods",        "zh": "公共方法"},
    "static_methods":       {"en": "Static Methods",        "zh": "静态方法"},
    "operators":            {"en": "Operators",             "zh": "运算符"},
    "enums":                {"en": "Enums",                 "zh": "枚举"},
    "values":               {"en": "Values",                "zh": "枚举值"},
    "example":              {"en": "Example",               "zh": "示例"},
    "see_also":             {"en": "See Also",              "zh": "另请参阅"},
    "class_in":             {"en": "class in",              "zh": "类位于"},
    "enum_in":              {"en": "enum in",               "zh": "枚举位于"},
    "function_in":          {"en": "function in",           "zh": "函数位于"},
    "module":               {"en": "module",                "zh": "模块"},
    "name":                 {"en": "Name",                  "zh": "名称"},
    "type":                 {"en": "Type",                  "zh": "类型"},
    "desc":                 {"en": "Description",           "zh": "描述"},
    "method":               {"en": "Method",                "zh": "方法"},
    "returns":              {"en": "Returns",               "zh": "返回值"},
    "parameters":           {"en": "Parameters",            "zh": "参数"},
    "signature":            {"en": "Signature",             "zh": "签名"},
    "inherited_from":       {"en": "Inherited from",        "zh": "继承自"},
    "inherits":             {"en": "Inherits from",         "zh": "继承自"},
    "package":              {"en": "Package",               "zh": "包"},
    "packages":             {"en": "Packages",              "zh": "包"},
    "version":              {"en": "Version 0.1",           "zh": "版本 0.1"},
    "api_ref_title":        {"en": "InfEngine Scripting API", "zh": "InfEngine 脚本 API"},
    "api_ref_welcome":      {
        "en": "Welcome to the InfEngine Scripting API Reference. Browse packages from the sidebar to see class documentation.",
        "zh": "欢迎查阅 InfEngine 脚本 API 参考文档。请从侧边栏浏览包以查看类文档。",
    },
    "decorator_in":         {"en": "decorator in",          "zh": "装饰器位于"},
    "lifecycle_methods":    {"en": "Lifecycle Methods",     "zh": "生命周期方法"},
}

def t(key: str, lang: str) -> str:
    """Translate a key into the given language."""
    return I18N.get(key, {}).get(lang, key)


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------

@dataclass
class ParamInfo:
    name: str
    type_hint: str = ""
    default: str = ""
    doc: str = ""

@dataclass
class MethodInfo:
    name: str
    params: List[ParamInfo] = field(default_factory=list)
    return_type: str = ""
    doc: str = ""
    is_static: bool = False
    is_classmethod: bool = False
    is_property: bool = False
    is_setter: bool = False
    is_operator: bool = False
    overloads: List["MethodInfo"] = field(default_factory=list)

@dataclass
class EnumValue:
    name: str
    value: str = ""
    doc: str = ""

@dataclass
class ClassInfo:
    name: str
    module: str                         # e.g. "InfEngine" or "InfEngine.core"
    doc: str = ""
    bases: List[str] = field(default_factory=list)
    properties: List[MethodInfo] = field(default_factory=list)
    methods: List[MethodInfo] = field(default_factory=list)
    static_methods: List[MethodInfo] = field(default_factory=list)
    constructors: List[MethodInfo] = field(default_factory=list)
    operators: List[MethodInfo] = field(default_factory=list)
    enum_values: List[EnumValue] = field(default_factory=list)
    is_enum: bool = False
    kind: str = "class"                 # "class", "enum", "function", "decorator"
    nested_enums: List["ClassInfo"] = field(default_factory=list)
    lifecycle_methods: List[MethodInfo] = field(default_factory=list)

@dataclass
class FunctionInfo:
    name: str
    module: str
    params: List[ParamInfo] = field(default_factory=list)
    return_type: str = ""
    doc: str = ""
    kind: str = "function"              # "function" or "decorator"

@dataclass
class ModuleInfo:
    name: str                           # e.g. "InfEngine.core"
    classes: List[ClassInfo] = field(default_factory=list)
    functions: List[FunctionInfo] = field(default_factory=list)
    doc: str = ""


# ---------------------------------------------------------------------------
# AST-based .pyi / .py introspection
# ---------------------------------------------------------------------------

OPERATOR_NAMES = {
    "__add__", "__radd__", "__sub__", "__rsub__", "__mul__", "__rmul__",
    "__truediv__", "__rtruediv__", "__iadd__", "__isub__", "__imul__",
    "__itruediv__", "__eq__", "__ne__", "__lt__", "__le__", "__gt__",
    "__ge__", "__neg__", "__pos__", "__abs__", "__getitem__", "__setitem__",
    "__len__", "__contains__", "__repr__", "__str__", "__hash__",
    "__bool__", "__iter__",
}

LIFECYCLE_NAMES = {
    "awake", "start", "update", "fixed_update", "late_update",
    "on_destroy", "on_enable", "on_disable", "on_validate", "reset",
    "on_after_deserialize", "on_before_serialize",
    "on_draw_gizmos", "on_draw_gizmos_selected",
}


def _unparse_annotation(node) -> str:
    """Convert an AST annotation node back to source string."""
    if node is None:
        return ""
    try:
        return ast.unparse(node)
    except Exception:
        return ""


def _get_docstring(node) -> str:
    """Extract docstring from an AST class/function body."""
    if node.body and isinstance(node.body[0], ast.Expr) and isinstance(node.body[0].value, ast.Constant):
        val = node.body[0].value
        if isinstance(val.value, str):
            return _clean_docstring(val.value)
    return ""


def _parse_func(node: ast.FunctionDef) -> MethodInfo:
    """Parse a function/method AST node into MethodInfo."""
    params = []
    args = node.args
    defaults_offset = len(args.args) - len(args.defaults)

    for i, arg in enumerate(args.args):
        if arg.arg == "self" or arg.arg == "cls":
            continue
        pi = ParamInfo(name=arg.arg)
        pi.type_hint = _unparse_annotation(arg.annotation)
        di = i - defaults_offset
        if di >= 0 and di < len(args.defaults):
            pi.default = ast.unparse(args.defaults[di])
        params.append(pi)

    # keyword-only args
    kw_defaults = args.kw_defaults
    for i, arg in enumerate(args.kwonlyargs):
        pi = ParamInfo(name=arg.arg)
        pi.type_hint = _unparse_annotation(arg.annotation)
        if kw_defaults[i] is not None:
            pi.default = ast.unparse(kw_defaults[i])
        params.append(pi)

    return_type = _unparse_annotation(node.returns)
    doc = _get_docstring(node)
    is_static = any(
        isinstance(d, ast.Name) and d.id == "staticmethod"
        for d in node.decorator_list
    )
    is_classmethod = any(
        isinstance(d, ast.Name) and d.id == "classmethod"
        for d in node.decorator_list
    )
    is_property = any(
        isinstance(d, ast.Name) and d.id == "property"
        for d in node.decorator_list
    )
    is_setter = any(
        isinstance(d, ast.Attribute) and d.attr == "setter"
        for d in node.decorator_list
    )
    is_operator = node.name in OPERATOR_NAMES

    return MethodInfo(
        name=node.name,
        params=params,
        return_type=return_type,
        doc=doc,
        is_static=is_static,
        is_classmethod=is_classmethod,
        is_property=is_property,
        is_setter=is_setter,
        is_operator=is_operator,
    )


def _parse_class(node: ast.ClassDef, module: str) -> ClassInfo:
    """Parse a class AST node into ClassInfo."""
    bases = [ast.unparse(b) for b in node.bases]
    doc = _get_docstring(node)
    is_enum = any(b in ("IntEnum", "Enum", "enum.IntEnum") for b in bases)

    ci = ClassInfo(
        name=node.name,
        module=module,
        doc=doc,
        bases=bases,
        is_enum=is_enum,
        kind="enum" if is_enum else "class",
    )

    # Track property names that have setters (to skip setter entries)
    setter_names = set()
    for item in node.body:
        if isinstance(item, ast.FunctionDef):
            for d in item.decorator_list:
                if isinstance(d, ast.Attribute) and d.attr == "setter":
                    setter_names.add(item.name)

    # Track overloaded methods
    overload_map: Dict[str, List[MethodInfo]] = {}
    for item in node.body:
        if isinstance(item, ast.FunctionDef):
            is_overload = any(
                (isinstance(d, ast.Name) and d.id == "overload") or
                (isinstance(d, ast.Attribute) and d.attr == "overload")
                for d in item.decorator_list
            )
            if is_overload:
                overload_map.setdefault(item.name, []).append(_parse_func(item))

    seen_methods = set()
    for item in node.body:
        if isinstance(item, ast.FunctionDef):
            mi = _parse_func(item)

            # Skip setter duplicates
            if mi.is_setter:
                continue

            # Skip private/internal methods
            if mi.name.startswith("_") and not mi.is_operator and mi.name != "__init__":
                continue

            # Skip overload-decorated versions (we'll aggregate them)
            is_overload = any(
                (isinstance(d, ast.Name) and d.id == "overload") or
                (isinstance(d, ast.Attribute) and d.attr == "overload")
                for d in item.decorator_list
            )
            if is_overload:
                continue

            # Attach overloads if any
            if mi.name in overload_map:
                mi.overloads = overload_map[mi.name]

            # Avoid duplicate entries
            if mi.name in seen_methods:
                continue
            seen_methods.add(mi.name)

            # Determine if property has setter
            has_setter = mi.name in setter_names

            if mi.name == "__init__":
                if mi.overloads:
                    ci.constructors.extend(mi.overloads)
                else:
                    ci.constructors.append(mi)
            elif mi.is_property:
                mi._has_setter = has_setter  # type: ignore
                ci.properties.append(mi)
            elif mi.is_operator:
                ci.operators.append(mi)
            elif mi.name in LIFECYCLE_NAMES:
                ci.lifecycle_methods.append(mi)
            elif mi.is_static or mi.is_classmethod:
                ci.static_methods.append(mi)
            else:
                ci.methods.append(mi)

        elif isinstance(item, ast.AnnAssign) and isinstance(item.target, ast.Name):
            # Class-level annotated attribute (e.g. `x: float`)
            name = item.target.id
            if name.startswith("_"):
                continue
            type_hint = _unparse_annotation(item.annotation)
            doc = ""
            # Check if next node is a docstring expression
            idx = node.body.index(item)
            if idx + 1 < len(node.body):
                nxt = node.body[idx + 1]
                if isinstance(nxt, ast.Expr) and isinstance(nxt.value, ast.Constant) and isinstance(nxt.value.value, str):
                    doc = nxt.value.value.strip()

            if is_enum:
                ci.enum_values.append(EnumValue(name=name, value="", doc=doc))
            else:
                has_setter = name in setter_names
                ci.properties.append(MethodInfo(
                    name=name,
                    return_type=type_hint,
                    doc=doc,
                    is_property=True,
                ))
                ci.properties[-1]._has_setter = has_setter  # type: ignore

        elif isinstance(item, ast.Assign):
            # Enum values like `Cube: int`   or  `Cube = 0`
            for target in item.targets:
                if isinstance(target, ast.Name) and not target.id.startswith("_"):
                    if is_enum:
                        ci.enum_values.append(EnumValue(
                            name=target.id,
                            value=ast.unparse(item.value) if item.value else "",
                        ))

        elif isinstance(item, ast.ClassDef):
            # Nested enum/class
            nested = _parse_class(item, module)
            ci.nested_enums.append(nested)

    return ci


def parse_stub_file(path: Path, module: str) -> ModuleInfo:
    """Parse a .pyi or .py file and extract all public classes and functions."""
    source = path.read_text(encoding="utf-8")
    tree = ast.parse(source)
    mi = ModuleInfo(name=module)

    # Module docstring
    if tree.body and isinstance(tree.body[0], ast.Expr):
        val = tree.body[0].value
        if isinstance(val, ast.Constant) and isinstance(val.value, str):
            mi.doc = val.value.strip()

    for node in tree.body:
        if isinstance(node, ast.ClassDef):
            if node.name.startswith("_"):
                continue
            ci = _parse_class(node, module)
            mi.classes.append(ci)

        elif isinstance(node, ast.FunctionDef):
            if node.name.startswith("_"):
                continue
            fi = _parse_func(node)
            mi.functions.append(FunctionInfo(
                name=fi.name,
                module=module,
                params=fi.params,
                return_type=fi.return_type,
                doc=fi.doc,
            ))

    return mi


# ---------------------------------------------------------------------------
# Markdown generation
# ---------------------------------------------------------------------------

def _sig(mi: MethodInfo, class_name: str = "") -> str:
    """Build a human-readable method signature."""
    parts = []
    for p in mi.params:
        s = p.name
        if p.type_hint:
            s += f": {p.type_hint}"
        if p.default:
            s += f" = {p.default}"
        parts.append(s)
    args = ", ".join(parts)
    ret = f" → {mi.return_type}" if mi.return_type else ""
    prefix = f"{class_name}." if class_name else ""
    if mi.is_static:
        prefix = f"static {prefix}"
    return f"`{prefix}{mi.name}({args}){ret}`"


def _clean_docstring(doc: str) -> str:
    """Clean a raw docstring: strip, dedent, remove code examples for display."""
    if not doc:
        return ""
    # Standard Python docstring cleaning: inspect.cleandoc handles
    # the first-line-no-indent + rest-indented pattern correctly
    return inspect.cleandoc(doc)


def _short_doc(doc: str) -> str:
    """Extract the first sentence of a docstring."""
    if not doc:
        return ""
    # Filter out "Ellipsis" which comes from `...` in stubs
    if doc.strip() == "Ellipsis" or doc.strip() == "...":
        return ""
    first = doc.split("\n")[0].strip()
    # Truncate at first period if reasonable
    if ". " in first:
        first = first[:first.index(". ") + 1]
    return first


def _user_block(section_id: str, existing_blocks: Dict[str, str]) -> str:
    """Generate a user content block, preserving existing content if available."""
    content = existing_blocks.get(section_id, "")
    return f"{USER_START} {section_id}\n{content}\n{USER_END}"


def _extract_user_blocks(text: str) -> Dict[str, str]:
    """Extract existing user-written content blocks from an MD file."""
    blocks = {}
    pattern = re.compile(
        rf"{re.escape(USER_START)}\s*(\S+)\n(.*?)\n{re.escape(USER_END)}",
        re.DOTALL
    )
    for m in pattern.finditer(text):
        blocks[m.group(1)] = m.group(2)
    return blocks


def generate_class_page(ci: ClassInfo, lang: str, existing: str = "") -> str:
    """Generate a single class/enum API reference page."""
    blocks = _extract_user_blocks(existing)
    lines: List[str] = []

    # Title
    lines.append(f"# {ci.name}\n")

    # Class header
    kind_key = "enum_in" if ci.is_enum else "class_in"
    lines.append(f'<div class="class-info">')
    lines.append(f'{t(kind_key, lang)} <b>{ci.module}</b>')
    lines.append(f'</div>\n')

    # Inheritance
    if ci.bases and not ci.is_enum:
        non_trivial = [b for b in ci.bases if b not in ("object", "IntEnum", "Enum")]
        if non_trivial:
            lines.append(f"**{t('inherits', lang)}:** {', '.join(f'[{b}]({b}.md)' for b in non_trivial)}\n")

    # Description
    lines.append(f"## {t('description', lang)}\n")
    if ci.doc:
        lines.append(f"{ci.doc}\n")
    lines.append(_user_block("description", blocks))
    lines.append("")

    # Enum values
    if ci.is_enum and ci.enum_values:
        lines.append(f"## {t('values', lang)}\n")
        lines.append(f"| {t('name', lang)} | {t('desc', lang)} |")
        lines.append("|------|------|")
        for ev in ci.enum_values:
            doc = _short_doc(ev.doc) if ev.doc else ""
            lines.append(f"| {ev.name} | {doc} |")
        lines.append("")
        lines.append(_user_block("enum_values", blocks))
        lines.append("")

    # Constructors
    if ci.constructors:
        lines.append(f"## {t('constructors', lang)}\n")
        lines.append(f"| {t('signature', lang)} | {t('desc', lang)} |")
        lines.append("|------|------|")
        for m in ci.constructors:
            sig = _sig(m, ci.name)
            doc = _short_doc(m.doc)
            lines.append(f"| {sig} | {doc} |")
        lines.append("")
        lines.append(_user_block("constructors", blocks))
        lines.append("")

    # Properties
    if ci.properties:
        lines.append(f"## {t('properties', lang)}\n")
        lines.append(f"| {t('name', lang)} | {t('type', lang)} | {t('desc', lang)} |")
        lines.append("|------|------|------|")
        for p in ci.properties:
            rw = ""
            if hasattr(p, "_has_setter") and not p._has_setter:
                rw = " *(read-only)*" if lang == "en" else " *(只读)*"
            type_str = p.return_type if p.return_type else ""
            doc = _short_doc(p.doc)
            lines.append(f"| {p.name} | `{type_str}` | {doc}{rw} |")
        lines.append("")
        lines.append(_user_block("properties", blocks))
        lines.append("")

    # Public methods
    if ci.methods:
        lines.append(f"## {t('public_methods', lang)}\n")
        lines.append(f"| {t('method', lang)} | {t('desc', lang)} |")
        lines.append("|------|------|")
        for m in ci.methods:
            sig = _sig(m)
            doc = _short_doc(m.doc)
            lines.append(f"| {sig} | {doc} |")
        lines.append("")
        lines.append(_user_block("public_methods", blocks))
        lines.append("")

    # Static methods
    if ci.static_methods:
        lines.append(f"## {t('static_methods', lang)}\n")
        lines.append(f"| {t('method', lang)} | {t('desc', lang)} |")
        lines.append("|------|------|")
        for m in ci.static_methods:
            sig = _sig(m, ci.name)
            doc = _short_doc(m.doc)
            lines.append(f"| {sig} | {doc} |")
        lines.append("")
        lines.append(_user_block("static_methods", blocks))
        lines.append("")

    # Lifecycle methods (for InfComponent and subclasses)
    if ci.lifecycle_methods:
        lines.append(f"## {t('lifecycle_methods', lang)}\n")
        lines.append(f"| {t('method', lang)} | {t('desc', lang)} |")
        lines.append("|------|------|")
        for m in ci.lifecycle_methods:
            sig = _sig(m)
            doc = _short_doc(m.doc)
            lines.append(f"| {sig} | {doc} |")
        lines.append("")
        lines.append(_user_block("lifecycle_methods", blocks))
        lines.append("")

    # Operators
    if ci.operators:
        lines.append(f"## {t('operators', lang)}\n")
        lines.append(f"| {t('method', lang)} | {t('returns', lang)} |")
        lines.append("|------|------|")
        for m in ci.operators:
            sig = _sig(m)
            ret = m.return_type if m.return_type else ""
            lines.append(f"| {sig} | `{ret}` |")
        lines.append("")
        lines.append(_user_block("operators", blocks))
        lines.append("")

    # Nested enums
    for ne in ci.nested_enums:
        lines.append(f"### {ne.name}\n")
        if ne.doc:
            lines.append(f"{ne.doc}\n")
        if ne.enum_values:
            lines.append(f"| {t('name', lang)} | {t('desc', lang)} |")
            lines.append("|------|------|")
            for ev in ne.enum_values:
                doc = _short_doc(ev.doc) if ev.doc else ""
                lines.append(f"| {ev.name} | {doc} |")
            lines.append("")

    # Example
    lines.append(f"## {t('example', lang)}\n")
    lines.append("```python")
    lines.append(f"# TODO: Add example for {ci.name}")
    lines.append("```\n")
    lines.append(_user_block("example", blocks))
    lines.append("")

    # See Also
    lines.append(f"## {t('see_also', lang)}\n")
    lines.append(_user_block("see_also", blocks))
    lines.append("")

    return "\n".join(lines)


def generate_function_page(fi: FunctionInfo, lang: str, existing: str = "") -> str:
    """Generate a page for a standalone function or decorator."""
    blocks = _extract_user_blocks(existing)
    lines: List[str] = []

    lines.append(f"# {fi.name}\n")

    kind_key = "decorator_in" if fi.kind == "decorator" else "function_in"
    lines.append(f'<div class="class-info">')
    lines.append(f'{t(kind_key, lang)} <b>{fi.module}</b>')
    lines.append(f'</div>\n')

    # Signature
    parts = []
    for p in fi.params:
        s = p.name
        if p.type_hint:
            s += f": {p.type_hint}"
        if p.default:
            s += f" = {p.default}"
        parts.append(s)
    ret = f" → {fi.return_type}" if fi.return_type else ""
    lines.append(f"```python\n{fi.name}({', '.join(parts)}){ret}\n```\n")

    # Description
    lines.append(f"## {t('description', lang)}\n")
    if fi.doc:
        lines.append(f"{fi.doc}\n")
    lines.append(_user_block("description", blocks))
    lines.append("")

    # Parameters
    if fi.params:
        lines.append(f"## {t('parameters', lang)}\n")
        lines.append(f"| {t('name', lang)} | {t('type', lang)} | {t('desc', lang)} |")
        lines.append("|------|------|------|")
        for p in fi.params:
            default = f" (default: `{p.default}`)" if p.default else ""
            lines.append(f"| {p.name} | `{p.type_hint}` | {p.doc}{default} |")
        lines.append("")

    # Example
    lines.append(f"## {t('example', lang)}\n")
    lines.append("```python")
    lines.append(f"# TODO: Add example for {fi.name}")
    lines.append("```\n")
    lines.append(_user_block("example", blocks))
    lines.append("")

    return "\n".join(lines)


def generate_index_page(modules: Dict[str, ModuleInfo], lang: str, existing: str = "") -> str:
    """Generate the API index page."""
    blocks = _extract_user_blocks(existing)
    lines: List[str] = []

    lines.append(f"# {t('api_ref_title', lang)}\n")
    lines.append(f'<div class="class-info">')
    lines.append(f'{t("version", lang)}')
    lang_other = "zh" if lang == "en" else "en"
    lang_labels = {"en": "English", "zh": "中文"}
    lines.append(f' &nbsp;|&nbsp; <a href="../../{lang_other}/api/index.html">{lang_labels[lang_other]}</a>')
    lines.append(f'</div>\n')

    lines.append(f"## {t('description', lang)}\n")
    lines.append(f"{t('api_ref_welcome', lang)}\n")
    lines.append(_user_block("description", blocks))
    lines.append("")

    lines.append(f"## {t('packages', lang)}\n")
    lines.append(f"| {t('package', lang)} | {t('desc', lang)} |")
    lines.append("|------|------|")

    for mod_name, mod in sorted(modules.items()):
        class_names = ", ".join(c.name for c in mod.classes[:6])
        if len(mod.classes) > 6:
            class_names += ", ..."
        func_names = ", ".join(f.name for f in mod.functions[:3])
        all_names = ", ".join(filter(None, [class_names, func_names]))
        lines.append(f"| {mod_name} | {all_names} |")

    lines.append("")
    lines.append(_user_block("index", blocks))
    lines.append("")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Module discovery — auto-discovery + public API whitelist
# ---------------------------------------------------------------------------

# Directories to exclude when walking the package tree
AUTO_DISCOVER_EXCLUDE_DIRS = {
    "__pycache__",
    "engine",       # Editor UI — not part of the scripting API
    "examples",     # Example scripts
    "resources",    # Internal resource helpers
}

# ── Public API whitelist ──────────────────────────────────────────────────
# Only classes / enums / functions listed here produce documentation pages
# in the default mode.  Use ``--all`` to generate pages for everything.
#
# When you add a new user-facing type, add its name here so the docs are
# regenerated on the next build.
# ──────────────────────────────────────────────────────────────────────────

PUBLIC_API_CLASSES = {
    # ── Core ──
    "GameObject",
    "Transform",
    "Component",
    "InfComponent",
    "Scene",
    "SceneManager",

    # ── Rendering Components ──
    "Camera",
    "Light",
    "MeshRenderer",

    # ── Resources ──
    "Material",
    "Shader",
    "Texture",
    "MeshData",

    # ── Math ──
    "vector2",
    "vector3",
    "vector4",

    # ── Input ──
    "Input",
    "KeyCode",

    # ── Debug & Gizmos ──
    "Debug",
    "Gizmos",

    # ── Enums ──
    "CameraClearFlags",
    "CameraProjection",
    "LightType",
    "LightShadows",
    "PrimitiveType",
    "LayerMask",
    "LogLevel",
}

PUBLIC_API_FUNCTIONS = {
    # ── Decorators / Attributes ──
    "serialized_field",
    "require_component",
    "add_component_menu",
    "execute_in_edit_mode",
    "disallow_multiple",
    "help_url",
    "hide_field",
    "icon",
}


def _path_to_module(rel_path: Path) -> str:
    """Map a file path (relative to STUB_ROOT) to its logical module name.

    Examples
    --------
    core/material.py      → InfEngine.core
    input/__init__.py     → InfEngine.input
    lib/_InfEngine.pyi    → InfEngine
    debug.py              → InfEngine.debug
    gizmos/gizmos.py      → InfEngine.gizmos
    math/vector.py        → InfEngine.math
    __init__.py           → InfEngine
    """
    parts = list(rel_path.parts)
    stem = parts[-1]
    if stem.endswith(".pyi"):
        stem = stem[:-4]
    elif stem.endswith(".py"):
        stem = stem[:-3]

    # Special case: native bindings stub
    if len(parts) >= 2 and parts[-2] == "lib" and stem == "_InfEngine":
        return "InfEngine"

    # __init__ → module is the enclosing package
    if stem == "__init__":
        pkg_parts = parts[:-1]
        if not pkg_parts:
            return "InfEngine"
        return "InfEngine." + ".".join(pkg_parts)

    # Regular file → module is the enclosing package
    pkg_parts = parts[:-1]
    if not pkg_parts:
        return f"InfEngine.{stem}"
    return "InfEngine." + ".".join(pkg_parts)


def auto_discover_sources() -> List[Tuple[str, Path]]:
    """Walk *python/InfEngine/* and collect every parseable .pyi / .py file.

    When both ``foo.pyi`` and ``foo.py`` exist in the same directory the
    ``.pyi`` stub is preferred (it has cleaner type information).

    Returns a list of ``(module_name, absolute_path)`` tuples.
    """
    sources: List[Tuple[str, Path]] = []

    for dirpath_str, dirnames, filenames in os.walk(STUB_ROOT):
        dirpath = Path(dirpath_str)
        # Prune excluded subtrees
        dirnames[:] = sorted(d for d in dirnames if d not in AUTO_DISCOVER_EXCLUDE_DIRS)

        # Prefer .pyi over .py for the same stem
        file_map: Dict[str, Path] = {}
        for fname in sorted(filenames):
            if fname.endswith(".pyi"):
                stem = fname[:-4]
                file_map[stem] = dirpath / fname          # .pyi always wins
            elif fname.endswith(".py"):
                stem = fname[:-3]
                if stem not in file_map:                   # only when no .pyi
                    file_map[stem] = dirpath / fname

        for stem, fpath in sorted(file_map.items()):
            # Skip private files except the native bindings and __init__
            if stem.startswith("_") and stem != "_InfEngine" and stem != "__init__":
                continue

            rel = fpath.relative_to(STUB_ROOT)
            mod_name = _path_to_module(rel)
            sources.append((mod_name, fpath))

    return sources


def discover_modules(*, include_all: bool = False) -> Dict[str, ModuleInfo]:
    """Discover and parse all modules.

    Parameters
    ----------
    include_all : bool
        When *True*, every discovered class/function gets a page (no
        whitelist filtering).  Activated by the ``--all`` CLI flag.
    """
    sources = auto_discover_sources()
    modules: Dict[str, ModuleInfo] = {}

    for mod_name, path in sources:
        print(f"  Parsing {path.relative_to(PROJECT_ROOT)} → {mod_name}")
        parsed = parse_stub_file(path, mod_name)

        # Merge into existing module bucket
        if mod_name not in modules:
            modules[mod_name] = ModuleInfo(name=mod_name, doc=parsed.doc)

        mod = modules[mod_name]

        for ci in parsed.classes:
            if not include_all and ci.name not in PUBLIC_API_CLASSES:
                continue
            existing_names = {c.name for c in mod.classes}
            if ci.name not in existing_names:
                mod.classes.append(ci)

        for fi in parsed.functions:
            if not include_all and fi.name not in PUBLIC_API_FUNCTIONS:
                continue
            existing_names = {f.name for f in mod.functions}
            if fi.name not in existing_names:
                mod.functions.append(fi)

    # Drop modules that ended up empty after filtering
    modules = {k: v for k, v in modules.items() if v.classes or v.functions}

    return modules


# ---------------------------------------------------------------------------
# File I/O — write pages, preserve user content
# ---------------------------------------------------------------------------

def _read_existing(path: Path) -> str:
    """Read existing file content (empty string if not exists)."""
    if path.exists():
        return path.read_text(encoding="utf-8")
    return ""


def _write_if_changed(path: Path, content: str):
    """Write content to file only if it changed."""
    path.parent.mkdir(parents=True, exist_ok=True)
    old = _read_existing(path)
    if old.strip() == content.strip():
        return False
    path.write_text(content, encoding="utf-8")
    return True


def generate_all(*, include_all: bool = False):
    """Main entry point: discover modules, generate all pages.

    Parameters
    ----------
    include_all : bool
        When *True* every discovered class/function gets a page regardless
        of the PUBLIC_API whitelist.  Pass ``--all`` on the CLI to activate.
    """
    print("=" * 60)
    print("InfEngine API Docs Generator")
    if include_all:
        print("  (--all mode: generating pages for ALL discovered types)")
    print("=" * 60)

    print("\nDiscovering modules...")
    modules = discover_modules(include_all=include_all)

    total_classes = sum(len(m.classes) for m in modules.values())
    total_funcs = sum(len(m.functions) for m in modules.values())
    print(f"\nFound {len(modules)} modules, {total_classes} classes, {total_funcs} functions")

    # Collect all pages for nav generation
    nav_entries: Dict[str, List[Tuple[str, str]]] = {}  # module → [(display_name, filename)]

    for lang in (LANG_EN, LANG_ZH):
        api_dir = EN_API if lang == LANG_EN else ZH_API
        print(f"\nGenerating {lang.upper()} pages in {api_dir.relative_to(PROJECT_ROOT)}...")

        written = 0

        for mod_name, mod in sorted(modules.items()):
            if mod_name not in nav_entries:
                nav_entries[mod_name] = []

            # Generate class pages
            for ci in mod.classes:
                filename = f"{ci.name}.md"
                filepath = api_dir / filename
                existing = _read_existing(filepath)
                content = generate_class_page(ci, lang, existing)
                if _write_if_changed(filepath, content):
                    written += 1

                # Track for nav (only once)
                if lang == LANG_EN:
                    entry = (ci.name, filename)
                    if entry not in nav_entries[mod_name]:
                        nav_entries[mod_name].append(entry)

            # Generate function pages
            for fi in mod.functions:
                filename = f"{fi.name}.md"
                filepath = api_dir / filename
                existing = _read_existing(filepath)
                content = generate_function_page(fi, lang, existing)
                if _write_if_changed(filepath, content):
                    written += 1

                if lang == LANG_EN:
                    entry = (fi.name, filename)
                    if entry not in nav_entries[mod_name]:
                        nav_entries[mod_name].append(entry)

        # Index page
        idx_path = api_dir / "index.md"
        existing = _read_existing(idx_path)
        content = generate_index_page(modules, lang, existing)
        if _write_if_changed(idx_path, content):
            written += 1

        print(f"  {written} files written/updated")

        # Clean up stale .md files that are no longer generated
        generated_filenames = {"index.md"}
        for mod_name, entries in nav_entries.items():
            for _, filename in entries:
                generated_filenames.add(filename)

        removed = 0
        for existing_file in api_dir.glob("*.md"):
            if existing_file.name not in generated_filenames:
                existing_file.unlink()
                removed += 1
        if removed:
            print(f"  {removed} stale files removed")

    # Generate mkdocs nav fragment
    _generate_nav_fragment(nav_entries)

    # Generate mkdocs.yml
    _generate_mkdocs_yml(nav_entries)

    print("\nDone! ✓")


def _generate_nav_fragment(nav_entries: Dict[str, List[Tuple[str, str]]]):
    """Generate a YAML nav fragment file for reference."""
    lines = ["# Auto-generated API nav — copy into mkdocs.yml if needed", ""]

    for lang in (LANG_EN, LANG_ZH):
        prefix = f"{lang}/api"
        section_title = "API Reference" if lang == "en" else "API 参考手册"
        lines.append(f"# {section_title}")

        for mod_name, entries in sorted(nav_entries.items()):
            lines.append(f"#   {mod_name}:")
            for display, filename in sorted(entries):
                lines.append(f"#     - {display}: {prefix}/{filename}")
        lines.append("")

    path = WIKI_ROOT / "mkdocs_api_nav.yml"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(f"\nNav fragment written to {path.relative_to(PROJECT_ROOT)}")


def _generate_mkdocs_yml(nav_entries: Dict[str, List[Tuple[str, str]]]):
    """Regenerate the full mkdocs.yml with auto-generated API nav."""
    lines = []
    lines.append("site_name: InfEngine Scripting API")
    lines.append("site_description: InfEngine Game Engine Scripting API Reference")
    lines.append("site_author: InfEngine Team")
    lines.append("")
    lines.append("use_directory_urls: false")
    lines.append("")
    lines.append("theme:")
    lines.append("  name: material")
    lines.append("  custom_dir: theme")
    lines.append("  language: en")
    lines.append("")
    lines.append("nav:")
    lines.append("  - Home: index.md")
    lines.append("  - Hello World: hello-world.md")

    for lang, section_title in [("en", "API Reference"), ("zh", "API 参考手册")]:
        prefix = f"{lang}/api"
        lines.append(f"  - {section_title}:")
        lines.append(f"    - Overview: {prefix}/index.md")

        for mod_name, entries in sorted(nav_entries.items()):
            lines.append(f"    - {mod_name}:")
            for display, filename in sorted(entries):
                lines.append(f"      - {display}: {prefix}/{filename}")

    lines.append("")
    lines.append("markdown_extensions:")
    lines.append("  - toc:")
    lines.append("      permalink: true")
    lines.append("  - pymdownx.highlight:")
    lines.append("      anchor_linenums: true")
    lines.append("      use_pygments: true")
    lines.append("      pygments_lang_class: true")
    lines.append("  - pymdownx.superfences")
    lines.append("  - pymdownx.inlinehilite")
    lines.append("")
    lines.append("plugins:")
    lines.append("  - search")
    lines.append("")

    path = WIKI_ROOT / "mkdocs.yml"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(f"mkdocs.yml regenerated at {path.relative_to(PROJECT_ROOT)}")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    include_all = "--all" in sys.argv
    generate_all(include_all=include_all)
