"""
physics_benchmark_1024_cubes.py
────────────────────────────────
Spawn cubes at random positions within a configurable range.
One cube is spawned per frame with BoxCollider + Rigidbody so it
falls naturally and gradually increases physics load.

Usage — attach this to any GameObject as an InfComponent:
    The cubes are created in ``start()`` and frame time is printed
    periodically in ``update()``.

Alternatively, you can run the ``setup_benchmark_scene()`` function
directly from the editor console.
"""

from InfEngine.components import InfComponent
from InfEngine.lib import SceneManager, PrimitiveType, vec3f
from InfEngine.debug import debug
import random
import time


# ──────────────────────────────────────────────────────────────
# Helper: Create floor in the active scene
# ──────────────────────────────────────────────────────────────
def setup_benchmark_scene():
    """Create benchmark floor in the active scene.

    Returns active scene, or None if no active scene exists.
    """
    sm = SceneManager.instance()
    scene = sm.get_active_scene()
    if scene is None:
        debug.log_error("[Benchmark] No active scene!")
        return None

    # ── Floor ──
    floor = scene.create_primitive(PrimitiveType.Plane, "BenchFloor")
    floor.transform.local_scale = vec3f(100, 1, 100)
    floor.transform.position = vec3f(0, 0, 0)
    floor.add_component("BoxCollider")
    return scene


# ──────────────────────────────────────────────────────────────
# InfComponent that sets up the scene at play-time and logs FPS
# ──────────────────────────────────────────────────────────────
class PhysicsBenchmark(InfComponent):
    cube_count = 1024
    spawn_range = 32.0
    height = 50.0

    def start(self):
        self._scene = setup_benchmark_scene()
        self._spawned_count = 0
        self._start_time = time.perf_counter()

    def _spawn_one_cube(self):
        if self._scene is None:
            return
        cube = self._scene.create_primitive(PrimitiveType.Cube, f"Cube_{self._spawned_count}")
        x = random.uniform(-self.spawn_range, self.spawn_range)
        z = random.uniform(-self.spawn_range, self.spawn_range)
        cube.transform.position = vec3f(x, self.height, z)
        cube.add_component("BoxCollider")
        cube.add_component("Rigidbody")
        self._spawned_count += 1

    def update(self, dt: float):
        if self._spawned_count < self.cube_count:
            self._spawn_one_cube()

        fps = (1.0 / dt) if dt > 1e-6 else 0.0
        wall = time.perf_counter() - self._start_time
        debug.log(f"[Benchmark] FPS={fps:.1f}, spawned={self._spawned_count}/{self.cube_count}, wall={wall:.1f}s")
