"""
FullScreenEffect — Multi-pass fullscreen post-processing effect base class.

A FullScreenEffect is a higher-level abstraction above ``RenderPass`` that
represents a **complete, parameterised, multi-pass fullscreen effect** such
as Bloom, Vignette, or SSAO.

Subclass hierarchy::

    RenderPass
    └── FullScreenEffect          (this module)
        ├── BloomEffect           (built-in)
        └── ...user-defined...

Subclass contract:
    1. Define ``name``, ``injection_point``, ``default_order``
    2. Declare tuneable parameters via ``serialized_field``
    3. Implement ``setup_passes(graph, bus)`` — inject all passes into the graph
    4. Optionally implement ``get_shader_list()`` for validation / precompilation

Integration with RenderStack:
    FullScreenEffect inherits RenderPass, so it is transparently discovered,
    mounted, validated, and serialised by the existing RenderStack machinery.
    ``inject()`` delegates to ``setup_passes()`` — subclasses override
    ``setup_passes`` instead of ``inject``.

Parameter serialization:
    Uses the same ``serialized_field`` / ``__init_subclass__`` mechanism as
    ``RenderPipeline``.  Parameters are persisted in the scene JSON via
    ``RenderStack.on_before_serialize()`` and restored by
    ``on_after_deserialize()``.
"""

from __future__ import annotations

from typing import Any, ClassVar, Dict, List, Set, TYPE_CHECKING

from InfEngine.renderstack.render_pass import RenderPass

if TYPE_CHECKING:
    from InfEngine.rendergraph.graph import RenderGraph
    from InfEngine.renderstack.resource_bus import ResourceBus

# Reserved attribute names that should never be treated as serialized fields.
_RESERVED_ATTRS = frozenset({
    "name",
    "injection_point",
    "default_order",
    "menu_path",
    "requires",
    "modifies",
    "creates",
    "enabled",
})


class FullScreenEffect(RenderPass):
    """多 pass 全屏后处理效果基类。

    子类必须定义:
        - ``name``: 全局唯一的效果名称
        - ``injection_point``: 目标注入点（如 ``"before_post_process"``）
        - ``default_order``: 同注入点内排序值

    子类可选定义:
        - ``menu_path``: 编辑器菜单中的分类路径（如 ``"Post-processing/Bloom"``）

    子类通过 ``serialized_field`` 声明可调参数::

        class BloomEffect(FullScreenEffect):
            menu_path = "Post-processing/Bloom"
            threshold: float = serialized_field(default=1.0, range=(0, 10))
            intensity: float = serialized_field(default=0.5, range=(0, 3))

    子类实现 ``setup_passes(graph, bus)`` 向 graph 注入所有渲染 pass。
    """

    # ---- 默认资源声明（大多数全屏效果读 + 改 color） ----
    requires: ClassVar[Set[str]] = {"color"}
    modifies: ClassVar[Set[str]] = {"color"}

    # ---- 编辑器分类路径（可选） ----
    menu_path: ClassVar[str] = ""

    # ---- 类级序列化字段元数据 ----
    _serialized_fields_: ClassVar[Dict[str, Any]] = {}

    # ------------------------------------------------------------------
    # __init_subclass__: 自动收集 serialized_field
    # ------------------------------------------------------------------

    def __init_subclass__(cls, **kwargs):
        """Collect class-level serialized fields (same mechanism as RenderPipeline)."""
        super().__init_subclass__(**kwargs)

        cls._serialized_fields_ = {}

        for attr_name in list(cls.__dict__):
            if attr_name.startswith("_"):
                continue
            if attr_name in _RESERVED_ATTRS:
                continue

            attr = cls.__dict__[attr_name]

            # Skip methods, properties, classmethods, staticmethods
            if callable(attr) and not isinstance(attr, (int, float, bool, str)):
                continue
            if isinstance(attr, (property, classmethod, staticmethod)):
                continue
            if attr is None:
                continue

            from InfEngine.components.serialized_field import (
                FieldMetadata,
                HiddenField,
                SerializedFieldDescriptor,
                infer_field_type_from_value,
            )

            if isinstance(attr, HiddenField):
                continue

            if isinstance(attr, SerializedFieldDescriptor):
                cls._serialized_fields_[attr_name] = attr.metadata
            elif isinstance(attr, FieldMetadata):
                cls._serialized_fields_[attr_name] = attr
            else:
                from enum import Enum as _Enum

                field_type = infer_field_type_from_value(attr)
                enum_type = type(attr) if isinstance(attr, _Enum) else None
                metadata = FieldMetadata(
                    name=attr_name,
                    field_type=field_type,
                    default=attr,
                    enum_type=enum_type,
                )
                descriptor = SerializedFieldDescriptor(metadata)
                descriptor.__set_name__(cls, attr_name)
                setattr(cls, attr_name, descriptor)
                cls._serialized_fields_[attr_name] = metadata

    # ------------------------------------------------------------------
    # Instance init
    # ------------------------------------------------------------------

    def __init__(self, enabled: bool = True) -> None:
        super().__init__(enabled=enabled)
        # Prime instance storage for serialized fields
        from InfEngine.components.serialized_field import get_serialized_fields
        for field_name, meta in get_serialized_fields(self.__class__).items():
            if not hasattr(self, f"_sf_{field_name}"):
                try:
                    setattr(self, field_name, meta.default)
                except Exception:
                    pass

    # ==================================================================
    # Core interface — subclasses implement these
    # ==================================================================

    def setup_passes(self, graph: "RenderGraph", bus: "ResourceBus") -> None:
        """向 RenderGraph 注入本效果的所有渲染 pass。

        子类实现此方法:
        1. 从 bus 获取输入资源（如 ``bus.get("color")``）
        2. 创建中间纹理（如 ``graph.create_texture(...)``）
        3. 按顺序添加 pass，指定 shader 和操作
        4. 将修改后的资源写回 bus

        Args:
            graph: 当前构建中的 RenderGraph。
            bus: 资源总线。
        """
        raise NotImplementedError(
            f"{type(self).__name__} must implement setup_passes()"
        )

    def get_shader_list(self) -> List[str]:
        """返回本效果使用的所有 shader id 列表。

        用于:
        - Editor 预验证 shader 是否存在
        - 未来的 shader 预编译 / 缓存

        Returns:
            shader id 列表，如 ``["bloom_prefilter", "bloom_downsample", ...]``
        """
        return []

    # ==================================================================
    # inject() — bridge to RenderStack
    # ==================================================================

    def inject(self, graph: "RenderGraph", bus: "ResourceBus") -> None:
        """由 RenderStack 调用。委托到 ``setup_passes()``。

        子类不应重写此方法——重写 ``setup_passes()`` 即可。
        """
        if not self.enabled:
            return
        self.setup_passes(graph, bus)

    # ==================================================================
    # Serialization helpers
    # ==================================================================

    def get_params_dict(self) -> Dict[str, Any]:
        """导出当前参数为可 JSON 序列化的字典。"""
        from InfEngine.components.serialized_field import get_serialized_fields
        from enum import Enum

        params: Dict[str, Any] = {}
        for field_name in get_serialized_fields(self.__class__):
            value = getattr(self, field_name, None)
            if isinstance(value, Enum):
                params[field_name] = {"__enum_name__": value.name}
            else:
                params[field_name] = value
        return params

    def set_params_dict(self, params: Dict[str, Any]) -> None:
        """从字典恢复参数。"""
        from InfEngine.components.serialized_field import get_serialized_fields, FieldType

        fields = get_serialized_fields(self.__class__)
        for field_name, value in params.items():
            meta = fields.get(field_name)
            if meta is None:
                continue
            try:
                if (meta.field_type == FieldType.ENUM
                        and isinstance(value, dict)
                        and "__enum_name__" in value):
                    enum_cls = meta.enum_type
                    enum_name = value["__enum_name__"]
                    if enum_cls is not None and enum_name in enum_cls.__members__:
                        setattr(self, field_name, enum_cls[enum_name])
                        continue
                setattr(self, field_name, value)
            except Exception:
                continue

    # ------------------------------------------------------------------
    # Repr
    # ------------------------------------------------------------------

    def __repr__(self) -> str:
        return (
            f"<{type(self).__name__} name='{self.name}' "
            f"point='{self.injection_point}' "
            f"enabled={self.enabled}>"
        )
