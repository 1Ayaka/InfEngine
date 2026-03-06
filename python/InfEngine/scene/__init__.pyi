"""Type stubs for InfEngine.scene — Unity-style scene query & management."""

from __future__ import annotations

from typing import List, Optional, Union

from InfEngine.lib._InfEngine import GameObject, Scene, TagLayerManager


class GameObjectQuery:
    """Static helper methods for Unity-style GameObject queries."""

    @staticmethod
    def find(name: str) -> Optional[GameObject]: ...
    @staticmethod
    def find_with_tag(tag: str) -> Optional[GameObject]: ...
    @staticmethod
    def find_game_objects_with_tag(tag: str) -> List[GameObject]: ...
    @staticmethod
    def find_game_objects_in_layer(layer: int) -> List[GameObject]: ...
    @staticmethod
    def find_by_id(object_id: int) -> Optional[GameObject]: ...


class LayerMask:
    """Unity-style layer mask utilities (32-bit bitmask)."""

    @staticmethod
    def get_mask(*layer_names: str) -> int: ...
    @staticmethod
    def layer_to_name(layer: int) -> str: ...
    @staticmethod
    def name_to_layer(name: str) -> int: ...


class SceneManager:
    """Unity-style scene management API (aligned with SceneManagement.SceneManager)."""

    @staticmethod
    def load_scene(scene: Union[int, str]) -> bool: ...
    @staticmethod
    def get_active_scene() -> Optional[Scene]: ...
    @staticmethod
    def get_scene_count() -> int: ...
    @staticmethod
    def get_scene_name(build_index: int) -> Optional[str]: ...
    @staticmethod
    def get_scene_path(build_index: int) -> Optional[str]: ...
    @staticmethod
    def get_build_index(name: str) -> int: ...
    @staticmethod
    def get_all_scene_names() -> List[str]: ...
    @staticmethod
    def get_scene_by_name(name: str) -> Optional[str]: ...
    @staticmethod
    def get_scene_by_build_index(build_index: int) -> Optional[str]: ...
    @staticmethod
    def get_scene_at(index: int) -> Optional[str]: ...
    @staticmethod
    def scene_count_value() -> int: ...
    @staticmethod
    def process_pending_load() -> None: ...


__all__ = [
    "GameObjectQuery",
    "LayerMask",
    "TagLayerManager",
    "SceneManager",
]
