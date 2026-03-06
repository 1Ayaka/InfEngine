"""
Collider — Abstract base class for all collider BuiltinComponent wrappers.

Mirrors Unity's ``Collider`` base class. Concrete subclasses
(``BoxCollider``, ``SphereCollider``, ``CapsuleCollider``) inherit
the shared ``center``, ``is_trigger`` properties and category.

Example::

    from InfEngine.components.builtin import BoxCollider

    class MyScript(InfComponent):
        def start(self):
            col = self.get_component(BoxCollider)
            if isinstance(col, Collider):
                print("It's a collider!")
"""

from __future__ import annotations

from InfEngine.components.builtin_component import BuiltinComponent, CppProperty
from InfEngine.components.serialized_field import FieldType


class Collider(BuiltinComponent):
    """Abstract Python base for all collider wrappers (mirrors Unity's Collider).

    Subclasses must still set ``_cpp_type_name`` to their concrete C++ type
    (e.g. ``"BoxCollider"``).  This class itself is **not** registered in
    ``_builtin_registry`` because ``_cpp_type_name`` is left empty.
    """

    # Not a concrete component — don't register
    _cpp_type_name = ""

    _component_category_ = "Physics"

    # Always-draw flag inherited by subclasses
    always_show = False

    # ---- Shared properties (common to all collider types) ----
    center = CppProperty(
        "center",
        FieldType.VEC3,
        default=None,
        tooltip="Center offset in local space",
    )
    is_trigger = CppProperty(
        "is_trigger",
        FieldType.BOOL,
        default=False,
        tooltip="Is this collider a trigger volume?",
    )
    friction = CppProperty(
        "friction",
        FieldType.FLOAT,
        default=0.4,
        tooltip="Dynamic friction coefficient [0..1]",
        range=(0.0, 1.0),
    )
    bounciness = CppProperty(
        "bounciness",
        FieldType.FLOAT,
        default=0.0,
        tooltip="Bounciness / restitution [0..1]",
        range=(0.0, 1.0),
    )
