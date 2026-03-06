# GameObject

<div class="class-info">
类位于 <b>InfEngine</b>
</div>

## 描述

A game object in the scene hierarchy with components.

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## 属性

| 名称 | 类型 | 描述 |
|------|------|------|
| name | `str` |  |
| active | `bool` |  |
| id | `int` | Unique object ID. *(只读)* |
| transform | `Transform` | Get the Transform component. *(只读)* |
| active_self | `bool` | Is this object itself active? Alias for active. *(只读)* |
| active_in_hierarchy | `bool` | Is this object active in the hierarchy? Unity: gameObject.activeInHierarchy *(只读)* |
| is_static | `bool` | Static flag. |
| scene | `Optional[Scene]` | The Scene this GameObject belongs to. *(只读)* |
| tag | `str` | Tag string for this GameObject. |
| layer | `int` | Layer index (0-31) for this GameObject. |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## 公共方法

| 方法 | 描述 |
|------|------|
| `get_transform() → Transform` | Get the Transform component. |
| `add_component(component_type: Union[str, type]) → Optional[Component]` | Add a C++ component by type or type name. |
| `remove_component(component: Component) → bool` | Remove a component instance (cannot remove Transform). |
| `get_components() → List[Component]` | Get all components (including Transform). |
| `get_cpp_component(type_name: str) → Optional[Component]` | Get a C++ component by type name (e.g., 'Transform', 'MeshRenderer', 'Light'). |
| `get_cpp_components(type_name: str) → List[Component]` | Get all C++ components of a given type name. |
| `add_py_component(component_instance: Any) → Optional[Any]` | Add a Python InfComponent instance to this GameObject. |
| `get_py_component(component_type: type) → Optional[Any]` | Get a Python component of the specified type. |
| `get_py_components() → List[Any]` | Get all Python components attached to this GameObject. |
| `remove_py_component(component: Any) → bool` | Remove a Python component instance. |
| `get_parent() → Optional[GameObject]` | Get the parent GameObject. |
| `set_parent(parent: Optional[GameObject], world_position_stays: bool = True) → None` | Set the parent GameObject (None for root). |
| `get_children() → List[GameObject]` | Get list of child GameObjects. |
| `get_child_count() → int` | Get the number of children. |
| `is_active_in_hierarchy() → bool` | Check if this object and all parents are active. |
| `get_child(index: int) → Optional[GameObject]` | Get child by index. |
| `find_child(name: str) → Optional[GameObject]` | Find direct child by name (non-recursive). |
| `find_descendant(name: str) → Optional[GameObject]` | Find descendant by name (recursive depth-first search). |
| `compare_tag(tag: str) → bool` | Returns True if this GameObject's tag matches the given tag. |
| `serialize() → str` | Serialize GameObject to JSON string. |
| `deserialize(json_str: str) → None` | Deserialize GameObject from JSON string. |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## 示例

```python
# TODO: Add example for GameObject
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## 另请参阅

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
