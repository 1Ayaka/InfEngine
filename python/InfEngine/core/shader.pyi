"""Type stubs for InfEngine.core.shader."""

from __future__ import annotations

from typing import Optional


class Shader:
    """Static utility class for shader management.

    Example::

        Shader.reload("pbr_lit")
        if Shader.is_loaded("pbr_lit", "vertex"):
            print("Ready")
    """

    _engine: Optional[object]

    @classmethod
    def _set_engine(cls, engine: object) -> None: ...

    @classmethod
    def is_loaded(cls, name: str, shader_type: str = ...) -> bool:
        """Check if a shader is loaded in the cache."""
        ...

    @classmethod
    def invalidate(cls, shader_id: str) -> None:
        """Invalidate the shader program cache for hot-reload."""
        ...

    @classmethod
    def reload(cls, shader_id: str) -> bool:
        """Invalidate cache and refresh all materials using this shader.

        Returns:
            True if at least one material was refreshed.
        """
        ...

    @classmethod
    def refresh_materials(cls, shader_id: str, engine: Optional[object] = ...) -> bool:
        """Refresh all material pipelines that use a given shader."""
        ...

    @classmethod
    def load_spirv(cls, name: str, spirv_code: bytes, shader_type: str = ...) -> None:
        """Load a SPIR-V shader module into the engine.

        Raises:
            RuntimeError: If the shader system is not initialized.
        """
        ...
