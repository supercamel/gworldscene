# SQGI Examples

These examples use `sqgi`, the Squirrel runtime bridge for GObject
Introspection, to drive the GWorldScene typelib directly.

After installing the library, run the GTK 3 or GTK 4 examples directly with
`sqgi`:

```sh
sudo meson install -C builddir
cd examples/sqgi
sqgi introspection-gtk3.nut
sqgi simple-scene-gtk3.nut
sqgi introspection-gtk4.nut
sqgi simple-scene-gtk4.nut
```

`introspection.nut` and `simple-scene.nut` default to GTK 4. The explicit
`*-gtk3.nut` and `*-gtk4.nut` launchers select the matching Gtk and
GWorldScene introspection namespaces.

## Calibrated camera (GTK 4)

`calibrated-camera.nut` demonstrates the exact quaternion pose and calibrated
projection APIs. It requires a build containing `set_camera_pose` and
`set_camera_projection` (the older 0.2.1 install does not contain them).

From the repository root, use the local build without installing it:

```sh
meson compile -C build
GI_TYPELIB_PATH="$PWD/build/src${GI_TYPELIB_PATH:+:$GI_TYPELIB_PATH}" \
LD_LIBRARY_PATH="$PWD/build/src${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
sqgi examples/sqgi/calibrated-camera.nut
```

After installing the updated library, simply run
`sqgi examples/sqgi/calibrated-camera.nut`.

- **Scroll over the scene** to zoom; the zoom slider uses the same control.
- Change **heading, pitch and roll** independently. Negative pitch looks down.
- Change **altitude (AMSL)** or translate **east/west and north/south** without rotating.
- Shift the **principal point** to see an off-centre projection without turning
  the camera. Reset restores every control.

The default scene looks toward the Cairns foothills in Australia, using the
same Google satellite imagery setup as `simple-scene.nut`, and the library's
ooblerg.xyz terrain default with its disk
cache enabled. Allow time for the initial downloads. Altitude is **AMSL**, not
height above local terrain; exact pose does not automatically avoid hills.

Set `GWORLD_SCENE_MAP_TILE_URL_TEMPLATE` to use another XYZ imagery provider
and `GWORLD_SCENE_MAP_ATTRIBUTION` to its required attribution text. Like the
other examples, `GWORLD_SCENE_GOOGLE_MAPS_API_KEY` together with
`GWORLD_SCENE_GOOGLE_MAPS_SESSION` selects the Google Map Tiles API; otherwise
the existing legacy Google satellite template is used.
`--offline` explicitly selects a synthetic flat terrain/grid scene instead;
`--smoke-test` also uses that fixture to keep automated checks independent of
network availability.

The sensor is 1280 × 720;
black margins preserve its aspect ratio as the window changes size. Zoom
scales both focal lengths, keeping the eye fixed; it demonstrates optical
projection rather than digital crop/resampling. Tooltips describe conventions.
The scene's observer gestures are disabled so they cannot override exact pose.
The example includes the heading/pitch/roll-to-quaternion calculation in source.

For an automated launch/control/capture check, append `--smoke-test` to the
command (use `xvfb-run -a` before `sqgi` on a headless Linux host). Set
`GWORLD_SCENE_DEMO_SCREENSHOT=/tmp/camera.png` to save the smoke test's rendered
frame. The test exits automatically and fails if its camera checks fail.
See [the camera API guide](../../docs/calibrated-cameras.rst) for conventions,
clipping limits, shared terrain and frame ownership.
