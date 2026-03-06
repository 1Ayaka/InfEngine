# Gizmos

<div class="class-info">
类位于 <b>InfEngine.gizmos</b>
</div>

## 描述

Unity-style immediate-mode gizmo drawing.

All methods are class-level (static-ish).  State resets each frame
via ``_begin_frame()``, called by the collector.

Drawing primitives accumulate line-segment vertices into a shared
per-frame buffer.  The collector packs and uploads them to the C++
``GizmosDrawCallBuffer`` before ``SubmitCulling()``.

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## 属性

| 名称 | 类型 | 描述 |
|------|------|------|
| color | `Tuple[float, float, float]` |  *(只读)* |
| matrix | `Optional[List[float]]` |  *(只读)* |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## 静态方法

| 方法 | 描述 |
|------|------|
| `Gizmos.draw_line(start: Vec3, end: Vec3)` | Draw a single line segment from *start* to *end*. |
| `Gizmos.draw_ray(origin: Vec3, direction: Vec3)` | Draw a ray from *origin* in *direction* (magnitude = length). |
| `Gizmos.draw_icon(position: Vec3, object_id: int, color: Optional[Tuple[float, float, float]] = None)` | Register a clickable icon at *position* for the given GameObject. |
| `Gizmos.draw_wire_cube(center: Vec3, size: Vec3)` | Draw a wireframe axis-aligned box centered at *center* with *size*. |
| `Gizmos.draw_wire_sphere(center: Vec3, radius: float, segments: int = 24)` | Draw a wireframe sphere as three axis-aligned circles. |
| `Gizmos.draw_frustum(position: Vec3, fov_deg: float, aspect: float, near: float, far: float, forward: Vec3 = (0, 0, -1), up: Vec3 = (0, 1, 0), right: Vec3 = (1, 0, 0))` | Draw a camera frustum wireframe. |
| `Gizmos.draw_wire_arc(center: Vec3, normal: Vec3, radius: float, start_angle_deg: float = 0.0, arc_deg: float = 360.0, segments: int = 32)` | Draw a wireframe arc (or full circle) in a plane defined by *normal*. |

<!-- USER CONTENT START --> static_methods

<!-- USER CONTENT END -->

## 示例

```python
# TODO: Add example for Gizmos
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## 另请参阅

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
