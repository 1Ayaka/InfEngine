"""Type stubs for InfEngine.math — re-exports vec2f, vec3f, vec4f from native module."""

from InfEngine.lib._InfEngine import vec2f as vec2f
from InfEngine.lib._InfEngine import vec3f as vec3f
from InfEngine.lib._InfEngine import vec4f as vec4f
from InfEngine.math.vector import vector2, vector3, vector4

__all__ = ["vector2", "vector3", "vector4"]
