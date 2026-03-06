# Debug

<div class="class-info">
类位于 <b>InfEngine.debug</b>
</div>

## 描述

Unity-style static logging API.

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## 静态方法

| 方法 | 描述 |
|------|------|
| `static Debug.log(message: Any, context: Any = None) → None` | Log a message to the debug console. |
| `static Debug.log_warning(message: Any, context: Any = None) → None` | Log a warning to the debug console. |
| `static Debug.log_error(message: Any, context: Any = None, source_file: str = '', source_line: int = 0) → None` | Log an error to the debug console. |
| `static Debug.log_exception(exception: Exception, context: Any = None) → None` | Log an exception with stack trace. |
| `static Debug.log_assert(condition: bool, message: Any = 'Assertion failed', context: Any = None) → None` | Assert a condition, log if False. |
| `static Debug.clear_console() → None` | Clear all log entries. |
| `static Debug.log_internal(message: Any, context: Any = None) → None` | Log an internal engine message (hidden from user Console). |

<!-- USER CONTENT START --> static_methods

<!-- USER CONTENT END -->

## 示例

```python
# TODO: Add example for Debug
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## 另请参阅

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
