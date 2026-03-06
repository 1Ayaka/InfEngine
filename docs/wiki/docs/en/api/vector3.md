# vector3

<div class="class-info">
class in <b>InfEngine.math</b>
</div>

## Description

Unity-compatible Vector3 type (lowercase, Pythonic).

Constructing ``vector3(x, y, z)`` returns a ``vec3f`` instance — they
are fully interchangeable at runtime.

Static properties (class-level, no parentheses)::

    vector3.zero                 # (0, 0, 0)
    vector3.one                  # (1, 1, 1)
    vector3.up                   # (0, 1, 0)
    vector3.down                 # (0, -1, 0)
    vector3.left                 # (-1, 0, 0)
    vector3.right                # (1, 0, 0)
    vector3.forward              # (0, 0, 1)
    vector3.back                 # (0, 0, -1)
    vector3.positive_infinity
    vector3.negative_infinity

Instance properties (no parentheses)::

    v.x, v.y, v.z               # component access (read/write)
    v.magnitude                  # length (read-only)
    v.sqr_magnitude              # squared length (read-only)
    v.normalized                 # unit-length copy (read-only)

Instance methods::

    v.set(x, y, z)              # set components in-place

Static methods::

    vector3.angle(a, b)
    vector3.clamp_magnitude(v, max_length)
    vector3.cross(a, b)
    vector3.distance(a, b)
    vector3.dot(a, b)
    vector3.lerp(a, b, t)
    vector3.lerp_unclamped(a, b, t)
    vector3.max(a, b)
    vector3.min(a, b)
    vector3.move_towards(current, target, max_delta)
    vector3.normalize(v)            # returns normalised copy
    vector3.ortho_normalize(v1, v2, v3)
    vector3.project(v, on_normal)
    vector3.project_on_plane(v, plane_normal)
    vector3.reflect(in_dir, normal)
    vector3.rotate_towards(current, target, max_radians, max_mag)
    vector3.scale(a, b)
    vector3.signed_angle(from_v, to_v)
    vector3.slerp(a, b, t)
    vector3.slerp_unclamped(a, b, t)
    vector3.smooth_damp(...)

Operators::

    v + w, v - w, v * s, v / s, -v, v == w, v != w

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## Example

```python
# TODO: Add example for vector3
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## See Also

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
