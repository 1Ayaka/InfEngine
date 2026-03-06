# Transform

<div class="class-info">
类位于 <b>InfEngine</b>
</div>

**继承自:** [Component](Component.md)

## 描述

Transform component — position, rotation, scale.

Follows Unity convention:
  - position / euler_angles → world space
  - local_position / local_euler_angles / local_scale → local (parent) space

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## 属性

| 名称 | 类型 | 描述 |
|------|------|------|
| position | `vec3f` | Position in world space (considering parent hierarchy). |
| euler_angles | `vec3f` | Rotation as Euler angles (degrees) in world space. |
| local_position | `vec3f` | Position in local (parent) space. |
| local_euler_angles | `vec3f` | Rotation as Euler angles (degrees) in local space. |
| local_scale | `vec3f` | Scale in local space. |
| lossy_scale | `vec3f` | Approximate world-space scale (read-only, like Unity lossyScale). *(只读)* |
| forward | `vec3f` | Forward direction in world space (negative Z). *(只读)* |
| right | `vec3f` | Right direction in world space (positive X). *(只读)* |
| up | `vec3f` | Up direction in world space (positive Y). *(只读)* |
| local_forward | `vec3f` | Forward direction in local space (negative Z). *(只读)* |
| local_right | `vec3f` | Right direction in local space (positive X). *(只读)* |
| local_up | `vec3f` | Up direction in local space (positive Y). *(只读)* |
| rotation | `Tuple[float, float, float, float]` | World-space rotation as quaternion (x, y, z, w). |
| local_rotation | `Tuple[float, float, float, float]` | Local-space rotation as quaternion (x, y, z, w). |
| parent | `Optional[Transform]` | Parent Transform (None if root). |
| root | `Transform` | Topmost Transform in the hierarchy. *(只读)* |
| child_count | `int` | Number of children. *(只读)* |
| has_changed | `bool` | Has the transform changed since last reset? Unity: transform.hasChanged |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## 公共方法

| 方法 | 描述 |
|------|------|
| `set_parent(parent: Optional[Transform], world_position_stays: bool = True) → None` | Set parent Transform. |
| `get_child(index: int) → Optional[Transform]` | Get child Transform by index. |
| `find(name: str) → Optional[Transform]` | Find child Transform by name (non-recursive). |
| `detach_children() → None` | Unparent all children. |
| `is_child_of(parent: Transform) → bool` | Is this transform a child of parent? Unity: transform.IsChildOf(parent) |
| `get_sibling_index() → int` | Get sibling index. |
| `set_sibling_index(index: int) → None` | Set sibling index. |
| `set_as_first_sibling() → None` | Move to first sibling. |
| `set_as_last_sibling() → None` | Move to last sibling. |
| `transform_point(point: vec3f) → vec3f` | Transform point from local to world space. |
| `inverse_transform_point(point: vec3f) → vec3f` | Transform point from world to local space. |
| `transform_direction(direction: vec3f) → vec3f` | Transform direction from local to world (rotation only). |
| `inverse_transform_direction(direction: vec3f) → vec3f` | Transform direction from world to local (rotation only). |
| `transform_vector(vector: vec3f) → vec3f` | Transform vector from local to world (with scale). |
| `inverse_transform_vector(vector: vec3f) → vec3f` | Transform vector from world to local (with scale). |
| `local_to_world_matrix() → List[float]` | Get local-to-world transformation matrix (16 floats, column-major). |
| `world_to_local_matrix() → List[float]` | Get world-to-local transformation matrix (16 floats, column-major). |
| `look_at(target: vec3f) → None` | Rotate to face a world-space target position. |
| `translate(delta: vec3f) → None` | Translate in world space. |
| `translate_local(delta: vec3f) → None` | Translate in local space (relative to own axes). |
| `rotate(euler: vec3f) → None` | Rotate by Euler angles (degrees) in local space. |
| `rotate_around(point: vec3f, axis: vec3f, angle: float) → None` | Rotate around a world-space point. |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## 示例

```python
# TODO: Add example for Transform
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## 另请参阅

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
