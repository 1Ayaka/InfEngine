"""
Built-in component wrappers — Python InfComponent facades for C++ components.

Provides ``Light``, ``MeshRenderer``, ``Camera``, ``BoxCollider``,
``SphereCollider``, ``CapsuleCollider``, and ``MeshCollider`` as first-class
InfComponent subclasses.  All state lives in the C++ component; CppProperty
descriptors delegate reads / writes transparently.

Usage::

    from InfEngine.components.builtin import Light, MeshRenderer, Camera
    from InfEngine.components.builtin import BoxCollider, SphereCollider, CapsuleCollider, MeshCollider
    from InfEngine.components import InfComponent

    class MyScript(InfComponent):
        def start(self):
            light = self.get_component(Light)
            light.intensity = 2.0
"""

from .light import Light
from .mesh_renderer import MeshRenderer
from .camera import Camera
from .collider import Collider
from .box_collider import BoxCollider
from .sphere_collider import SphereCollider
from .capsule_collider import CapsuleCollider
from .mesh_collider import MeshCollider
from .rigidbody import Rigidbody
from .audio_source import AudioSource
from .audio_listener import AudioListener

__all__ = [
    "Light",
    "MeshRenderer",
    "Camera",
    "Collider",
    "BoxCollider",
    "SphereCollider",
    "CapsuleCollider",
    "MeshCollider",
    "Rigidbody",
    "AudioSource",
    "AudioListener",
]
