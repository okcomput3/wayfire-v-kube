# Wayfire Vertical Kube Plugin

A Wayfire plugin that displays workspace rows as vertical cubes, allowing navigation between workspace rows in a 3D cube interface.

## Installation

Targets **Wayfire master (0.12.0 development)** and also supports 0.11.0.
Requires matching Wayfire development files, Meson, Ninja,
a C++17 compiler, and the dependencies exposed by `pkg-config wayfire`.
Requires the matching `libwayfire-blur-base` library for native glass blur.
The plugin uses the GLES renderer; Vulkan and Pixman are not supported.
Rebuild the plugin whenever Wayfire's plugin ABI changes.

Run these commands from the repository root:

```bash
# Select the Wayfire master installation used here:
export PKG_CONFIG_PATH=/way/wayfire/lib/x86_64-linux-gnu/pkgconfig:/way/wayfire/share/pkgconfig:$PKG_CONFIG_PATH
pkg-config --modversion wayfire
meson setup build wf-v-kube --buildtype=release
meson compile -C build
sudo meson install -C build
```

The plugin and metadata install into the directories advertised by the selected
Wayfire pkg-config package (`plugindir` and `metadatadir`). Make sure this is the
same Wayfire installation you run.

Builds default to release optimization. For an existing debug build, use
`meson configure build -Dbuildtype=release` and rebuild.

GLES 3.2 deformation and lighting support is built automatically when its headers
are available and used only when the GPU supports tessellation. To build only the
GLES 2 fallback, pass `-Denable_gles32=disabled` to `meson setup`.

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

# Optional interior point lights
dynamic_lighting = false     # Enable orbiting hue-changing lights per cube
lighting_strength = 0.7      # Brightness (0.0-2.0)
lighting_speed = 1.0         # Orbit/hue speed (0.1-4.0)

# Desktop glass (optional; uses the installed Wayfire blur library)
desktop_opacity = 0.75        # 0.0 transparent, 1.0 opaque; windows stay separate
desktop_blur = true           # Blur behind translucent desktop faces
cap_blur = true               # Blur behind translucent caps

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
background_mode = simple      # simple, shader, image, skydome, or cubemap
shader_resolution_scale = 0.75 # Shader only: faster; use 1.0 for full detail
background = 0.1 0.1 0.1 1.0  # Background color (simple mode)

# Flat picture (image mode; PNG/JPEG, stretched to fill the output)
background_image = /home/you/Pictures/background.jpg

# Skydome options
skydome_mirror = true
skydome_texture =             # Texture file path

# Cubemap options
cubemap_image =               # Image file path
```

4. Restart Wayfire

## Usage

- **Activate cube**: Hold `Alt+Ctrl` and click left mouse button, then drag to rotate
- **Switch left/right**: `Ctrl+Alt+Left/Right` rotates to the adjacent desktop without window popout when starting from the normal desktop. In an already-open cube, the same keys rotate one face while preserving cube mode, zoom, and popout.
- **Zoom**: While the cube is active, use the mouse wheel or vertical two-finger scrolling. Scroll movement accumulates smoothly during animation, and two-finger scrolling eases to a stop after release; adjust `speed_zoom` to change sensitivity. The selected row stays the zoom focus.
- **Move between workspace rows**: `Ctrl+Alt+Up/Down` moves one fixed row at a time, independent of zoom. Row order stays unchanged when leaving and reopening the cube; movement stops at the first and last row.

## Features

### Deformation Modes
- **None** (0): Standard cube
- **Cylinder** (1): Cylindrical deformation
- **Star** (2): Star-shaped deformation

### Window Popout Effect
Creates a larger cube showing only windows without background, floating above the main cube for better visibility.

### Blur compatibility

Keep Wayfire's `blur` plugin enabled and leave `enable_window_popout = true`.
The popout reuses each window's existing blur transformer and selected blur
method (including custom methods provided by your installed blur plugin).
It supplies the workspace wallpaper as the offscreen backdrop and removes
wallpaper outside the windows before drawing the floating layer.

Your existing `[blur]` method, strength, alpha threshold, and alpha exponent
settings remain in effect. Windows without a blur transformer keep ordinary
transparency. The cutout follows the standard Wayfire blur alpha-coverage
contract; blur forks which change that contract may need an adapter.

This adds offscreen work for blurred windows while the cube is active. The
shader/image background outside the cube is separate from the workspace
wallpaper used for window blur. Set `enable_window_popout = false` only if you
want complete workspaces directly on the main cube faces.

### Optional interior point lights
dynamic_lighting = false     # Enable orbiting hue-changing lights per cube
lighting_strength = 0.7      # Brightness (0.0-2.0)
lighting_speed = 1.0         # Orbit/hue speed (0.1-4.0)

# Desktop glass (optional; uses the installed Wayfire blur library)
desktop_opacity = 0.75        # 0.0 transparent, 1.0 opaque; windows stay separate
desktop_blur = true           # Blur behind translucent desktop faces
cap_blur = true               # Blur behind translucent caps

# Cube Caps
Top and bottom polygonal caps that match the number of workspace faces, with customizable colors and optional textures.

### Background Modes
- **Simple**: Solid color background (default)
- **Shader**: Animated procedural nebula; compiled only when selected
- **Image**: Ordinary PNG/JPEG picture stretched to fill the output
- **Skydome**: Hemisphere texture background with optional mirroring
- **Cubemap**: Full 360° environment map using a 4×3 cross-layout image

To use a picture instead of the shader:

```ini
[vertical_kube]
background_mode = image
background_image = /home/you/Pictures/background.jpg
```

For a panoramic picture, use `background_mode = skydome` and set
`skydome_texture` to its absolute path. For the animated nebula, set
`background_mode = shader`. Missing or unreadable pictures fall back to the
configured `background` color. PNG/JPEG support requires Wayfire to be built with
image I/O support.

When upgrading Wayfire master, install its matching development files and rebuild
this plugin in a fresh build directory. Verified against `/way/wayfire`
0.12.0 master, commit `71f2bcfa`, ABI `20260801`. Master uses wlroots 0.20 and
wf-config 0.12; pkg-config resolves those through the selected Wayfire package.

## Uninstallation
```bash
sudo ninja -C build uninstall
```

For staged packaging, use `DESTDIR=/path/to/staging meson install -C build`.
