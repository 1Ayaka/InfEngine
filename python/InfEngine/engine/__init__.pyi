"""Type stubs for InfEngine.engine."""

from __future__ import annotations

from InfEngine.lib._InfEngine import (
    InfGUIContext,
    InfGUIRenderable,
    LogLevel,
    TextureData,
    TextureLoader,
)
from InfEngine.resources import engine_font_path as engine_font_path
from InfEngine.resources import icon_path as icon_path

from .engine import Engine
from .play_mode import PlayModeManager, PlayModeState, PlayModeEvent
from .resources_manager import ResourcesManager
from .scene_manager import SceneFileManager
from .ui import (
    MenuBarPanel,
    ToolbarPanel,
    HierarchyPanel,
    InspectorPanel,
    ConsolePanel,
    SceneViewPanel,
    GameViewPanel,
    ProjectPanel,
    WindowManager,
    TagLayerSettingsPanel,
    StatusBarPanel,
    BuildSettingsPanel,
)

def release_engine(project_path: str, engine_log_level: LogLevel = ...) -> None:
    """Launch InfEngine with Unity-style editor layout."""
    ...

__all__ = [
    "Engine",
    "LogLevel",
    "InfGUIRenderable",
    "InfGUIContext",
    "TextureLoader",
    "TextureData",
    "PlayModeManager",
    "PlayModeState",
    "PlayModeEvent",
    "ResourcesManager",
    "SceneFileManager",
    "MenuBarPanel",
    "ToolbarPanel",
    "HierarchyPanel",
    "InspectorPanel",
    "ConsolePanel",
    "SceneViewPanel",
    "GameViewPanel",
    "ProjectPanel",
    "WindowManager",
    "TagLayerSettingsPanel",
    "StatusBarPanel",
    "BuildSettingsPanel",
    "engine_font_path",
    "icon_path",
    "release_engine",
]
