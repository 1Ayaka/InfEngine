"""
Script loader for dynamically importing InfComponent subclasses from .py files.

This module provides utilities to load Python scripts and extract component classes
for use in the InfEngine editor. Used for drag-and-drop script attachment.
"""

import os
import sys
import importlib.util
import inspect
from typing import Type, List, Optional

from InfEngine.engine.project_context import resolve_script_path

from .component import InfComponent


class ScriptLoadError(Exception):
    """Raised when a script cannot be loaded or doesn't contain valid components."""
    pass


# ---------------------------------------------------------------------------
# Script error tracking — allows the editor to know which scripts are broken
# without crashing.  Components backed by broken scripts can still be
# *attached* to GameObjects (they keep their serialized data), but Play
# mode is blocked until every script compiles cleanly.
# ---------------------------------------------------------------------------

# Maps normalised absolute path → error message string
_script_errors: dict[str, str] = {}


def _record_script_error(file_path: str, exc: Exception) -> None:
    """Record that *file_path* failed to load with *exc*."""
    import traceback
    norm = os.path.normcase(os.path.normpath(os.path.abspath(file_path)))
    tb_str = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
    _script_errors[norm] = tb_str
    # Also log to Console so the user sees it
    try:
        from InfEngine.debug import Debug
        Debug.log_error(tb_str, source_file=file_path, source_line=0)
    except ImportError:
        import sys
        print(tb_str, file=sys.stderr)


def set_script_error(file_path: str, message: str) -> None:
    """Record an error message for a script (no exception object needed)."""
    norm = os.path.normcase(os.path.normpath(os.path.abspath(file_path)))
    _script_errors[norm] = message


def _clear_script_error(file_path: str) -> None:
    """Clear any previously recorded error for *file_path*."""
    norm = os.path.normcase(os.path.normpath(os.path.abspath(file_path)))
    _script_errors.pop(norm, None)


def get_script_errors() -> dict[str, str]:
    """Return a snapshot of all currently broken scripts {path: traceback}."""
    return dict(_script_errors)


def has_script_errors() -> bool:
    """Return True if any loaded script has unresolved errors."""
    return bool(_script_errors)


def get_script_error_by_path(file_path: str) -> Optional[str]:
    """Return the error string for *file_path*, or ``None`` if it loaded OK."""
    norm = os.path.normcase(os.path.normpath(os.path.abspath(file_path)))
    return _script_errors.get(norm)


def load_component_from_file(file_path: str) -> Type[InfComponent]:
    """
    Load the first InfComponent subclass from a Python file.
    
    Args:
        file_path: Absolute path to the .py file
        
    Returns:
        The first InfComponent subclass found in the file
        
    Raises:
        ScriptLoadError: If file doesn't exist, can't be imported, or contains no components
    """
    components = load_all_components_from_file(file_path)
    if not components:
        raise ScriptLoadError(f"No InfComponent subclasses found in {file_path}")
    return components[0]


def load_all_components_from_file(file_path: str) -> List[Type[InfComponent]]:
    """
    Load all InfComponent subclasses from a Python file.
    
    Args:
        file_path: Absolute path to the .py file
        
    Returns:
        List of InfComponent subclasses found in the file (may be empty)
        
    Raises:
        ScriptLoadError: If file doesn't exist or can't be imported
    """
    # Resolve path (project-relative allowed)
    file_path = resolve_script_path(file_path)

    # Validate file exists
    if not os.path.exists(file_path):
        raise ScriptLoadError(f"Script file not found: {file_path}")
    
    if not file_path.endswith(('.py', '.pyc')):
        raise ScriptLoadError(f"Not a Python file: {file_path}")
    
    # Normalize path for consistent module naming (important for cache invalidation)
    normalized_path = os.path.normcase(os.path.normpath(file_path))
    
    # Get module name from file
    module_name = os.path.splitext(os.path.basename(file_path))[0]
    
    # Create unique module name to avoid conflicts (use normalized file path hash)
    import hashlib
    path_hash = hashlib.md5(normalized_path.encode()).hexdigest()[:8]
    unique_module_name = f"infengine_script_{module_name}_{path_hash}"
    
    # Remove existing module to force reload
    if unique_module_name in sys.modules:
        # Also need to clear any references in the old module's classes
        old_module = sys.modules[unique_module_name]
        from .serialized_field import clear_serialized_fields_cache
        for name, obj in inspect.getmembers(old_module, inspect.isclass):
            # Only clear classes DEFINED in this script module — never touch
            # imported classes (e.g. BuiltinComponent subclasses like Rigidbody,
            # Camera) whose _serialized_fields_ must remain intact.
            if getattr(obj, '__module__', None) != unique_module_name:
                continue
            if '_serialized_fields_' in obj.__dict__:
                clear_serialized_fields_cache(obj)
                obj._serialized_fields_ = {}
        sys.modules.pop(unique_module_name, None)

    # Load module from file path
    spec = importlib.util.spec_from_file_location(unique_module_name, file_path)
    if spec is None or spec.loader is None:
        raise ScriptLoadError(f"Failed to create module spec for {file_path}")

    module = importlib.util.module_from_spec(spec)

    # Add parent directory to sys.path temporarily
    parent_dir = os.path.dirname(file_path)
    old_path = sys.path.copy()
    if parent_dir not in sys.path:
        sys.path.insert(0, parent_dir)

    # Execute the module — catch errors so a broken script never crashes the editor
    try:
        spec.loader.exec_module(module)
    except Exception as exc:
        # Restore sys.path before propagating
        sys.path = old_path
        # Track this script as having a load error
        _record_script_error(file_path, exc)
        # Return empty list — the component can still be referenced by GUID/type
        # but will not be instantiable until the script is fixed.
        return []
    finally:
        # Always restore sys.path even on success
        sys.path = old_path

    # If we get here the script loaded successfully — clear any prior error
    _clear_script_error(file_path)

    # Store module in sys.modules for proper importing
    sys.modules[unique_module_name] = module

    # Find all InfComponent subclasses in the module
    components = []
    for name, obj in inspect.getmembers(module, inspect.isclass):
        # Check if it's a subclass of InfComponent (but not InfComponent itself)
        if issubclass(obj, InfComponent) and obj is not InfComponent:
            # Ensure it's defined in this module (not imported)
            if obj.__module__ == unique_module_name:
                components.append(obj)

    return components



def create_component_instance(component_class: Type[InfComponent]) -> InfComponent:
    """
    Create an instance of a component class.
    
    Args:
        component_class: The InfComponent subclass to instantiate
        
    Returns:
        New instance of the component
        
    Raises:
        ScriptLoadError: If instantiation fails
    """
    return component_class()


def load_and_create_component(file_path: str, asset_database=None) -> Optional[InfComponent]:
    """
    Convenience function: Load first component from file and create instance.
    
    Args:
        file_path: Absolute path to the .py file
        
    Returns:
        New instance of the first component found, or None if the script
        has errors (the error is already logged to Console).
        
    Note:
        Requires AssetDatabase for GUID-only component references.

    Raises:
        ScriptLoadError: If AssetDatabase is missing or GUID cannot be resolved.
    """
    if asset_database is None:
        raise ScriptLoadError("AssetDatabase is required for script components (GUID-only mode)")

    components = load_all_components_from_file(file_path)
    if not components:
        # Script had errors or contains no InfComponent subclasses.
        # Error was already logged by load_all_components_from_file / _record_script_error.
        return None

    component_class = components[0]
    instance = create_component_instance(component_class)
    # Resolve and store script GUID
    guid = asset_database.get_guid_from_path(file_path)
    if not guid:
        guid = asset_database.import_asset(file_path)
    if not guid:
        raise ScriptLoadError(f"Failed to resolve GUID for script: {file_path}")
    instance._script_guid = guid
    return instance


def get_component_info(component_class: Type[InfComponent]) -> dict:
    """
    Extract metadata from a component class.
    
    Args:
        component_class: The InfComponent subclass
        
    Returns:
        Dictionary with component metadata (name, docstring, fields)
    """
    from .serialized_field import get_serialized_fields
    
    return {
        'name': component_class.__name__,
        'module': component_class.__module__,
        'docstring': inspect.getdoc(component_class) or "",
        'fields': list(get_serialized_fields(component_class).keys()),
    }


# Example usage (for testing):
if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1:
        script_path = sys.argv[1]
        print(f"Loading components from: {script_path}")

        components = load_all_components_from_file(script_path)
        print(f"Found {len(components)} component(s):")

        for comp_class in components:
            info = get_component_info(comp_class)
            print(f"\n  - {info['name']}")
            print(f"    Doc: {info['docstring'][:50]}...")
            print(f"    Fields: {info['fields']}")

            # Try to instantiate
            instance = create_component_instance(comp_class)
            print(f"    [OK] Instantiation successful")

    else:
        print("Usage: python script_loader.py <path_to_script.py>")
