"""Type stubs for InfEngine.renderstack.injection_point."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Set


@dataclass
class InjectionPoint:
    """Pipeline 中的一个命名注入位置。"""

    name: str
    display_name: str = ""
    description: str = ""
    resource_state: Set[str] = ...
    removable: bool = True
