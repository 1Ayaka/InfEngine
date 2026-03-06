"""Type stubs for InfEngine.renderstack."""

from __future__ import annotations

from typing import Dict, Type

from InfEngine.renderstack.injection_point import InjectionPoint as InjectionPoint
from InfEngine.renderstack.resource_bus import ResourceBus as ResourceBus
from InfEngine.renderstack.render_pass import RenderPass as RenderPass
from InfEngine.renderstack.geometry_pass import GeometryPass as GeometryPass
from InfEngine.renderstack.fullscreen_effect import FullScreenEffect as FullScreenEffect
from InfEngine.renderstack.bloom_effect import BloomEffect as BloomEffect
from InfEngine.renderstack.tonemapping_effect import ToneMappingEffect as ToneMappingEffect
from InfEngine.renderstack.render_stack import RenderStack as RenderStack
from InfEngine.renderstack.render_stack import PassEntry as PassEntry
from InfEngine.renderstack.render_stack_pipeline import RenderStackPipeline as RenderStackPipeline
from InfEngine.renderstack.default_forward_pipeline import DefaultForwardPipeline as DefaultForwardPipeline
from InfEngine.renderstack.builtin_passes import BUILTIN_PASSES as BUILTIN_PASSES
from InfEngine.renderstack.discovery import discover_pipelines as discover_pipelines
from InfEngine.renderstack.discovery import discover_passes as discover_passes

__all__ = [
    "RenderStack",
    "PassEntry",
    "RenderStackPipeline",
    "DefaultForwardPipeline",
    "InjectionPoint",
    "ResourceBus",
    "RenderPass",
    "GeometryPass",
    "BUILTIN_PASSES",
    "discover_pipelines",
    "discover_passes",
]
