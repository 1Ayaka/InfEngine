"""Type stubs for InfEngine.debug — Unity-style Debug.log system."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from enum import Enum
from typing import Any, Callable, List, Optional


class LogType(Enum):
    """Log message severity."""

    LOG = ...
    WARNING = ...
    ERROR = ...
    ASSERT = ...
    EXCEPTION = ...


@dataclass
class LogEntry:
    """A single log entry in the debug console."""

    message: str
    log_type: LogType
    timestamp: datetime
    stack_trace: str = ""
    context: Any = None
    internal: bool = False
    source_file: str = ""
    source_line: int = 0

    def get_formatted_time(self) -> str: ...
    def get_icon(self) -> str: ...


class DebugConsole:
    """Singleton console that stores log entries and notifies listeners."""

    @classmethod
    def get_instance(cls) -> DebugConsole: ...
    def add_listener(self, callback: Callable[[LogEntry], None]) -> None: ...
    def remove_listener(self, callback: Callable[[LogEntry], None]) -> None: ...
    def log(self, entry: LogEntry) -> None: ...
    def get_entries(self) -> List[LogEntry]: ...
    def get_filtered_entries(
        self,
        show_logs: bool = True,
        show_warnings: bool = True,
        show_errors: bool = True,
    ) -> List[LogEntry]: ...
    def clear(self) -> None: ...
    @property
    def log_count(self) -> int: ...
    @property
    def warning_count(self) -> int: ...
    @property
    def error_count(self) -> int: ...


class Debug:
    """Unity-style static logging API."""

    @staticmethod
    def log(message: Any, context: Any = None) -> None:
        """Log a message to the debug console."""
        ...
    @staticmethod
    def log_warning(message: Any, context: Any = None) -> None:
        """Log a warning to the debug console."""
        ...
    @staticmethod
    def log_error(
        message: Any,
        context: Any = None,
        *,
        source_file: str = "",
        source_line: int = 0,
    ) -> None:
        """Log an error to the debug console."""
        ...
    @staticmethod
    def log_exception(exception: Exception, context: Any = None) -> None:
        """Log an exception with stack trace."""
        ...
    @staticmethod
    def log_assert(
        condition: bool, message: Any = "Assertion failed", context: Any = None
    ) -> None:
        """Assert a condition, log if False."""
        ...
    @staticmethod
    def clear_console() -> None:
        """Clear all log entries."""
        ...
    @staticmethod
    def log_internal(message: Any, context: Any = None) -> None:
        """Log an internal engine message (hidden from user Console)."""
        ...


# Module-level aliases
def log(message: Any, context: Any = None) -> None: ...
def log_warning(message: Any, context: Any = None) -> None: ...
def log_error(message: Any, context: Any = None) -> None: ...
def log_exception(exception: Exception, context: Any = None) -> None: ...
