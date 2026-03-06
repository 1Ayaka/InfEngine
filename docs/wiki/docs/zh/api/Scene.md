# Scene

<div class="class-info">
类位于 <b>InfEngine</b>
</div>

## 描述

A scene containing GameObjects.

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## 属性

| 名称 | 类型 | 描述 |
|------|------|------|
| name | `str` |  |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## 公共方法

| 方法 | 描述 |
|------|------|
| `create_game_object(name: str = 'GameObject') → GameObject` | Create a new empty GameObject in this scene. |
| `create_primitive(type: PrimitiveType, name: str = '') → GameObject` | Create a primitive GameObject (Cube, Sphere, Capsule, Cylinder, Plane). |
| `get_root_objects() → List[GameObject]` | Get all root-level GameObjects. |
| `get_all_objects() → List[GameObject]` | Get all GameObjects in the scene. |
| `find(name: str) → Optional[GameObject]` | Find a GameObject by name. |
| `find_by_id(id: int) → Optional[GameObject]` | Find a GameObject by ID. |
| `find_with_tag(tag: str) → Optional[GameObject]` | Find the first GameObject with a given tag. |
| `find_game_objects_with_tag(tag: str) → List[GameObject]` | Find all GameObjects with a given tag. |
| `find_game_objects_in_layer(layer: int) → List[GameObject]` | Find all GameObjects in a given layer. |
| `destroy_game_object(game_object: GameObject) → None` | Destroy a GameObject (removed at end of frame). |
| `process_pending_destroys() → None` | Process pending GameObject destroys. |
| `is_playing() → bool` | Check if the scene is in play mode. |
| `serialize() → str` | Serialize scene to JSON string. |
| `deserialize(json_str: str) → None` | Deserialize scene from JSON string. |
| `save_to_file(path: str) → None` | Save scene to a JSON file. |
| `load_from_file(path: str) → None` | Load scene from a JSON file. |
| `has_pending_py_components() → bool` | Check if there are pending Python components to restore. |
| `take_pending_py_components() → List[PendingPyComponent]` | Get and clear pending Python components for restoration. |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## 生命周期方法

| 方法 | 描述 |
|------|------|
| `start() → None` | Trigger Awake+Start on all components (idempotent — skipped if already started). |

<!-- USER CONTENT START --> lifecycle_methods

<!-- USER CONTENT END -->

## 示例

```python
# TODO: Add example for Scene
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## 另请参阅

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
