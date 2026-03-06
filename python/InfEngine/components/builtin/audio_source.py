"""
AudioSource — Python BuiltinComponent wrapper for C++ AudioSource.

InfEngine's AudioSource supports **multi-track** playback: a single
AudioSource can hold N tracks, each with its own AudioClip and per-track
volume.  All tracks share the source-level settings (volume, mute, pitch,
3D spatial attenuation).

Example::

    from InfEngine.components.builtin import AudioSource, AudioListener
    from InfEngine.core.audio_clip import AudioClip

    class MusicPlayer(InfComponent):
        def start(self):
            source = self.get_component(AudioSource)
            source.track_count = 2

            bgm = AudioClip.load("Assets/Audio/bgm.wav")
            sfx = AudioClip.load("Assets/Audio/sfx.wav")

            source.set_track_clip(0, bgm)
            source.set_track_clip(1, sfx)
            source.set_track_volume(1, 0.5)

            source.play(0)  # play BGM
            source.play(1)  # play SFX simultaneously
"""

from __future__ import annotations

from typing import Optional

from InfEngine.components.builtin_component import BuiltinComponent, CppProperty
from InfEngine.components.serialized_field import FieldType


class AudioSource(BuiltinComponent):
    """Python wrapper for the C++ AudioSource component.

    Properties delegate to the C++ ``AudioSource`` via CppProperty.
    Playback methods delegate directly to the C++ component.

    Multi-track:
        - ``track_count`` — number of tracks (default 1)
        - ``set_track_clip(i, clip)`` / ``get_track_clip(i)``
        - ``set_track_volume(i, vol)`` / ``get_track_volume(i)``
        - ``play(i)`` / ``stop(i)`` / ``pause(i)`` / ``un_pause(i)``
        - ``stop_all()`` — stop all tracks
    """

    _cpp_type_name = "AudioSource"
    _component_category_ = "Audio"

    # ---- Track Count ----
    track_count = CppProperty(
        "track_count",
        FieldType.INT,
        default=1,
        range=(1, 16),
        tooltip="Number of audio tracks (each can play a different clip)",
    )

    # ---- Volume / Pitch / Mute (source-level) ----
    volume = CppProperty(
        "volume",
        FieldType.FLOAT,
        default=1.0,
        range=(0.0, 1.0),
        tooltip="Source-level volume. Multiplied with per-track volume.",
    )
    pitch = CppProperty(
        "pitch",
        FieldType.FLOAT,
        default=1.0,
        range=(0.1, 3.0),
        tooltip="Pitch multiplier (1.0 = normal speed)",
    )
    mute = CppProperty(
        "mute",
        FieldType.BOOL,
        default=False,
        tooltip="If enabled, all tracks are muted",
    )

    # ---- Loop / PlayOnAwake ----
    loop = CppProperty(
        "loop",
        FieldType.BOOL,
        default=False,
        tooltip="Whether to loop playback (all tracks)",
    )
    play_on_awake = CppProperty(
        "play_on_awake",
        FieldType.BOOL,
        default=True,
        tooltip="Automatically play track 0 when the component starts",
    )

    # ---- 3D Spatial ----
    min_distance = CppProperty(
        "min_distance",
        FieldType.FLOAT,
        default=1.0,
        range=(0.0, 500.0),
        header="3D Spatial",
        tooltip="Distance at which volume starts to attenuate",
    )
    max_distance = CppProperty(
        "max_distance",
        FieldType.FLOAT,
        default=500.0,
        range=(0.0, 10000.0),
        tooltip="Distance at which volume reaches minimum",
    )

    # ---- Wwise hook ----
    output_bus = CppProperty(
        "output_bus",
        FieldType.STRING,
        default="Master",
        tooltip="Output audio bus name (for future Wwise routing)",
    )

    # ------------------------------------------------------------------
    # Track management (delegate methods)
    # ------------------------------------------------------------------

    def set_track_clip(self, track_index: int, clip) -> None:
        """Assign an AudioClip to a specific track.

        Args:
            track_index: 0-based track index.
            clip: An ``AudioClip`` (Python wrapper) or C++ native clip.
        """
        cpp = self._cpp_component
        if cpp is None:
            return
        # Accept both Python AudioClip wrapper and raw C++ clip
        native_clip = getattr(clip, "native", clip) if clip is not None else None
        cpp.set_track_clip(track_index, native_clip)

    def get_track_clip(self, track_index: int):
        """Get the AudioClip on a specific track."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.get_track_clip(track_index)
        return None

    def set_track_volume(self, track_index: int, volume: float) -> None:
        """Set per-track volume (0.0–1.0)."""
        cpp = self._cpp_component
        if cpp is not None:
            cpp.set_track_volume(track_index, volume)

    def get_track_volume(self, track_index: int) -> float:
        """Get per-track volume."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.get_track_volume(track_index)
        return 0.0

    # ------------------------------------------------------------------
    # Playback control (delegate methods)
    # ------------------------------------------------------------------

    def play(self, track_index: int = 0) -> None:
        """Start playing a track (default: track 0)."""
        cpp = self._cpp_component
        if cpp is not None:
            cpp.play(track_index)

    def stop(self, track_index: int = 0) -> None:
        """Stop a track (default: track 0)."""
        cpp = self._cpp_component
        if cpp is not None:
            cpp.stop(track_index)

    def pause(self, track_index: int = 0) -> None:
        """Pause a track (default: track 0)."""
        cpp = self._cpp_component
        if cpp is not None:
            cpp.pause(track_index)

    def un_pause(self, track_index: int = 0) -> None:
        """Resume a paused track (default: track 0)."""
        cpp = self._cpp_component
        if cpp is not None:
            cpp.un_pause(track_index)

    def stop_all(self) -> None:
        """Stop all tracks."""
        cpp = self._cpp_component
        if cpp is not None:
            cpp.stop_all()

    def is_track_playing(self, track_index: int) -> bool:
        """Whether a specific track is currently playing."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.is_track_playing(track_index)
        return False

    def is_track_paused(self, track_index: int) -> bool:
        """Whether a specific track is paused."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.is_track_paused(track_index)
        return False

    @property
    def is_playing(self) -> bool:
        """Whether track 0 is currently playing."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.is_playing
        return False

    @property
    def is_paused(self) -> bool:
        """Whether track 0 is paused."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.is_paused
        return False

    # ------------------------------------------------------------------
    # Read-only properties
    # ------------------------------------------------------------------

    @property
    def game_object_id(self) -> int:
        """Owning GameObject ID (for Wwise gameObjectId mapping)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.game_object_id
        return 0

    # ------------------------------------------------------------------
    # Serialization
    # ------------------------------------------------------------------

    def serialize(self) -> str:
        """Serialize AudioSource to JSON string (delegates to C++)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.serialize()
        return "{}"

    def deserialize(self, json_str: str) -> bool:
        """Deserialize AudioSource from JSON string (delegates to C++)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.deserialize(json_str)
        return False
