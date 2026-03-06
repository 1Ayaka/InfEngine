"""
Rigidbody — Python BuiltinComponent wrapper for C++ Rigidbody.

Mirrors Unity's ``Rigidbody`` component. When attached alongside a Collider,
the Collider's body becomes dynamic (affected by gravity, forces, etc.).

Example::

    from InfEngine.components.builtin import Rigidbody
    from InfEngine.math import Vector3

    class MyScript(InfComponent):
        def start(self):
            rb = self.get_component(Rigidbody)
            rb.mass = 2.0
            rb.use_gravity = True

        def fixed_update(self, dt):
            rb = self.get_component(Rigidbody)
            rb.add_force(Vector3(0, 10, 0))
"""

from __future__ import annotations

from InfEngine.components.builtin_component import BuiltinComponent, CppProperty
from InfEngine.components.serialized_field import FieldType


class Rigidbody(BuiltinComponent):
    """Python wrapper for the C++ Rigidbody component."""

    _cpp_type_name = "Rigidbody"

    _component_category_ = "Physics"

    # ---- Serialized properties (displayed in Inspector) ----

    mass = CppProperty(
        "mass",
        FieldType.FLOAT,
        default=1.0,
        tooltip="Mass in kilograms",
        range=(0.001, 1000.0),
    )

    drag = CppProperty(
        "drag",
        FieldType.FLOAT,
        default=0.0,
        tooltip="Linear drag coefficient",
        range=(0.0, 100.0),
    )

    angular_drag = CppProperty(
        "angular_drag",
        FieldType.FLOAT,
        default=0.05,
        tooltip="Angular drag coefficient",
        range=(0.0, 100.0),
    )

    use_gravity = CppProperty(
        "use_gravity",
        FieldType.BOOL,
        default=True,
        tooltip="Should this rigidbody be affected by gravity?",
    )

    is_kinematic = CppProperty(
        "is_kinematic",
        FieldType.BOOL,
        default=False,
        tooltip="If enabled, the object is not driven by physics but by script/animation",
    )

    constraints = CppProperty(
        "constraints",
        FieldType.INT,
        default=0,
        tooltip="Constraints bitmask (RigidbodyConstraints). 0=None, 14=FreezePosition, 112=FreezeRotation, 126=FreezeAll",
    )

    collision_detection_mode = CppProperty(
        "collision_detection_mode",
        FieldType.INT,
        default=0,
        tooltip="0=Discrete, 1=Continuous, 2=ContinuousDynamic, 3=ContinuousSpeculative",
        range=(0, 3),
    )

    interpolation = CppProperty(
        "interpolation",
        FieldType.INT,
        default=1,
        tooltip="0=None, 1=Interpolate. Smooths presentation between fixed physics steps.",
        range=(0, 1),
    )

    max_angular_velocity = CppProperty(
        "max_angular_velocity",
        FieldType.FLOAT,
        default=7.0,
        tooltip="Maximum angular velocity in rad/s",
        range=(0.0, 100.0),
    )

    max_linear_velocity = CppProperty(
        "max_linear_velocity",
        FieldType.FLOAT,
        default=500.0,
        tooltip="Maximum linear velocity in m/s",
        range=(0.0, 5000.0),
    )

    # ---- Convenience property: freeze_rotation ----

    @property
    def freeze_rotation(self) -> bool:
        """Shortcut to freeze all rotation axes."""
        cpp = self._cpp_component
        if cpp is None:
            return False
        return cpp.freeze_rotation

    @freeze_rotation.setter
    def freeze_rotation(self, value: bool):
        cpp = self._cpp_component
        if cpp is not None:
            cpp.freeze_rotation = value

    # ---- Runtime-only properties (not serialized via CppProperty, accessed via methods) ----

    @property
    def velocity(self):
        """Linear velocity in world space (Vector3)."""
        cpp = self._cpp_component
        if cpp is None:
            from InfEngine.math import Vector3
            return Vector3(0, 0, 0)
        return cpp.velocity

    @velocity.setter
    def velocity(self, value):
        cpp = self._cpp_component
        if cpp is not None:
            from InfEngine.lib import vec3f
            if not isinstance(value, vec3f):
                value = vec3f(value[0], value[1], value[2])
            cpp.velocity = value

    @property
    def angular_velocity(self):
        """Angular velocity in world space (Vector3)."""
        cpp = self._cpp_component
        if cpp is None:
            from InfEngine.math import Vector3
            return Vector3(0, 0, 0)
        return cpp.angular_velocity

    @angular_velocity.setter
    def angular_velocity(self, value):
        cpp = self._cpp_component
        if cpp is not None:
            from InfEngine.lib import vec3f
            if not isinstance(value, vec3f):
                value = vec3f(value[0], value[1], value[2])
            cpp.angular_velocity = value

    # ---- Read-only world info ----

    @property
    def world_center_of_mass(self):
        """World-space center of mass (read-only)."""
        cpp = self._cpp_component
        if cpp is None:
            from InfEngine.math import Vector3
            return Vector3(0, 0, 0)
        return cpp.world_center_of_mass

    @property
    def position(self):
        """World-space position of the rigidbody (read-only)."""
        cpp = self._cpp_component
        if cpp is None:
            from InfEngine.math import Vector3
            return Vector3(0, 0, 0)
        return cpp.position

    @property
    def rotation(self):
        """World-space rotation quaternion (x, y, z, w) (read-only)."""
        cpp = self._cpp_component
        if cpp is None:
            return (0.0, 0.0, 0.0, 1.0)
        return cpp.rotation

    # ---- Force / Torque API ----

    def add_force(self, force, mode=None):
        """Add a force to the rigidbody.

        Args:
            force: Force vector (Vector3 or tuple).
            mode: ForceMode enum value (default: ForceMode.Force).
        """
        cpp = self._cpp_component
        if cpp is None:
            return
        from InfEngine.lib import vec3f, ForceMode as _FM
        if not isinstance(force, vec3f):
            force = vec3f(force[0], force[1], force[2])
        if mode is None:
            mode = _FM.Force
        cpp.add_force(force, mode)

    def add_torque(self, torque, mode=None):
        """Add a torque to the rigidbody.

        Args:
            torque: Torque vector (Vector3 or tuple).
            mode: ForceMode enum value (default: ForceMode.Force).
        """
        cpp = self._cpp_component
        if cpp is None:
            return
        from InfEngine.lib import vec3f, ForceMode as _FM
        if not isinstance(torque, vec3f):
            torque = vec3f(torque[0], torque[1], torque[2])
        if mode is None:
            mode = _FM.Force
        cpp.add_torque(torque, mode)

    def add_force_at_position(self, force, position, mode=None):
        """Add a force at a world-space position.

        Args:
            force: Force vector (Vector3 or tuple).
            position: World-space point where force is applied.
            mode: ForceMode enum value (default: ForceMode.Force).
        """
        cpp = self._cpp_component
        if cpp is None:
            return
        from InfEngine.lib import vec3f, ForceMode as _FM
        if not isinstance(force, vec3f):
            force = vec3f(force[0], force[1], force[2])
        if not isinstance(position, vec3f):
            position = vec3f(position[0], position[1], position[2])
        if mode is None:
            mode = _FM.Force
        cpp.add_force_at_position(force, position, mode)

    # ---- Kinematic movement ----

    def move_position(self, position):
        """Move a kinematic body to target position (Unity: Rigidbody.MovePosition).

        Args:
            position: Target world-space position (Vector3 or tuple).
        """
        cpp = self._cpp_component
        if cpp is None:
            return
        from InfEngine.lib import vec3f
        if not isinstance(position, vec3f):
            position = vec3f(position[0], position[1], position[2])
        cpp.move_position(position)

    def move_rotation(self, rotation):
        """Rotate a kinematic body to target rotation (Unity: Rigidbody.MoveRotation).

        Args:
            rotation: Target rotation as (x, y, z, w) quaternion tuple.
        """
        cpp = self._cpp_component
        if cpp is None:
            return
        cpp.move_rotation(rotation)

    # ---- Sleep API ----

    def is_sleeping(self) -> bool:
        """Is the rigidbody sleeping?"""
        cpp = self._cpp_component
        if cpp is None:
            return True
        return cpp.is_sleeping()

    def wake_up(self):
        """Wake the rigidbody up."""
        cpp = self._cpp_component
        if cpp is not None:
            cpp.wake_up()

    def sleep(self):
        """Put the rigidbody to sleep."""
        cpp = self._cpp_component
        if cpp is not None:
            cpp.sleep()
