# InfComponent

<div class="class-info">
类位于 <b>InfEngine.components</b>
</div>

## 描述

Base class for Python-scripted game components (Unity-style lifecycle).

Subclass this to create game logic scripts. Use ``serialized_field()``
class variables for Inspector-editable properties.

Example::

    class PlayerController(InfComponent):
        speed: float = serialized_field(default=5.0, range=(0, 100))

        def start(self):
            Debug.log("PlayerController started")

        def update(self, delta_time: float):
            pos = self.transform.position
            self.transform.position = vec3f(
                pos.x + self.speed * delta_time, pos.y, pos.z
            )

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## 构造函数

| 签名 | 描述 |
|------|------|
| `InfComponent.__init__() → None` |  |

<!-- USER CONTENT START --> constructors

<!-- USER CONTENT END -->

## 属性

| 名称 | 类型 | 描述 |
|------|------|------|
| game_object | `Optional[GameObject]` | The GameObject this component is attached to. *(只读)* |
| transform | `Optional[Transform]` | Shortcut to ``self.game_object.transform``. *(只读)* |
| is_valid | `bool` | Whether the underlying GameObject reference is still alive. *(只读)* |
| enabled | `bool` | Whether the component is enabled. |
| type_name | `str` | Class name of this component. *(只读)* |
| component_id | `int` | Unique auto-incremented ID for this component instance. *(只读)* |
| tag | `str` | Tag of the attached GameObject. |
| game_object_layer | `int` | Layer index (0-31) of the attached GameObject. |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## 公共方法

| 方法 | 描述 |
|------|------|
| `get_component(component_type: Union[type, str]) → Optional[Any]` | Get another component of the specified type on the same GameObject. |
| `get_components(component_type: Union[type, str]) → List[Any]` | Get all components of the specified type on the same GameObject. |
| `try_get_component(component_type: Union[type, str]) → Tuple[bool, Optional[Any]]` | Try to get a component; returns (found, component_or_None). |
| `get_mesh_renderer() → Optional[MeshRenderer]` | Shortcut to get the MeshRenderer on the same GameObject. |
| `compare_tag(tag: str) → bool` | Returns True if the attached GameObject's tag matches. |
| `get_component_in_children(component_type: Union[type, str], include_inactive: bool = ...) → Optional[Any]` | Get a component of the specified type on this or any child GameObject. |
| `get_component_in_parent(component_type: Union[type, str], include_inactive: bool = ...) → Optional[Any]` | Get a component of the specified type on this or any parent GameObject. |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## 生命周期方法

| 方法 | 描述 |
|------|------|
| `awake() → None` | Called once when the component is first created. |
| `start() → None` | Called before the first Update after the component is enabled. |
| `update(delta_time: float) → None` | Called every frame. |
| `fixed_update(fixed_delta_time: float) → None` | Called at a fixed time step (default 50 Hz). |
| `late_update(delta_time: float) → None` | Called every frame after all Update calls. |
| `on_destroy() → None` | Called when the component or its GameObject is destroyed. |
| `on_enable() → None` | Called when the component is enabled. |
| `on_disable() → None` | Called when the component is disabled. |
| `on_validate() → None` | Called when a serialized field is changed in the Inspector. |
| `reset() → None` | Called when the component is reset to defaults. |
| `on_after_deserialize() → None` | Called after deserialization (scene load / undo). |
| `on_before_serialize() → None` | Called before serialization (scene save). |

<!-- USER CONTENT START --> lifecycle_methods

<!-- USER CONTENT END -->

## 运算符

| 方法 | 返回值 |
|------|------|
| `__repr__() → str` | `str` |

<!-- USER CONTENT START --> operators

<!-- USER CONTENT END -->

## 示例

```python
# TODO: Add example for InfComponent
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## 另请参阅

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
