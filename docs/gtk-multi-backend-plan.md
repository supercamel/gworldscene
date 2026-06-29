# GTK3, GTK4, and Offscreen Rendering Plan

## Goals

- Build GWorldScene from one code base against GTK3, GTK4, or both.
- Keep GTK-specific code at the presentation boundary only.
- Move node ownership, camera math, picking, rendering preparation, tile downloading, terrain loading, and cache management into toolkit-neutral layers.
- Leave a clear path for future offscreen framebuffer rendering, including simulated gimbal camera views.
- Allow GTK3-only systems to build a GTK3 package, GTK4-only systems to build a GTK4 package, and systems with both development packages to build both.

## Non-Goals

- Do not make one binary dynamically switch between GTK3 and GTK4 at runtime.
- Do not support loading GTK3 and GTK4 in the same application process.
- Do not fork the renderer or scene graph for each GTK major version.

GTK3 and GTK4 are separate ABI worlds. The widget libraries should be separate build targets, while their shared engine code should be common.

## Target Package Shape

Build these installable packages:

- `gworldscene-core-0.1`
- `gworldscene-gtk3-0.1`
- `gworldscene-gtk4-0.1`

The GTK libraries link to `gworldscene-core-0.1`.

Suggested installed artifacts:

- `libgworldscene-core-0.1.so`
- `libgworldscene-gtk3-0.1.so`
- `libgworldscene-gtk4-0.1.so`
- `gworldscene-core-0.1.pc`
- `gworldscene-gtk3-0.1.pc`
- `gworldscene-gtk4-0.1.pc`
- `GWorldSceneCore-0.1.gir`
- `GWorldSceneGtk3-0.1.gir`
- `GWorldSceneGtk4-0.1.gir`
- matching `.typelib` and `.vapi` files

C headers should install under separate include roots so both GTK variants can coexist:

- `gworldscene-core-0.1/gworldscene/...`
- `gworldscene-gtk3-0.1/gworldscene/...`
- `gworldscene-gtk4-0.1/gworldscene/...`

The C API can keep the existing `gworld_scene_*` symbol names. The package names, library names, GIR names, and include roots prevent install-time collisions. Applications should link either GTK3 or GTK4, not both.

## Target Architecture

### Core Layer

No GTK dependency.

Owns:

- scene nodes and node styling
- scene graph storage and change tracking
- geodetic math and local frames
- camera models and camera movement
- picking math and hit results
- LOD decisions
- light and sun calculations
- terrain and map tile scheduling
- cache path and cache policy
- downloading through GIO/libsoup
- image/model loading abstractions

Current candidates:

- `gworld-scene-node.*`
- `gworld-scene-camera.*`
- `gworld-scene-geo.*`
- `gworld-scene-light.*`
- `gworld-scene-lod.*`
- `gworld-scene-picking.*`

The current `GWorldSceneViewBackend` should evolve into a toolkit-neutral scene engine or controller. It should not know about `GtkWidget`, `GtkGLArea`, event controllers, or `gtk_widget_queue_draw()`.

The core should expose an explicit scene context:

```c
GWorldSceneContext *gworld_scene_context_new(void);
void gworld_scene_context_add_node(GWorldSceneContext *context,
                                   GWorldSceneNode *node);
```

`GWorldSceneView` can create a context internally for the simple API, but advanced users should be able to provide a context. This lets an onscreen GTK view and an offscreen gimbal renderer share nodes, terrain, textures, downloads, and cache state.

### Rendering Layer

No GTK dependency, but it may depend on OpenGL through epoxy.

Owns:

- shader creation
- GL resource lifetime
- mesh and texture uploads
- world draw calls
- billboard and overlay rendering
- framebuffer target selection

The renderer should assume a GL context is current, but not know who created that context.

Proposed API shape:

```c
typedef struct {
  guint framebuffer;
  int width;
  int height;
} GWorldSceneRenderTarget;

void gworld_scene_renderer_realize(GWorldSceneRenderer *renderer);
void gworld_scene_renderer_unrealize(GWorldSceneRenderer *renderer);
gboolean gworld_scene_renderer_render(GWorldSceneRenderer *renderer,
                                      const GWorldSceneRenderTarget *target,
                                      const GWorldSceneCamera *camera);
```

For an onscreen GTK view, `framebuffer` is `0`. For offscreen rendering, it is an owned FBO.

### Widget Adapter Layer

GTK-specific.

Owns:

- `GtkGLArea` subclassing
- GL context creation and current-context calls
- frame clock integration
- queue-draw requests
- mouse, touchpad, keyboard, and drag input mapping
- widget size lookup
- focus handling
- GTK-specific examples

Files should split roughly into:

- `gworld-scene-view-gtk-common.cpp`
- `gworld-scene-view-gtk3.cpp`
- `gworld-scene-view-gtk4.cpp`

The GTK adapters translate toolkit events into shared engine calls such as:

- `engine->rotate_camera_pixels(dx, dy)`
- `engine->zoom_camera(delta)`
- `engine->set_movement_key(key, pressed)`
- `engine->pick_at(x, y, width, height)`
- `engine->render(target, width, height)`

## Meson Plan

Add feature options:

```meson
option('gtk3', type: 'feature', value: 'auto', description: 'Build GTK3 widget library')
option('gtk4', type: 'feature', value: 'auto', description: 'Build GTK4 widget library')
```

Probe dependencies:

```meson
gtk3_dep = dependency('gtk+-3.0', version: '>=3.24', required: get_option('gtk3'))
gtk4_dep = dependency('gtk4', version: '>=4.10', required: get_option('gtk4'))
```

Build behavior:

- GTK3 dev package only: build `gworldscene-core-0.1` and `gworldscene-gtk3-0.1`
- GTK4 dev package only: build `gworldscene-core-0.1` and `gworldscene-gtk4-0.1`
- both GTK dev packages: build all three libraries
- neither GTK dev package: fail with a clear error
- `-Dgtk3=enabled`: GTK3 dependency is required
- `-Dgtk4=enabled`: GTK4 dependency is required
- `-Dgtk3=disabled -Dgtk4=disabled`: fail unless a future `build-core-only` option is added

Use a Meson helper for GTK variants:

```meson
build_gtk_variant(
  name: 'gtk4',
  gtk_dep: gtk4_dep,
  gtk_major: 4,
  gir_namespace: 'GWorldSceneGtk4',
  gir_includes: ['Gtk-4.0', 'GObject-2.0'],
  vapi_packages: ['gtk4'],
)
```

For GTK3:

```meson
build_gtk_variant(
  name: 'gtk3',
  gtk_dep: gtk3_dep,
  gtk_major: 3,
  gir_namespace: 'GWorldSceneGtk3',
  gir_includes: ['Gtk-3.0', 'GObject-2.0'],
  vapi_packages: ['gtk+-3.0'],
)
```

Each variant should compile with:

```meson
cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=3']
```

or:

```meson
cpp_args: ['-DGWORLD_SCENE_GTK_MAJOR=4']
```

## GTK API Differences To Isolate

### Widget Size

GTK4:

```c
gtk_widget_get_width(widget);
gtk_widget_get_height(widget);
```

GTK3:

```c
gtk_widget_get_allocated_width(widget);
gtk_widget_get_allocated_height(widget);
```

Hide this behind:

```c
gworld_scene_widget_get_size(widget, &width, &height);
```

### Focus

GTK4:

```c
gtk_widget_set_focusable(widget, TRUE);
```

GTK3:

```c
gtk_widget_set_can_focus(widget, TRUE);
```

### Input

GTK4 currently uses:

- `GtkGestureClick`
- `GtkGestureDrag`
- `GtkEventControllerScroll`
- `GtkEventControllerKey`
- `gtk_widget_add_controller()`

GTK3 should use:

- `gtk_widget_add_events()`
- `button-press-event`
- `button-release-event`
- `motion-notify-event`
- `scroll-event`
- `key-press-event`
- `key-release-event`

The callbacks should be GTK-specific, but the camera, picking, and movement behavior should be shared.

### Window Examples

GTK4:

```c
gtk_window_set_child(GTK_WINDOW(window), view);
gtk_window_present(GTK_WINDOW(window));
```

GTK3:

```c
gtk_container_add(GTK_CONTAINER(window), view);
gtk_widget_show_all(window);
```

Examples should build as:

- `gworldscene-demo-gtk3`
- `gworldscene-demo-gtk4`
- `gworldscene-controls-gtk3`
- `gworldscene-controls-gtk4`

## Offscreen Rendering And Gimbal Camera Path

A future gimbal camera should not require a GTK widget. It should reuse the same scene, terrain, textures, model loading, picking, and renderer.

Add a core camera object:

```c
GWorldSceneCamera *gworld_scene_camera_new(void);
void gworld_scene_camera_set_position(...);
void gworld_scene_camera_set_orientation(...);
void gworld_scene_camera_set_lens(...);
```

The existing interactive view camera becomes one consumer of this object. A gimbal camera becomes another.

Add an offscreen renderer:

```c
GWorldSceneOffscreenRenderer *gworld_scene_offscreen_renderer_new(GWorldSceneContext *context);
gboolean gworld_scene_offscreen_renderer_render(GWorldSceneOffscreenRenderer *renderer,
                                                GWorldSceneCamera *camera,
                                                int width,
                                                int height,
                                                GBytes **rgba_pixels);
```

OpenGL concerns:

- create and own an FBO
- create color texture and optional depth renderbuffer
- render with the same `GWorldSceneRenderer`
- optionally return a GL texture id for in-process consumers
- optionally read back RGBA pixels for recording, streaming, or tests

Gimbal-specific requirements to keep in mind:

- camera pose may be attached to a moving scene node or aircraft pose
- gimbal yaw, pitch, and roll should be independent from the main interactive camera
- lens/FOV/aspect should be per camera
- rendering should not require a visible widget
- rendering should be able to run at a fixed output resolution
- later, the offscreen camera may need overlays, target tracking, or stabilized horizon modes

The important design rule: the GTK view is only one presentation of a scene. It should not own the scene engine in a way that prevents another camera from rendering the same world.

## Migration Phases

### Phase 1: Build System Skeleton

- Add `gtk3` and `gtk4` Meson feature options.
- Detect installed GTK variants.
- Keep the current GTK4 build working.
- Rename the current generated package to `gworldscene-gtk4-0.1`.
- Add a compatibility pkg-config alias only if needed for existing users.

### Phase 2: Source Split Without Behavior Changes

- Split current sources into `core_sources`, `renderer_sources`, and `gtk_sources`.
- Move GTK-free files into a core library.
- Keep `GWorldSceneView` behavior unchanged.
- Make tests link against core where possible.

### Phase 3: Renderer Extraction

- Move GL resource management and draw calls out of `GWorldSceneView`.
- Introduce `GWorldSceneRenderer`.
- Replace direct GTK size and draw assumptions with a render target.
- Keep onscreen GTK4 rendering green throughout.

### Phase 4: GTK Adapter Split

- Create GTK4 adapter from the current widget code.
- Create GTK3 adapter with event-mask based input.
- Add variant-specific examples.
- Generate separate GIR/VAPI files.

### Phase 5: Offscreen Renderer

- Add an offscreen renderer that uses the shared renderer.
- Add FBO render target support.
- Add a smoke test that renders a tiny frame and verifies non-empty pixels when a GL test environment is available.
- Add a gimbal camera sample after the offscreen path exists.

## Testing Strategy

Core tests:

- always run
- no GTK dependency
- node, camera, geo, picking, light, LOD

GTK variant tests:

- run only for built variants
- instantiate `GWorldSceneView`
- verify widget type and basic API calls
- smoke test render where a display/GL context is available

Renderer tests:

- prefer offscreen/FBO tests once available
- keep tests tolerant of headless builders
- use skipped tests, not failures, when no GL context provider exists

Packaging tests:

- build with only GTK3 installed
- build with only GTK4 installed
- build with both installed
- build with `-Dgtk3=enabled -Dgtk4=disabled`
- build with `-Dgtk3=disabled -Dgtk4=enabled`

## Risks And Decisions

- GIR namespace naming needs an early decision. Separate `GWorldSceneGtk3` and `GWorldSceneGtk4` names avoid install collisions.
- Public C symbol names can stay stable, but applications should link only one GTK variant.
- GTK3 input behavior must match GTK4 closely enough that camera and picking feel identical.
- GL context sharing matters for future offscreen rendering. The renderer should avoid assuming the default framebuffer is the only target.
- Downloading and tile cache code must notify through toolkit-neutral callbacks or signals. GTK adapters can translate those notifications into `queue_draw()`.

## Success Criteria

- A GTK4-only developer machine builds the GTK4 library and examples.
- A GTK3-only developer machine builds the GTK3 library and examples.
- A machine with both GTK dev packages builds both libraries in one Meson build directory.
- Most tests run without GTK.
- The renderer can draw into either a GTK widget framebuffer or an explicit FBO.
- A future gimbal camera can be implemented as another camera plus an offscreen render target, not as another widget implementation.
