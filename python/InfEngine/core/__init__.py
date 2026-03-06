"""
InfEngine Core Module

Provides Pythonic wrappers around the C++ engine core, establishing a clean
boundary between the C++ execution engine and the Python business logic layer.

Design Principles:
    - C++ handles: physics, rendering, memory management, resource I/O
    - Python handles: business logic, render graph topology, component scripting
    - All resource lifecycle managed via context managers or explicit acquire/release
    - AI/LLM-friendly API surface — clear, minimal, self-documenting

Usage::

    from InfEngine.core import Material, Texture, Mesh, Shader, ResourceManager

    # Context-managed resource lifecycle
    with Material.create("MyMaterial") as mat:
        mat.set_color("_BaseColor", 1.0, 0.0, 0.0)
        mat.set_float("_Metallic", 0.8)
        renderer.material = mat

    # Shader hot-reload
    Shader.reload("pbr_lit")

    # Render pipeline topology
    from InfEngine.rendergraph import RenderGraph, Format
"""

from .material import Material
from .texture import Texture
from .mesh import MeshData, VertexData
from .shader import Shader
from .audio_clip import AudioClip
from .resource_manager import CoreResourceManager
from .assets import AssetManager
from .asset_types import (
    TextureImportSettings, TextureType, WrapMode, FilterMode,
    ShaderAssetInfo, asset_category_from_extension,
    AudioImportSettings, AudioCompressionFormat,
    read_meta_file, write_meta_fields,
    read_texture_import_settings, write_texture_import_settings,
    read_audio_import_settings, write_audio_import_settings,
)
from .asset_ref import TextureRef, ShaderRef, AudioClipRef

__all__ = [
    "Material",
    "Texture",
    "MeshData",
    "VertexData",
    "Shader",
    "AudioClip",
    "CoreResourceManager",
    "AssetManager",
    "TextureImportSettings",
    "TextureType",
    "WrapMode",
    "FilterMode",
    "ShaderAssetInfo",
    "AudioImportSettings",
    "AudioCompressionFormat",
    "TextureRef",
    "ShaderRef",
    "AudioClipRef",
]
