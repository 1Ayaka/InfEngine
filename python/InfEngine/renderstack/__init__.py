"""
InfEngine RenderStack Module

Scene-level rendering configuration system.
Provides a composable, per-scene rendering stack where users can mount
arbitrary render passes to pipeline-defined injection points.

Core classes:
    - **RenderStack**: Scene-singleton component managing pipeline + passes
    - **RenderPipeline**: Topology skeleton with named injection points
    - **RenderPass**: Base class for mountable rendering steps
    - **GeometryPass**: Scene geometry drawing (outline, decal, etc.)
    - **ResourceBus**: Transient resource handle dictionary
    - **InjectionPoint**: Named slot in the pipeline topology

Architecture::

    Scene
    └── RenderStack (InfComponent)
        ├── selected_pipeline: RenderPipeline
        │   └── define_topology(graph, bus, on_injection_point)
        └── pass_entries: [PassEntry, ...]
            └── each: RenderPass.inject(graph, bus)

Quick start::

    from InfEngine.renderstack import RenderStack

    # Mount to scene's RenderStack
    stack = game_object.add_component(RenderStack)

See Also:
    - ``docs/design/RenderStack_Design.md`` for the full design document
"""

from InfEngine.renderstack.injection_point import InjectionPoint
from InfEngine.renderstack.resource_bus import ResourceBus
from InfEngine.renderstack.render_pass import RenderPass
from InfEngine.renderstack.geometry_pass import GeometryPass
from InfEngine.renderstack.fullscreen_effect import FullScreenEffect
from InfEngine.renderstack.bloom_effect import BloomEffect
from InfEngine.renderstack.render_stack import RenderStack, PassEntry
from InfEngine.renderstack.render_stack_pipeline import RenderStackPipeline
from InfEngine.renderstack.default_forward_pipeline import DefaultForwardPipeline
from InfEngine.renderstack.builtin_passes import BUILTIN_PASSES
from InfEngine.renderstack.discovery import discover_pipelines, discover_passes

__all__ = [
    # Core
    "RenderStack",
    "PassEntry",
    "RenderStackPipeline",
    "DefaultForwardPipeline",
    # Injection points
    "InjectionPoint",
    # Resource bus
    "ResourceBus",
    # Pass base classes
    "RenderPass",
    "GeometryPass",
    "FullScreenEffect",
    "BloomEffect",
    # Built-in passes (registry)
    "BUILTIN_PASSES",
    # Discovery
    "discover_pipelines",
    "discover_passes",
]