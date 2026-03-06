from InfEngine.engine import *
from InfEngine.math import *
from InfEngine.components import InfComponent, serialized_field, FieldType
from InfEngine.components import GameObjectRef, MaterialRef
from InfEngine.components import BuiltinComponent, CppProperty, Light, MeshRenderer, Camera
from InfEngine.components import AudioSource, AudioListener
from InfEngine.debug import Debug, debug, log, log_warning, log_error, log_exception
from InfEngine import core  # Phase 1: core module with Pythonic resource wrappers
from InfEngine import rendergraph  # Phase 2: Python-driven RenderGraph topology
from InfEngine import renderstack  # RenderStack: scene-level render configuration
from InfEngine import scene  # Phase 4: Tag & Layer query utilities
from InfEngine import input  # Unified input system (Unity-style API)
from InfEngine import ui  # Phase 0: Canvas/Text + layout foundation
from InfEngine.timing import Time          # Unity-style static Time class
from InfEngine.mathf import Mathf          # Unity-style math utilities
from InfEngine.coroutine import (          # Coroutine yield instructions
    Coroutine,
    WaitForSeconds,
    WaitForSecondsRealtime,
    WaitForEndOfFrame,
    WaitForFixedUpdate,
    WaitUntil,
    WaitWhile,
)