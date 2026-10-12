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

# Mirroring
mirror_outputs = true        # Show the activating monitor's cube on all monitors

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

## Mirrored multi-monitor cube

With `mirror_outputs = true` (the default), activate the cube on any monitor to
show that monitor's workspace cube on every connected monitor. Rotation, zoom,
and row navigation share one controller. This mirrors the same content; it does
not combine the monitors into a wider workspace. On closing, each monitor selects
the same workspace coordinate, clamped to its own grid. Normal desktops resume
on their own monitors. Set the option to `false` for independent cubes.

While the cube is open, press **Ctrl+K** to smoothly join the mirrored views
into **one panoramic cube spanning the monitor layout**. Each face contains the
monitors' own desktop contents side by side, ordered left to right. Rotation,
zoom and vertical navigation operate on that shared view. Press Ctrl+K again to
return to mirrored views. Releasing the activation button animates back to each
monitor's own desktop while remembering your chosen mode. In panoramic mode,
the joined cube/star/cylinder stays joined as it flattens and zooms into the
desktops; closing does not run the Ctrl+K split transition. Reopening the cube,
even from another monitor, restores that mode; only Ctrl+K changes it during
the running Wayfire session. The binding is configurable as
`toggle_panorama = <ctrl> KEY_K` in `[vertical_kube]`.

All participating outputs consume one shared animation snapshot before the
controller advances it. The procedural background uses continuous panoramic
coordinates, and caps and lighting use the shared frame clock. This coordinates
rendering; it does not frame-lock hardware scanout. The universal recorder below
collects these matched slices into one video stream.

Panoramic mode requires the same workspace grid dimensions on each monitor.
It captures all participating desktops, so it uses more GPU memory and rendering
work than mirroring. The first implementation is intended for a horizontal
monitor arrangement. Each desktop occupies an equal-width segment of the face.

If another effect has reserved a monitor, mirrored activation is declined as a
whole. Disconnecting a participating monitor closes the cube safely. Monitors
connected while it is open join on the next activation.

### Simulate three monitors on one screen

Run from a terminal inside your Wayland desktop, from this repository:

```bash
python3 tools/three-monitor-demo.py
```

This launches your `/way/wayfire` installation as a separate nested compositor,
with three 640×360 monitor windows and an interactive terminal on each. It copies
your configured wallpaper from `~/.config/wf-shell.ini`, plus cube effects, blur
settings and workspace grid from `~/.config/wayfire.ini`. Use `--plain` to select
the minimal blue test scene instead. Existing apps in the main session are not
copied, and main-session autostart services are not launched again. Arrange the three
windows side by side. The test uses its own temporary config and logs; it does
not change your main Wayfire configuration or physical output settings.

- Hold **Alt+Shift+left mouse** inside a test monitor and drag to rotate.
- **Alt+Shift+arrow keys** rotate or change rows; scroll to zoom.
- **Ctrl+K** while open joins the three views into one panoramic cube; press again to return.
- Release the mouse button to close the cube.
- **Alt+Shift+Enter** opens another terminal inside the test desktop.
- Press **Ctrl+C in the launching terminal** to stop the whole test.

The different modifiers avoid the main session's Ctrl+Alt cube binding. If your
host uses Alt+Shift bindings too, edit the generated test config shown at startup.
Use `--size 480x270` for smaller windows, `--prefix /path/to/wayfire` for another
installation, or `--build-dir /path/to/meson-build` to test an uninstalled build.
`--software` selects software GLES when hardware nesting is unavailable.

For the automated check (requires `foot`, `swaybg`, `grim`, and Pillow):

```bash
python3 tools/three-monitor-demo.py --smoke
```

It compares the three cube screenshots, tests controls from a follower monitor,
workspace synchronization, reopening from a non-origin monitor, and removing a
monitor while the cube is active. Screenshots and logs remain in the printed
`/tmp/v-kube-three-monitors-*` directory. `--backend headless` is available for
CI, but the custom installed wlroots software/headless path can crash independently
of this plugin; the nested Wayland backend is the verified test path here.

### Record the three simulated monitors as one panoramic video

Inside a terminal **in the simulation**, run:

```bash
python3 /path/to/wayfire-v-kube/tools/record-panorama.py panorama.mp4 --audio
```

The helper receives **complete panoramic frames directly from the cube plugin**
and feeds one FFmpeg encoder. All monitor slices belong to the same controller
frame; there are no independently timed clips and no post-recording stitching.
Restart the simulation after updating the plugin, then run the command above.
Activate the cube and press Ctrl+K. Recording starts with the first cube frame;
Ctrl+C finishes the file. Closing the cube holds the last image until you reopen
it or stop recording.

Omit `--audio` for silent video. With `--audio`, desktop sound comes from the
default audio output's monitor, in the same encoder. Use `--fps 60` for 60 fps or
`--seconds 20` to stop after 20 seconds of video. Three 640×360 simulated outputs
produce one 1920×360 video, in their actual output layout. Capture uses logical
output resolution and is intended for the horizontal, unrotated simulation.
Resizing the layout requires a new recording. Late frames repeat the **whole**
previous panorama, so one monitor never advances independently in the video.

Requires the updated cube plugin, Wayfire IPC and `ffmpeg` (`pactl` for audio).
Native framebuffer readback adds GPU/CPU work; lower the simulation resolution
or recording FPS if necessary. Capture buffers are allocated only while recording.

For a single output, use `wf-recorder -o WL-1 -f myvid.mkv -a`: the filename
requires `-f`, otherwise wf-recorder uses its default recording filename.

Cap ripple bump mapping is enabled by default. Set `cap_bump_mapping = false`
in `[vertical_kube]`, or turn off **Cap bump mapping** in WCM, for smooth caps.
Cap transparency, blur and dynamic lighting keep their existing settings.

Set `cap_texture_top` and `cap_texture_bottom` to picture paths (PNG/JPEG), or
choose **Top cap texture** / **Bottom cap texture** in WCM. Empty or invalid paths
fall back to the cap color. Picture transparency is preserved by default. Enable **Cap colour underneath
pictures** (`cap_picture_underlay = true`) to fill transparent areas with the
top/bottom cap colour instead. Pictures receive the same optional ripple lighting.
Enable **3D cap ripples** (`cap_bump_3d = true`, default false) for real mesh
relief on either pictures or plain caps. This adds rendering cost; the perimeter
stays attached and relief flattens on return to the desktop. The relief follows
the animated ripple pattern, not the brightness of the chosen picture.

Rotate cap pictures independently with `cap_rotation_top` and
`cap_rotation_bottom` (degrees, -180 to 180; default 0). These rotate only the
picture; the cap outline and ripple pattern stay fixed.

Enable **Separate depth for each window** (`per_window_depth = true`) to give
individual windows their own popout layers. Front windows sit farther out;
**Window depth spacing** (`window_depth_spacing = 0.25` by default) controls
additional separation relative to the normal popout distance. Depth smoothly
returns to zero on closing. The option is off by default and uses extra GPU
memory/rendering work. It works with panoramic faces and the native window blur.

### OBJ models inside the cube

The **Object inside cube** settings add a textured Wavefront `.obj` model
inside each workspace row. Enable **Enable OBJ model**, set **OBJ model path**
(an absolute path or `~/...`), then adjust **Object size** and **Object rotation**.
Use the file or folder browse buttons beside **OBJ model or folder path** in WCM
to choose a single model or a folder containing `.obj` files. Each cube
entry randomly chooses one model, shared across monitors and retained until you
close the cube. It avoids immediate repeats when the folder contains multiple
models. Only the folder's direct OBJ files are considered (case-insensitive);
subfolders are not searched. An empty folder displays no object.

Enable **Flip object vertically** if your model appears upside down.
The model is centered automatically, follows the cube rotation and smoothly
scales in/out with activation. Reduce desktop/cap opacity to see inside; opaque
faces correctly hide it. The same object appears in mirrored and panoramic views.

For a quick test, use this repository's `examples/cube-object.obj`.

```ini
[vertical_kube]
object_enabled = true
object_path = /absolute/path/to/model.obj
object_scale = 0.6
object_rotation = 0
```

The importer supports triangle faces, convex polygons, positive/negative indices,
and supplied normals (or generated face normals). Export concave polygons as
triangles. Material libraries (`mtllib`/`usemtl`) load automatically. Keep the `.obj`, `.mtl`,
and texture folders together: MTL paths resolve relative to the OBJ, and image
paths relative to the MTL. PNG/JPEG diffuse (`map_Kd`), specular (`map_Ks`) and
opacity (`map_d`) maps are supported, including UV scale/offset and clamp options.
Material colors (`Ka`, `Kd`, `Ks`, `Ke`), shininess (`Ns`), opacity (`d`/`Tr`) and
basic `illum` shading are respected. Texture alpha and opacity maps support
cutouts; fractional opacity uses screen-door dithering to preserve cube depth
ordering. Missing images fall back to the material color and log a warning.
OBJ normal/bump maps, PBR extensions and reflection/refraction are not
implemented. OBJ is static; GLB animation playback is described below. Files are limited to 64 MiB and 500,000
triangles. Models are cached until the path changes or the option is switched off;
toggle off and on to reload an edited file. Large models cost more GPU time.

CPU parsing is isolated in `wf-v-kube/src/obj-loader.cpp` and `obj-material.cpp`, with a separate
`obj-renderer.cpp` for GPU buffers, shading and cleanup. The loader test can run
without Wayfire:

```sh
c++ -std=c++17 -Iwf-v-kube/src tests/test_obj_loader.cpp wf-v-kube/src/obj-loader.cpp wf-v-kube/src/obj-material.cpp -o /tmp/test-cube-obj
/tmp/test-cube-obj
```

### GLB models

The same **OBJ/GLB model or folder path** accepts binary glTF 2.0 (`.glb`).
Random folder selection includes both OBJ and GLB files. GLB imports scene/node
transforms, triangle meshes, normals, UV0, embedded or external PNG/JPEG base-color
textures, material colors, emissive factors and alpha cutouts. Metallic/roughness
factors are approximated using the existing cube object lighting, not a full PBR
renderer. GLB's top-left texture orientation is handled automatically.

Enable **Play model animation** to loop GLB clips while the cube is open.
**Animation clip** selects a zero-based clip index; **Animation speed** controls
playback rate. Clip names are printed in the Wayfire log. Playback restarts on
cube entry or clip changes, and uses the shared monitor frame clock. Disable it
to return to the rest pose. Static OBJ files are unaffected.

Node translation/rotation/scale, skeletal skinning (up to eight influences) and
morph weights are supported, with STEP, LINEAR and CUBICSPLINE interpolation.
Quaternions use spherical interpolation for LINEAR tracks. Model normalization
stays fixed across frames so walking does not cause automatic resizing.
Textures are loaded once; animated vertices update once per shared frame.
Large animated meshes require additional CPU/GPU work.
Normal maps and metallic/roughness image maps are not applied. Draco/Meshopt/KTX2 and
other unsupported required extensions are rejected with a log message. Export
uncompressed triangles with PNG/JPEG textures. Texture-transform rotation and
UV sets other than UV0 are not supported. Partial transparency uses coverage
dithering, as with OBJ materials.

Importer implementation: `wf-v-kube/src/glb-loader.cpp`, using vendored cgltf and
stb_image (licences in `src/third-party`). Validate with
`python3 tests/test_glb_loader.py`.

The downloaded textured [Fox example](examples/animated/fox/README.md) includes
Survey (0), Walk (1) and Run (2). Its model is by PixelMannen; rigging/animation
by tomkranis; glTF conversion by AsoboStudio/scurest. See the included licence
and attribution. This animates geometry, not video textures.

Object size supports **0.05–10.0**. **Object position X/Y/Z** moves the model
left/right, vertically and front/back relative to each cube row centre
(range -10 to +10, default 0). Position rotates with the cube, independently of
the model's own rotation, flip and size, and eases in/out with cube activation.
Large sizes or offsets can place geometry beyond the caps and desktop faces.

### Cinematic camera

While the cube is open, **Ctrl+Alt+Shift+Left/Right/Up/Down** plays a cinematic
camera sweep: sideways orbit or vertical swoop with bank, tilt and pullback.
Each move eases from and back to the existing view without changing workspace.
Press a direction during a sweep to queue the next move (latest direction wins).
Closing the cube fades out the camera offset so it still lands on the desktop.
Mirrors and panorama use the same sampled camera pose. WCM's **Cinematic camera**
group exposes bindings, sweep duration (default 2.4 seconds) and strength.

### Rotating searchlights

WCM → **Rotating searchlights** → **Enable rotating searchlights** turns on
color-changing beams radiating from each cube row centre. Adjust **Rotation
speed**, **Beam brightness**, **Beam reach** and **Beams per cube**. Off by default.
Beams rotate, tilt and cycle hues using the shared frame clock, fading with cube
activation. Opaque geometry occludes beam sections; transparent glass allows
the stylized additive glow through. These are lightweight glowing ribbons,
not volumetric scattering or shadow-casting lights. Implementation is isolated
in `wf-v-kube/src/cube-searchlights.cpp` and its header.

Enable **Refract beams through cube** to trace beams against each row's optical
volume. **Beam refractive index** controls bending (1.0 is straight through;
1.45 is the default). Rays split at side/cap boundaries, refract on exit and
entry, and reflect internally when they cannot escape. Cylinder/star boundaries
follow the animated desktop deformation. Two-sided desktop layouts have no
closed volume, so their rays remain straight. Tracing is bounded to twelve
segments per beam. The optical envelope excludes decorative cap relief and
rounded-edge cutouts; this remains a stylized beam effect, not glass rendering
or illumination of nearby surfaces. CPU geometry is in `searchlight-path.hpp`.

### Cube mist

WCM → **Cube mist** → **Enable drifting mist** adds softly animated smoke
around each cube row. Off by default; adjust **Mist density** (0–3) and
**Mist drift speed** (0.1–5). It follows the deformed perimeter and fades with
cube activation, using the shared monitor clock. The effect uses 48 procedural
billboards per row in a single draw, with no image downloads or simulation
buffers. The camera-facing half fades out per fragment as you rotate, and mist is
painted before desktop surfaces to keep the front content clear. Five layers
of smoothly warped noise provide continuously drifting detail. Transparent
glass composites over the rear mist; this is stylized smoke, not volumetric
scattering or refraction. Implementation: `cube-mist.cpp/.hpp`.

### Window edge thickness

Set **Window edge thickness** above zero (try 0.02–0.04) to extrude pop-out
windows inward into slabs. Zero disables it. Each boundary pixel supplies its
own RGB along the entire side wall, preserving a Pixdecor marble pattern rather
than stretching one average border colour. Rounded corners and alpha cutouts
follow the captured pixel silhouette. **Window edge alpha cutoff** (default
0.8) excludes faint shadows; lower it for translucent decorations.

Edges are captured against transparent black before supplying the native blur
backdrop, so wallpaper/blur is not used as the side material. This requires the
GLES 3.2 rendering path. Thickness fades with cube activation and works with
separate window depths. It adds per-window captures and GPU silhouette work;
it is disabled by default. Geometry and shaders live in `window-thickness.cpp`.

**Desktop face thickness** provides the same border-pixel extrusion independently
for desktop faces. Try 0.02–0.04; zero disables it. The outline follows desktop
rounding and the sides sample the desktop texture before glass blur. Thickness
fades with activation and follows the deformed surface normal, including cylinder
and star modes. Like window thickness, it requires GLES 3.2 and adds GPU work.

Thickness rendering conservatively rejects off-screen faces and 128-pixel
texture tiles before silhouette geometry runs. Visible regions are merged into
draw calls. Bounds include extrusion depth and animated deformation, and use
each output's projection so panoramic slices skip unrelated regions. This is
frustum culling, not an occlusion test: rear geometry that may be visible through
transparent faces or open caps is retained.

### Cosmic accents

Optional reference-inspired effects are under **Cosmic accents** in WCM:
**Glowing cap rims** (requires caps enabled), **Orbital light rings**, and
**Orbiting star sparkles**. Each defaults off. Start with brightness 1.0 and
speed 1.0, then adjust to taste. These add cyan/gold highlights and orbiting
trails without changing the existing cap picture, lighting, mist or thickness.
The rounded rim follows cube/cylinder/star deformation; effects fade with cube
activation and use the shared multimonitor clock. Opaque scene geometry occludes
hidden accents. The glow is stylized additive geometry, not reflections, optical
bloom or holographic panels. No new textures or full-screen postprocessing are
required. Implementation is isolated in `cosmic-accents.cpp/.hpp`.

Orbital rings use compact spacing by default, with smoothly tapered trails and
moving thicker highlights. **Ring distance from cube** defaults to 0.08 (previous
fixed spacing was 0.32, plus a wider second ring). **Ring trail thickness** scales
the width variation; start at 1.0. Ring tilt is reduced and the two tracks sit
near the top/bottom edges. Orbiting sparkles follow the same compact tracks.

**Crystal cap highlights** (off by default) adds view-dependent cyan/gold glints
and a grazing-angle sheen under Cosmic accents. **Crystal highlight strength**
controls their intensity. It preserves picture coverage and cap alpha, works
with existing bump mapping, and fades with cube activation. These are stylized
specular highlights, not reflections of the desktop or other windows.

The panorama recorder accepts `--ready-file PATH` for automation: it creates a
new file after the first frame reaches the encoder. Automated camera movement
and cube closure should wait for this signal rather than a startup delay.

### Selective bloom

Enable **Selective bloom → Enable accent bloom** for halos around cosmic rims,
rings, sparkles and searchlights. Adjust strength (default 1), spread (8 pixels)
and brightness threshold (0.15). Fast quality blurs at quarter resolution; High
uses half resolution. Off by default, with buffers released when unused.

The source is the positive RGB difference before and after those additive
accents, so desktop text, wallpaper, and cap pictures are not bloom sources.
Crystal cap highlights are currently excluded. Bright backgrounds can reduce
bloom because the output clamps colour before extraction. Blur operates within
each output; light outside a monitor's capture cannot spill into its neighbor.
Two full-resolution RGB snapshots are required, but all four blur passes run
at reduced resolution. Implementation: `accent-bloom.cpp/.hpp`.

### Alternative shader background

Choose **Background mode → Alternative shader** for a modified copy of the original
volumetric shader: gold emission clouds, deep blue space and moving stars,
with faster drifting motion and stronger contrast inspired by the cosmic reference. **Animated shader** remains
the original scene. Both use **Shader background resolution** and span the
participating monitors. The new shader is in `alternative-background.tpp`;
it adds only the background, not the reference's holographic panels or crystals.
