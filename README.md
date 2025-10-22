# Wayfire Vertical Kube Plugin

A Wayfire plugin that displays workspace rows as vertical cubes, allowing navigation between workspace rows in a 3D cube interface.

## Installation

Set `--prefix` to match your Wayfire installation path.
```bash
# For system-wide Wayfire installation
meson setup build --prefix=/usr

# Or for custom Wayfire installation
meson setup build --prefix=/opt/wayfire

# Build the plugin
meson compile -C build

# Install (requires sudo)
sudo meson install -C build
```

## Configuration

1. Edit your `~/.config/wayfire.ini`
2. Add `vertical_kube` to the plugins list in the `[core]` section:
```ini
[core]
plugins = ... vertical_kube
```

**Important:** Remove the default `cube` plugin if present to avoid conflicts.

3. Configure options in the `[vertical_kube]` section:
```ini
[vertical_kube]
# Bindings
activate = <alt> <ctrl> BTN_LEFT
rotate_left = <ctrl> <alt> KEY_LEFT
rotate_right = <ctrl> <alt> KEY_RIGHT
rotate_up = <ctrl> <alt> KEY_UP
rotate_down = <ctrl> <alt> KEY_DOWN

# Effects
deform = 1                    # 0=None, 1=Cylinder, 2=Star
light = true                  # Enable lighting effects

# Window Popout
enable_window_popout = true   # Show windows on larger cube above main cube
popout_scale = 1.20           # Scale factor (1.0-2.0)
popout_opacity = 1.0          # Opacity (0.0-1.0)

# Cube Caps
enable_caps = true            # Show top/bottom caps
cap_alpha = 0.8               # Cap transparency (0.0-1.0)
cap_color_top = 0.8 0.8 0.8 1.0
cap_color_bottom = 0.8 0.8 0.8 1.0
cap_texture_top =             # Optional texture file path
cap_texture_bottom =          # Optional texture file path

# Zoom and Animation
zoom = 0.1                    # Zoom out level (0.0 = no zoom)
initial_animation = 350ms     # Animation duration
speed_zoom = 0.07             # Zoom speed factor
speed_spin_horiz = 0.02       # Horizontal rotation speed
speed_spin_vert = 0.02        # Vertical rotation speed

# Background
background_mode = simple      # simple, skydome, or cubemap
background = 0.1 0.1 0.1 1.0  # Background color (simple mode)

# Skydome options
skydome_mirror = true
skydome_texture =             # Texture file path

# Cubemap options
cubemap_image =               # Image file path
```

4. Restart Wayfire

## Usage

- **Activate cube**: Hold `Alt+Ctrl` and click left mouse button, then drag to rotate
- **Rotate left/right**: `Ctrl+Alt+Left/Right` arrow keys
- **Move between workspace rows**: `Ctrl+Alt+Up/Down` arrow keys
- **Zoom**: Mouse wheel while cube is active

## Features

### Deformation Modes
- **None** (0): Standard cube
- **Cylinder** (1): Cylindrical deformation
- **Star** (2): Star-shaped deformation

### Window Popout Effect
Creates a larger cube showing only windows without background, floating above the main cube for better visibility.

### Cube Caps
Top and bottom polygonal caps that match the number of workspace faces, with customizable colors and optional textures.

### Background Modes
- **Simple**: Solid color background
- **Skydome**: Hemisphere texture background with optional mirroring
- **Cubemap**: Full 360° environment map

## Uninstallation
```bash
sudo ninja -C build uninstall
```

Or manually remove:
```bash
sudo rm /opt/wayfire/lib/x86_64-linux-gnu/wayfire/libvertical_kube.so
sudo rm /opt/wayfire/share/wayfire/metadata/vertical_kube.xml
```
