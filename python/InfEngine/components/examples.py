"""
Example Python Components for testing the InfComponent system.

These components demonstrate how to create custom game logic in Python.
"""

from InfEngine.components import InfComponent, serialized_field


class RotatorComponent(InfComponent):
    """
    Example component that rotates the GameObject continuously.
    Demonstrates serialized fields and update lifecycle.
    """
    
    # Serialized fields - visible and editable in Inspector
    rotation_speed: float = serialized_field(
        default=45.0, 
        range=(0, 360), 
        tooltip="Rotation speed in degrees per second"
    )
    
    axis_x: float = serialized_field(default=0.0, tooltip="X axis rotation weight")
    axis_y: float = serialized_field(default=1.0, tooltip="Y axis rotation weight")
    axis_z: float = serialized_field(default=0.0, tooltip="Z axis rotation weight")
    
    def start(self):
        """Called before first update."""
        print(f"[RotatorComponent] Started on {self.game_object.name} with speed {self.rotation_speed}")
    
    def update(self, delta_time: float):
        """Rotate the object each frame."""
        if self.transform is None:
            return
        
        # Get current rotation
        euler = self.transform.euler_angles
        
        # Apply rotation
        delta = self.rotation_speed * delta_time
        from InfEngine.lib import vec3f
        self.transform.euler_angles = vec3f(
            euler.x + delta * self.axis_x,
            euler.y + delta * self.axis_y,
            euler.z + delta * self.axis_z,
        )


class OscillatorComponent(InfComponent):
    """
    Example component that oscillates the GameObject's position.
    """
    
    amplitude: float = serialized_field(default=1.0, range=(0, 10), tooltip="Oscillation amplitude")
    frequency: float = serialized_field(default=1.0, range=(0, 10), tooltip="Oscillation frequency")
    use_x: bool = serialized_field(default=False)
    use_y: bool = serialized_field(default=True)
    use_z: bool = serialized_field(default=False)
    
    def start(self):
        """Store initial position."""
        self._time = 0.0
        pos = self.transform.position
        self._initial_pos = (pos.x, pos.y, pos.z)
        print(f"[OscillatorComponent] Started on {self.game_object.name}")
    
    def update(self, delta_time: float):
        """Oscillate position each frame."""
        import math
        
        self._time += delta_time
        offset = math.sin(self._time * self.frequency * 2 * math.pi) * self.amplitude
        
        px, py, pz = self._initial_pos
        
        if self.use_x:
            px += offset
        if self.use_y:
            py += offset
        if self.use_z:
            pz += offset
        
        from InfEngine.lib import vec3f
        self.transform.position = vec3f(px, py, pz)


class DebugInfoComponent(InfComponent):
    """
    Example component that prints debug info.
    Shows how to use different field types.
    """
    
    message: str = serialized_field(default="Hello InfEngine!", tooltip="Debug message to print")
    print_interval: float = serialized_field(default=1.0, range=(0.1, 10), tooltip="Print interval in seconds")
    verbose: bool = serialized_field(default=False, tooltip="Enable verbose output")
    
    def start(self):
        self._timer = 0.0
        print(f"[DebugInfoComponent] Initialized: {self.message}")
    
    def update(self, delta_time: float):
        self._timer += delta_time
        if self._timer >= self.print_interval:
            self._timer = 0.0
            if self.verbose:
                pos = self.transform.position
                rot = self.transform.euler_angles
                print(f"[Debug] {self.game_object.name}: pos=({pos.x}, {pos.y}, {pos.z}), rot=({rot.x}, {rot.y}, {rot.z})")
            else:
                print(f"[Debug] {self.message}")


# Export for easy import
__all__ = [
    "RotatorComponent",
    "OscillatorComponent", 
    "DebugInfoComponent",
]
