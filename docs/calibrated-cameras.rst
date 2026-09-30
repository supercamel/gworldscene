Calibrated cameras, frame capture and shared terrain
====================================================

These APIs are generic building blocks for inspection, robotics, simulation
and multi-view applications. They do not implement a particular device,
actuator controller, camera mount or video encoder. Both GTK variants export
the same API through C, GIR, Vala and SQGI.

Exact camera pose
-----------------

``set_camera_pose(latitude, longitude, altitude_amsl, qw, qx, qy, qz)``
atomically sets the eye position and full orientation, selects free-camera
mode, and queues scene work. Unlike the interactive observer it applies no
orbit displacement, pitch clamp or 25 metre minimum altitude. Latitude is
[-90,90], longitude [-180,180], altitude [-12000,10000000] metres. Nonfinite
values and a zero quaternion are errors; invalid calls change nothing.
Nonzero finite quaternions are normalized, including very large/small inputs.

Quaternion order is **w,x,y,z**. It rotates optical **forward/right/down**
into geographic **north/east/down**. Identity looks north; optical up points
toward zenith. A positive 90 degree rotation around the forward axis makes
image up point east. All positions and scene terrain use the same numeric
AMSL reference. Convert ellipsoidal input heights to the desired AMSL datum
before calling; the library does not infer a geoid or vehicle Home offset.

``get_camera()`` reads the eye, ``get_camera_quaternion()`` returns the four
normalized components, and ``get_camera_pose_enabled()`` identifies whether
that exact orientation is active. Legacy heading/pitch setters, look-at
operations and changing camera mode leave the quaternion orientation path;
``get_camera_quaternion()`` then returns identity. Set complete pose again to
resume externally controlled orientation. Interactive navigation is not a
mount controller; applications own its input policy.

Calibrated projection
---------------------

``set_camera_projection(width, height, fx, fy, cx, cy, near_m, far_m)``
accepts pinhole intrinsics in sensor pixels. Image origin is the top-left edge;
pixel centers are ``(i+0.5,j+0.5)``. The optical axis intersects ``(cx,cy)``;
``fx`` and ``fy`` are independently calibrated focal lengths. Off-centre
principal points are supported. Output resizing letterboxes this sensor image
instead of changing FOV or stretching its aspect ratio. Picking uses the same
projection and rejects letterbox margins.

Width and height are 1–8192; focal lengths are 0.000001–1000000000 pixels,
principal-point coordinates have magnitude at most 1000000000 pixels. The near
plane is at least 0.001 m and the far plane is greater than near, at most
100000000 m. Values must be finite. Invalid calls retain the previous
projection. ``reset_camera_projection()`` restores the observer's 45 degree
vertical FOV and altitude-dependent clipping without changing pose.

Imagery detail follows the calibrated view. The finest tile layer is centred
on the terrain crossing of the image-centre ray (including principal-point
shift and roll), rather than beneath the eye. Its zoom level follows ground
range divided by sensor focal length, capped at XYZ level 19. Increasing focal
length requests finer source tiles; it does not merely magnify the atlas.
The existing atlas/tile budgets bound coverage, with coarser layers filling
outside the focused patch and during downloads. Provider zoom limits and
available source imagery still limit actual detail. Selection uses sensor
pixels, so resizing a widget does not churn tile levels.

Ground targeting uses bounded height sampling and crossing refinement; when
terrain is missing it temporarily uses the eye's ground-height estimate (or
sea level) and refines on terrain arrival. A sky-facing centre ray without a
ground crossing within the available terrain range does not request a focused
patch. This is imagery selection, not a precision terrain-intersection API.
Projection changes schedule tile demand even when camera position is fixed.

For a centred image, ``horizontalFOV = 2 atan(width/(2 fx))`` and
``verticalFOV = 2 atan(height/(2 fy))``. A digitally cropped image is a separate
sampling operation: render the base sensor, then crop/resample if its finite
resolution matters. Merely increasing focal length while retaining output
resolution does not simulate the lost detail of digital magnification.
Lens distortion, sensor switching, exposure and other imaging effects belong
to the consumer, not this pinhole renderer.

Calibrated projection uses logarithmic depth for world terrain, solid models,
billboards and ground overlays. This supports centimetre near planes and
metre separation at distant ranges without the depth collapse of a standard
near/far ratio. Shadow maps retain their own depth convention and cover a
conservative frustum around off-centre sensors. The observer retains its
ordinary depth shader, so enabling this feature in one view does not change
other views' rendering policy. Atmosphere, lighting and sun glow remain scene
visualizations, not a radiometrically calibrated sensor model.

Moving and articulated objects
------------------------------

Represent separately moving rigid components as retained model nodes. The
application composes parent/mount/pivot transformations and updates each node's
geodetic position, NED orientation and physical scale. Asset node transforms
are imported by Assimp; arbitrary named subnode animation inside one imported
file is not exposed by this API. Export separate rigid meshes for articulation.

Scene-node changes are applied before the next render/capture, using current
pose data, independently of asynchronous terrain builds. Terrain vertices are
retained. Updates with unchanged geometry sizes upload just the node portion
of the buffers; asset import caches survive pose edits. A frame cannot keep an
old object transform simply because a terrain download is stalled. The first
asset import and changed-topology buffer allocation still cost time: warm
models before a time-sensitive observation loop. This is not a flight or
actuator dynamics engine, and it never applies an automatic minimum model size.

Capturing and sharing frames
----------------------------

``capture_frame(width, height, timestamp_us)`` renders current state to a
retained offscreen framebuffer and returns an owned ``SceneFrame``. It works
with a realized view that is not mapped, including before first mapping when
its native ancestor and the view have been explicitly realized. It does not
create an invisible window for the application or bypass GTK's context setup.
An unrealized, disposed or recursively capturing view is an error. Context,
allocation and readback failures are returned as errors, not frozen success.

The capture dimensions are 1–8192 and at most 16777216 pixels. The hardware
texture limit may be smaller. Calibrated capture preserves the full sensor
aspect using opaque black bars. The image contains the native scene, without
GTK overlay widgets. Native scene labels/credit overlays remain scene content;
applications choose which nodes/layers to include in a dedicated camera view.

A frame provides:

* ``get_image()``: borrowed, **read-only** top-down sRGB RGBA ``GdkPixbuf``;
  retain the frame or an image reference while using it.
* ``get_timestamp()``: the caller's microsecond timestamp, in the caller's
  chosen clock domain; the renderer does not pretend this is a physical
  device exposure timestamp.
* ``get_completed_time()``: GLib monotonic completion time, in microseconds.
* ``get_sequence()``: increasing successful-capture id scoped to the view.

Frames own independent CPU images. Later captures and view/context destruction
do not invalidate retained frames. GTK 4 consumers can make a ``GdkTexture``
from the image; GTK 3 and encoders can consume the pixbuf/pixels directly.
Share one frame between displays and encoders instead of rendering a separate
camera for each consumer. Retaining frames or their image references also retains imagery attribution
leases for those pixels; the provider's release signal must not fire early.

**Capture is synchronous on the GTK thread.** It submits rendering and reads
pixels back; it is not an asynchronous video codec or a zero-copy GPU texture
API. It does not spin a nested main loop or wait for network/terrain jobs.
Applications should bound capture cadence, retain only necessary frames, and
pass CPU pixels to an encoder worker. Correlate the caller timestamp with the
pose and node updates immediately preceding capture. Live displays driven by
GTK render independently unless the application presents the captured image
itself. Capture preserves the current GL context, framebuffer/viewport and
pixel-pack state, and restores widget presentation attribution bookkeeping.

Use ``set_offscreen_enabled(true)`` before hiding a view whose camera consumer
still needs terrain/imagery updates. Disable it when that consumer stops. It
keeps demand active but does not create a render timer or prevent unrealization.
The application chooses recording/pause semantics and owns the view's lifetime.

Shared application terrain
--------------------------

``SceneTerrainSource`` is a toolkit-independent, application-fed elevation
store. Attach the **same instance** to all consumers with
``view.set_terrain_source(source)``. Decoded immutable tiles are shared between
views and their mesh workers; merely giving independent views the same disk
cache path would not achieve this. ``get_terrain_source()`` returns a borrowed
reference. Passing null restores built-in terrain loading.

``put_tile(south, west, dimension, samples)`` takes signed 16-bit big-endian
heights in metres AMSL, rows north-to-south and columns west-to-east, including
the shared tile boundaries. The tile spans one degree. Its south edge is
[-90,89], west edge [-180,179], dimension 2–3601, and byte count must be exactly
``2*dimension*dimension``. The source copies/decodes once; caller bytes remain
owned by the caller. Replace tiles atomically, never mutate a worker's data.

The store has a 128 MiB sample budget, including retired data still held by
workers. ``get_memory_used()`` reports that accounting. ``remove_tile()``
releases the store's copy; old snapshots drain independently. Over-budget or
malformed replacements fail without losing the previous tile. The application
owns eviction and any larger disk cache; this source does not fetch or save.

Views emit demand through the source's ``tile-needed(south,west)`` signal,
once per missing tile across consumers. Return promptly, fetch asynchronously
and call ``put_tile`` on the creating/GTK thread. At most 1024 outstanding
request identities are retained. A failed fetch stays suppressed until the
application explicitly removes that tile to permit retry. There is no automatic
retry storm. Use a **new source instance** when changing provider/datum so late
responses belonging to the previous source cannot replace current data.

While attached, the view makes no built-in terrain network/cache reads,
regardless of the cache-enabled setting. Existing imagery providers remain
independent and can share the application's normal imagery station. Source
replacement cancels old native terrain work and rebuilds geometry from the
new source. Destroying one view does not clear another view's shared source.

``get_altitude(lat,lon)`` returns one double or raises an unavailable/invalid
coordinate error. This is convenient in SQGI; ``sample()`` also offers the
traditional C boolean-plus-output signature. Missing and entirely void data
are unknown, not zero elevation. The sentinel is -32768; isolated holes use
the existing bounded neighbouring-sample fallback. A scene may still draw its
flat visual fallback while terrain is absent: that is not a successful height
query or evidence that downloads are complete.

SQGI example
------------

The following uses already constructed ``view`` and ``window`` widgets on the
GTK thread. In a GTK 4 application use ``window.set_child(view)`` first; in GTK
3 use ``window.add(view)``. Rendered frames can be displayed in another widget.

.. code-block:: javascript

   local Scene = import("GWorldSceneGtk4", "0.1")
   local terrain = Scene.SceneTerrainSource.new()
   view.set_terrain_source(terrain)
   // Connect tile-needed to your existing asynchronous terrain/cache service.
   view.set_offscreen_enabled(true)
   window.realize()
   view.realize()
   view.set_camera_pose(-35.0, 149.0, 600.0, 1.0, 0.0, 0.0, 0.0)
   view.set_camera_projection(1920, 1080, 1000.0, 1000.0,
                              960.0, 540.0, 0.01, 100000.0)
   // Update retained model nodes to the same source timestamp before capture.
   local frame = view.capture_frame(1920, 1080, sourceTimeUs)
   local image = frame.get_image()
   // Share image/frame with preview and recording consumers.

Verification and limits
-----------------------

``camera`` tests independent transform/projection mathematics. Native
``camera-capture-gtk3/gtk4`` and their GLES variants check sub-metre occlusion,
off-centre projection, roll, projection zoom, letterboxing, immediate node
movement, distant depth ordering, source replacement, unmapped capture and
frame survival after destruction. ``terrain-source`` checks shared immutable
samples, retirement accounting, demand suppression, void handling and memory
limits. GIR and actual SQGI tests exercise both backend typelibs, errors,
scalar outputs, frame ownership and supplied elevation bytes. Existing render,
terrain, imagery, node and picking suites remain regression gates.

These tests establish API/render behavior on their executed platforms. They do
not establish application-specific optics, mount calibration, sustained camera
frame rate under an application's full workload, native Windows/ARM hardware
performance, or video encoding support. Benchmark the intended GPU and scene;
software GL/Xvfb correctness tests are not a hardware throughput qualification.
