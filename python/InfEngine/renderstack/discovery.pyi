"""Type stubs for InfEngine.renderstack.discovery."""

from __future__ import annotations

from typing import Dict


def discover_pipelines() -> Dict[str, type]:
    """Scan all loaded RenderPipeline subclasses.

    Returns ``{pipeline.name: pipeline_class}``.
    """
    ...


def discover_passes() -> Dict[str, type]:
    """Scan all loaded RenderPass subclasses.

    Returns ``{pass.name: pass_class}``.
    """
    ...
