"""Type stubs for InfEngine.rendergraph.graph."""

from __future__ import annotations

from enum import IntEnum
from typing import Dict, List, Optional, Set, Tuple, Union

from InfEngine.lib._InfEngine import RenderGraphDescription
from InfEngine.renderstack.injection_point import InjectionPoint


class Format(IntEnum):
    """Common texture formats for RenderGraph resources (maps to VkFormat)."""
    RGBA8_UNORM = 37
    RGBA8_SRGB = 43
    BGRA8_UNORM = 44
    RGBA16_SFLOAT = 97
    RGBA32_SFLOAT = 109
    R32_SFLOAT = 100
    D32_SFLOAT = 126
    D24_UNORM_S8_UINT = 129

    @property
    def is_depth(self) -> bool:
        """Check if this is a depth/stencil format."""
        ...


class TextureHandle:
    """Opaque handle to a texture resource in the RenderGraph."""
    name: str
    format: Format
    is_camera_target: bool
    size: Optional[Tuple[int, int]]

    def __init__(self, name: str, format: Format, is_camera_target: bool = ...,
                 size: Optional[Tuple[int, int]] = ...) -> None: ...
    @property
    def is_depth(self) -> bool: ...
    def __repr__(self) -> str: ...
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...


class RenderPassBuilder:
    """Builder for configuring a single render pass.

    All resource args accept a string alias or ``TextureHandle``.

    Example::

        with graph.add_pass("OpaquePass") as p:
            p.write_color("color")
            p.draw_renderers(queue_range=(0, 2500))
    """

    def __init__(self, name: str, graph: RenderGraph | None = ...) -> None: ...
    @property
    def name(self) -> str: ...

    # Resource declarations
    def read(self, texture: Union[str, TextureHandle]) -> RenderPassBuilder: ...
    def write_color(self, texture: Union[str, TextureHandle], slot: int = ...) -> RenderPassBuilder: ...
    def write_depth(self, texture: Union[str, TextureHandle]) -> RenderPassBuilder: ...
    def set_input(self, sampler_name: str, texture: Union[str, TextureHandle]) -> RenderPassBuilder: ...

    # Clear settings
    def set_clear(
        self,
        color: Optional[Tuple[float, float, float, float]] = ...,
        depth: Optional[float] = ...,
    ) -> RenderPassBuilder: ...

    # Render actions
    def draw_renderers(
        self,
        queue_range: Tuple[int, int] = ...,
        sort_mode: str = ...,
    ) -> RenderPassBuilder: ...
    def draw_skybox(self) -> RenderPassBuilder: ...
    def draw_shadow_casters(
        self,
        queue_range: Tuple[int, int] = ...,
        light_index: int = ...,
        shadow_type: str = ...,
    ) -> RenderPassBuilder:
        """Configure this pass to render shadow casters (depth-only shadow map)."""
        ...

    # Context manager
    def __enter__(self) -> RenderPassBuilder: ...
    def __exit__(self, *args: object) -> None: ...
    def __repr__(self) -> str: ...


class RenderGraph:
    """Python-side RenderGraph topology builder.

    Example::

        graph = RenderGraph("ForwardPipeline")
        graph.create_texture("color", camera_target=True)
        graph.create_texture("depth", format=Format.D32_SFLOAT)

        with graph.add_pass("OpaquePass") as p:
            p.write_color("color")
            p.write_depth("depth")
            p.set_clear(color=(0.1, 0.1, 0.1, 1.0), depth=1.0)
            p.draw_renderers(queue_range=(0, 2500), sort_mode="front_to_back")

        graph.injection_point("after_opaque", resources={"color", "depth"})
        graph.set_output("color")
        desc = graph.build()
    """

    def __init__(self, name: str = ...) -> None: ...
    @property
    def name(self) -> str: ...
    @property
    def pass_count(self) -> int: ...
    @property
    def texture_count(self) -> int: ...
    @property
    def topology_sequence(self) -> List[Tuple[str, str]]: ...
    @property
    def injection_points(self) -> List[InjectionPoint]: ...

    def create_texture(
        self,
        name: str,
        *,
        format: Format = ...,
        camera_target: bool = ...,
        size: Optional[Tuple[int, int]] = ...,
    ) -> TextureHandle:
        """Create a texture resource (unified method)."""
        ...
    def get_texture(self, name: str) -> Optional[TextureHandle]:
        """Look up a texture by its string alias."""
        ...
    def injection_point(
        self,
        name: str,
        *,
        display_name: str = ...,
        resources: Optional[Set[str]] = ...,
    ) -> None:
        """Declare an injection point at the current topology position."""
        ...
    def add_pass(self, name: str) -> RenderPassBuilder:
        """Add a render pass to the graph."""
        ...
    def set_output(self, texture: Union[str, TextureHandle]) -> None:
        """Mark a texture as the final graph output."""
        ...
    def validate_no_ip_before_first_pass(self) -> None:
        """Raise ``ValueError`` if an IP precedes the first pass."""
        ...
    def build(self) -> RenderGraphDescription:
        """Build the graph into a ``RenderGraphDescription``."""
        ...
    def get_debug_string(self) -> str:
        """Get a human-readable representation of the graph."""
        ...
    def __repr__(self) -> str: ...
