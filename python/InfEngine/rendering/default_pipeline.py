"""
Default forward-rendering pipeline.

The canonical pipeline is ``DefaultForwardPipeline`` from the
``renderstack`` module. It supports the injection-point system
for composable post-processing via ``RenderStack``.

Usage::

    from InfEngine.renderstack import RenderStackPipeline
    engine.set_render_pipeline(RenderStackPipeline())
"""

from InfEngine.renderstack.default_forward_pipeline import (
    DefaultForwardPipeline,
)

__all__ = [
    "DefaultForwardPipeline",
]
