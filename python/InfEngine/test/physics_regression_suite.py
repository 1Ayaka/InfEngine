"""
Manual physics regression scenarios for Play Mode.

Attach one of the components in this file to an empty GameObject, enter Play
Mode, and watch the Console for PASS / FAIL logs.

Covered by script:
- trigger toggle
- sleep / wake
- static move wake-up
- mesh collider floor
- layer collision matrix

Covered by manual checklist:
- play mode enter / exit restore
- scene reload + body rebuild
"""

from __future__ import annotations

from InfEngine.components import InfComponent
from InfEngine.debug import debug
from InfEngine.lib import ForceMode, PrimitiveType, SceneManager, TagLayerManager, vec3f


PASS = "[PhysicsRegression] PASS"
FAIL = "[PhysicsRegression] FAIL"
INFO = "[PhysicsRegression]"


def _v3(x: float, y: float, z: float):
    return vec3f(float(x), float(y), float(z))


def _scene():
    return SceneManager.instance().get_active_scene()


def _make_floor(scene, y: float = 0.0):
    floor = scene.create_primitive(PrimitiveType.Cube, "RegressionFloor")
    floor.transform.position = _v3(0.0, y - 0.5, 0.0)
    floor.transform.local_scale = _v3(20.0, 1.0, 20.0)
    floor.add_component("BoxCollider")
    return floor


class _TriggerProbe(InfComponent):
    enter_count = 0
    exit_count = 0

    def on_trigger_enter(self, other):
        _TriggerProbe.enter_count += 1

    def on_trigger_exit(self, other):
        _TriggerProbe.exit_count += 1


class TriggerToggleRegression(InfComponent):
    """Verify runtime `is_trigger` toggle wakes the resting rigidbody and emits trigger enter."""

    def start(self):
        scene = _scene()
        if scene is None:
            debug.log_error(f"{FAIL} TriggerToggleRegression: no active scene")
            return

        _TriggerProbe.enter_count = 0
        _TriggerProbe.exit_count = 0
        _make_floor(scene)

        self._done = False
        self._timer = 0.0
        self._toggled = False

        platform = scene.create_primitive(PrimitiveType.Cube, "TriggerTogglePlatform")
        platform.transform.position = _v3(0.0, 1.0, 0.0)
        platform.transform.local_scale = _v3(8.0, 1.0, 8.0)
        self._platform_collider = platform.add_component("BoxCollider")
        platform.add_py_component(_TriggerProbe())

        falling = scene.create_primitive(PrimitiveType.Cube, "TriggerToggleFaller")
        falling.transform.position = _v3(0.0, 5.0, 0.0)
        falling.transform.local_scale = _v3(1.0, 1.0, 1.0)
        falling.add_component("BoxCollider")
        self._rb = falling.add_component("Rigidbody")
        self._faller = falling

    def update(self, dt: float):
        if self._done or self._rb is None:
            return

        self._timer += dt
        if (not self._toggled) and self._timer >= 1.2:
            self._platform_collider.is_trigger = True
            self._toggled = True
            debug.log(f"{INFO} TriggerToggleRegression: toggled platform.is_trigger = True")

        if self._toggled and self._faller.transform.position.y < 0.2:
            if _TriggerProbe.enter_count > 0:
                debug.log(f"{PASS} TriggerToggleRegression: trigger_enter={_TriggerProbe.enter_count}, y={self._faller.transform.position.y:.2f}")
            else:
                debug.log_error(f"{FAIL} TriggerToggleRegression: rigidbody fell but trigger_enter was not observed")
            self._done = True
        elif self._timer > 5.0:
            debug.log_error(
                f"{FAIL} TriggerToggleRegression: timeout, enter_count={_TriggerProbe.enter_count}, y={self._faller.transform.position.y:.2f}"
            )
            self._done = True


class SleepWakeRegression(InfComponent):
    """Verify a sleeping body can be explicitly woken and reacts to force again."""

    def start(self):
        scene = _scene()
        if scene is None:
            debug.log_error(f"{FAIL} SleepWakeRegression: no active scene")
            return

        _make_floor(scene)
        self._done = False
        self._slept_once = False
        self._wake_pos_x = None
        self._timeout = 0.0

        box = scene.create_primitive(PrimitiveType.Cube, "SleepWakeBox")
        box.transform.position = _v3(0.0, 4.0, 0.0)
        box.add_component("BoxCollider")
        self._rb = box.add_component("Rigidbody")
        self._box = box

    def update(self, dt: float):
        if self._done or self._rb is None:
            return

        self._timeout += dt
        if (not self._slept_once) and self._timeout > 1.2 and self._rb.is_sleeping():
            self._slept_once = True
            self._wake_pos_x = self._box.transform.position.x
            self._rb.wake_up()
            self._rb.add_force(_v3(8.0, 2.5, 0.0), ForceMode.Impulse)
            debug.log(f"{INFO} SleepWakeRegression: body slept, wake_up + impulse applied")

        if self._slept_once and self._box.transform.position.x > (self._wake_pos_x + 0.35):
            debug.log(f"{PASS} SleepWakeRegression: body moved after wake_up")
            self._done = True
        elif self._timeout > 6.0:
            debug.log_error(f"{FAIL} SleepWakeRegression: timeout, slept={self._slept_once}, x={self._box.transform.position.x:.2f}")
            self._done = True


class StaticMoveWakeRegression(InfComponent):
    """Verify moving a static collider wakes a resting rigidbody on top of it."""

    def start(self):
        scene = _scene()
        if scene is None:
            debug.log_error(f"{FAIL} StaticMoveWakeRegression: no active scene")
            return

        _make_floor(scene, y=-6.0)
        self._done = False
        self._slept = False
        self._started_move = False
        self._timeout = 0.0

        platform = scene.create_primitive(PrimitiveType.Cube, "WakePlatform")
        platform.transform.position = _v3(0.0, 1.0, 0.0)
        platform.transform.local_scale = _v3(8.0, 1.0, 8.0)
        platform.add_component("BoxCollider")
        self._platform = platform

        box = scene.create_primitive(PrimitiveType.Cube, "WakeBox")
        box.transform.position = _v3(0.0, 3.5, 0.0)
        box.add_component("BoxCollider")
        self._rb = box.add_component("Rigidbody")
        self._box = box
        self._start_x = None

    def update(self, dt: float):
        if self._done or self._rb is None:
            return

        self._timeout += dt
        if (not self._slept) and self._timeout > 1.2 and self._rb.is_sleeping():
            self._slept = True
            self._start_x = self._box.transform.position.x
            debug.log(f"{INFO} StaticMoveWakeRegression: box is sleeping on platform")

        if self._slept and not self._started_move:
            self._started_move = True
            self._move_elapsed = 0.0

        if self._started_move:
            self._move_elapsed += dt
            self._platform.transform.position = _v3(self._move_elapsed * 2.0, 1.0, 0.0)

        if self._started_move and (self._box.transform.position.x > self._start_x + 0.2 or self._box.transform.position.y < 2.5):
            debug.log(
                f"{PASS} StaticMoveWakeRegression: box reacted to static platform move (x={self._box.transform.position.x:.2f}, y={self._box.transform.position.y:.2f})"
            )
            self._done = True
        elif self._timeout > 6.0:
            debug.log_error(
                f"{FAIL} StaticMoveWakeRegression: timeout, slept={self._slept}, x={self._box.transform.position.x:.2f}, y={self._box.transform.position.y:.2f}"
            )
            self._done = True


class MeshColliderRegression(InfComponent):
    """Verify a static `MeshCollider` built from primitive mesh geometry blocks a falling body."""

    def start(self):
        scene = _scene()
        if scene is None:
            debug.log_error(f"{FAIL} MeshColliderRegression: no active scene")
            return

        self._done = False
        self._timeout = 0.0

        floor = scene.create_primitive(PrimitiveType.Cube, "MeshColliderFloor")
        floor.transform.position = _v3(0.0, -0.5, 0.0)
        floor.transform.local_scale = _v3(12.0, 1.0, 12.0)
        self._mesh_collider = floor.add_component("MeshCollider")

        box = scene.create_primitive(PrimitiveType.Cube, "MeshColliderBox")
        box.transform.position = _v3(0.0, 5.0, 0.0)
        box.add_component("BoxCollider")
        self._rb = box.add_component("Rigidbody")
        self._box = box

    def update(self, dt: float):
        if self._done:
            return

        self._timeout += dt
        y = self._box.transform.position.y
        if self._timeout > 1.0 and self._rb.is_sleeping() and y > 0.25:
            debug.log(f"{PASS} MeshColliderRegression: dynamic body settled on mesh collider at y={y:.2f}")
            self._done = True
        elif self._timeout > 5.0:
            debug.log_error(f"{FAIL} MeshColliderRegression: timeout, box y={y:.2f}")
            self._done = True


class LayerMatrixRegression(InfComponent):
    """Verify the project layer collision matrix can disable broadphase interaction between two layers."""

    def start(self):
        scene = _scene()
        if scene is None:
            debug.log_error(f"{FAIL} LayerMatrixRegression: no active scene")
            return

        self._done = False
        self._timeout = 0.0
        self._layer_a = 8
        self._layer_b = 9
        self._tag_layers = TagLayerManager.instance()
        self._previous = self._tag_layers.get_layers_collide(self._layer_a, self._layer_b)
        self._tag_layers.set_layer_name(self._layer_a, "RegressionA")
        self._tag_layers.set_layer_name(self._layer_b, "RegressionB")
        self._tag_layers.set_layers_collide(self._layer_a, self._layer_b, False)

        floor = scene.create_primitive(PrimitiveType.Cube, "LayerMatrixFloor")
        floor.transform.position = _v3(0.0, -0.5, 0.0)
        floor.transform.local_scale = _v3(12.0, 1.0, 12.0)
        floor.layer = self._layer_a
        floor.add_component("BoxCollider")

        box = scene.create_primitive(PrimitiveType.Cube, "LayerMatrixBox")
        box.transform.position = _v3(0.0, 5.0, 0.0)
        box.layer = self._layer_b
        box.add_component("BoxCollider")
        self._rb = box.add_component("Rigidbody")
        self._box = box

    def on_destroy(self):
        if hasattr(self, "_tag_layers") and self._tag_layers is not None:
            self._tag_layers.set_layers_collide(self._layer_a, self._layer_b, self._previous)

    def update(self, dt: float):
        if self._done:
            return

        self._timeout += dt
        y = self._box.transform.position.y
        if y < -1.5:
            debug.log(f"{PASS} LayerMatrixRegression: non-colliding layers allowed body to pass through floor")
            self._done = True
        elif self._timeout > 4.0:
            debug.log_error(f"{FAIL} LayerMatrixRegression: body still blocked, y={y:.2f}")
            self._done = True
