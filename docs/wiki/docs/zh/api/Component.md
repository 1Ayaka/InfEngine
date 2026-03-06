# Component

<div class="class-info">
类位于 <b>InfEngine</b>
</div>

## 描述

Base class for all components attached to a GameObject.

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## 属性

| 名称 | 类型 | 描述 |
|------|------|------|
| type_name | `str` | Component type name (e.g. *(只读)* |
| component_id | `int` | Unique component ID. *(只读)* |
| enabled | `bool` |  |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## 公共方法

| 方法 | 描述 |
|------|------|
| `serialize() → str` | Serialize component to JSON string. |
| `deserialize(json_str: str) → None` | Deserialize component from JSON string. |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## 示例

```python
# TODO: Add example for Component
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## 另请参阅

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
