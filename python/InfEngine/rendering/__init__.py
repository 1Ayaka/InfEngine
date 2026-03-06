"""
InfEngine Rendering Pipeline Module

Provides the Unity SRP-style rendering pipeline API:
- RenderPipelineAsset: factory for creating pipeline instances
- RenderPipeline: base class for defining custom rendering logic
  (``define_topology(graph)``, ``graph.injection_point()``)
- DefaultForwardPipeline: standard forward pipeline with injection-point
  support (via ``InfEngine.renderstack``)
"""

from .render_pipeline import RenderPipelineAsset, RenderPipeline
from .default_pipeline import DefaultForwardPipeline

__all__ = [
    "RenderPipelineAsset",
    "RenderPipeline",
    "DefaultForwardPipeline",
]
