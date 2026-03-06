"""Type stubs for InfEngine.core."""

from __future__ import annotations

from .material import Material
from .texture import Texture
from .mesh import MeshData, VertexData
from .shader import Shader
from .resource_manager import CoreResourceManager

__all__ = [
    "Material",
    "Texture",
    "MeshData",
    "VertexData",
    "Shader",
    "CoreResourceManager",
]
