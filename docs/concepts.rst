Concepts
========

Scene view
----------

``GWorldSceneView`` is a GTK widget derived from ``GtkGLArea``. It is provided
by separate GTK 3 and GTK 4 libraries with the same C API surface. Add it to a
normal GTK widget hierarchy and then configure the camera, map tiles, terrain,
lighting, and scene nodes.

The widget owns every node returned from an ``add_*`` method. Node pointers are
borrowed handles: keep them while you want to modify a node, but do not unref
them. Remove nodes with ``gworld_scene_view_remove_node()`` or clear them all
with ``gworld_scene_view_clear_nodes()``.

.. _application-imagery:

Application-supplied imagery
----------------------------

Attach a ``GWorldSceneTileProvider`` with ``set_tile_provider()`` when the
application owns imagery transport, credentials, caching, or offline data.
This bypasses the view's native imagery HTTP and disk paths. Terrain and water
keep their independent settings. Passing ``NULL`` selects a neutral imagery
background; explicitly calling ``set_map_tile_url_template()`` restores URL
imagery. Hidden external views make no new requests and retire their imagery
references; mapping them again requests fresh coverage.

Create one provider per view on the GTK thread and call its public methods on
that thread. Deferred notifications run on its creating main context, which
must continue to be iterated until cleanup finishes. Connect handlers before
attaching the provider. ``tile-requested(id, zoom, x, y)`` asks for an immutable
RGB/RGBA, 8-bit ``GdkPixbuf`` of the provider's declared size, 256 or 512 pixels
square. Complete each live identity once, using ``complete_tile()`` or
``complete_annotated()``. ``NULL`` pixels mean unavailable; malformed dimensions
also mark the request unavailable. Stale, duplicate, and closed identities are
rejected. ``clear()`` retires the current requests and allows retry with new IDs.

Zoom limits are inclusive, from 0 through 22. Demand above the maximum uses
ancestors and reports reduced detail; demand below the minimum is unsupported.
``dup_demand()`` returns all accepted coordinates as a newly allocated,
null-terminated array of ``z/x/y`` strings, including queued requests.
``demand-changed(revision)`` announces changes before dispatching requests, so
the application can prepare regional metadata. The set is sorted numerically
by zoom, X, and Y; X wraps at the dateline.

``complete_annotated(id, image, source_zoom, annotation)`` can supply an ancestor
of the requested cell. Its pixels are sampled only inside that cell, even if
the source image covers a larger area. The source zoom must lie between the
provider minimum and the requested zoom. This permits regional resolution
limits without expanding the region for which the application supplied data.

Attribution is separate from pixel ownership. An annotation is an opaque,
case-sensitive ID of 1--128 printable ASCII bytes without spaces; ``NULL`` means
no annotation. Publish its credit in the application's UI before completing
the tile. Reuse an ID only for the same immutable credit record.
``tile-released(id)`` ends ownership of the original pixels, but derived atlas
pixels may still be visible. Keep the credit until
``annotation-released(id)``: atlas workers, uploads, active GPU textures, and the
last presented frame retain it independently. Successful replacement retires
presentation references after GTK paints; hiding or destroying the view also
retires them. Ongoing workers finish releasing asynchronously.

When retiring a provider, detach it, call ``close()``, and keep application
callback state alive until ``drained``. This signal can fire during ``close()``
when nothing remains, so connect first. Closed providers cannot be reopened.
Keep iterating the creating context while waiting for deferred releases.

The provider limits unfinished dispatched requests to 16 and retained request
records to 1024, including retired records still held by workers. Overflow and
unsupported counters expose incomplete coverage. Source pixels require CPU
memory in addition to the view's GPU texture budget: 1024 distinct 512-pixel
RGBA images alone occupy about 1 GiB. Applications should bound their own
transport and cache memory as well.

``get_imagery_ready()`` becomes true only when accepted coverage is complete
and the current requested atlases have reached active GPU textures, with no
overflow or unsupported demand. It can be true for reduced-detail ancestors.
It reports upload readiness; applications requiring a presented frame should
observe the frame clock's ``after-paint`` phase.

Coordinates
-----------

Positions use geodetic coordinates:

``latitude``
  Degrees north, clamped to ``[-90, 90]``.

``longitude``
  Degrees east, clamped to ``[-180, 180]``.

``altitude_amsl``
  Metres above mean sea level. This is the default altitude interpretation for
  cameras and most scene nodes.

Orientation uses the local NED frame:

``yaw``
  Heading in degrees clockwise from geographic north.

``pitch``
  Rotation around the local east/right axis.

``roll``
  Rotation around the local north/forward axis.

For imported models, the model is interpreted in NED-friendly axes: ``+Z`` is
north/forward, ``+X`` is east/right, and ``+Y`` is up. Model units are treated
as metres before node scale is applied.

Altitude modes
--------------

``GWORLD_SCENE_ALTITUDE_AMSL``
  Use the altitude value as metres above mean sea level.

``GWORLD_SCENE_ALTITUDE_AGL``
  Use the altitude value as metres above loaded terrain.

``GWORLD_SCENE_ALTITUDE_CLAMP_TO_GROUND``
  Ignore the altitude value and conform to loaded terrain.

AGL and clamp-to-ground objects depend on terrain data. While a terrain tile is
still loading, rendering uses the best available loaded terrain state.

Camera modes
------------

``GWORLD_SCENE_CAMERA_MODE_DEFAULT``
  The default Google-Earth-style camera. It blends from local terrain viewing
  into an orbit/globe presentation at high altitude.

``GWORLD_SCENE_CAMERA_MODE_FREE``
  The camera position is treated as the actual eye position at all altitudes.
  Use this for aircraft-like or free-flight camera control.

The default camera is controlled with ``set_camera()`` and
``set_camera_orientation()``. Free camera mode is controlled with
``set_free_camera_position()``, ``set_free_camera_orientation()``,
``set_free_camera_azimuth()``, and ``set_free_camera_pitch()``. Calling
``look_at_location()`` or ``look_at_node()`` rotates the current camera toward a
target and switches to free mode.

Scene nodes
-----------

All renderable objects inherit from ``GWorldSceneNode``. Common node operations
include:

``set_position()``
  Move to a latitude, longitude, and AMSL altitude.

``translate_ned()``
  Move by local north/east/down metre offsets.

``slew_position()``
  Set a target geodetic position for smooth movement.

``set_orientation_ned()`` and ``rotate_ned()``
  Control local yaw, pitch, and roll.

``set_scale()``
  Apply per-axis scale.

``set_color()``
  Set the base sRGB color used by primitives and compatibility paths.

Every node has a stable read-only ``id`` and emits ``changed`` whenever a
mutation affects rendering.

Renderable node types
---------------------

``SceneCubeNode``
  Box primitive. Dimensions are width, depth, and height in metres.

``SceneSphereNode``
  Sphere primitive. Size is diameter in metres.

``SceneCylinderNode``
  Cylinder primitive. Size is diameter and height in metres.

``SceneModelNode``
  Imported mesh loaded through Assimp. Common Assimp formats include GLB/glTF,
  OBJ, Collada, FBX, STL, and PLY, depending on the system Assimp build.

``SceneBillboardNode``
  Camera-facing image marker. Supports pixel size limits, reference size,
  maximum visible distance, and AMSL/AGL altitude modes.

``SceneGroundOverlayNode``
  Image draped over four geodetic corners in top-left, top-right,
  bottom-right, bottom-left order.

``ScenePolylineNode``
  Ordered geodetic line string with width, opacity, and altitude mode.

``ScenePolygonNode``
  Geodetic filled polygon with fill color, outline color, outline width, and
  altitude mode.

``SceneCircleNode``
  Geodetic circle with radius, segment count, fill color, outline color,
  outline width, and altitude mode.

``SceneTextLabelNode``
  Camera-facing text label with font, text/background colors, padding, pixel
  size limits, reference size, maximum visible distance, and altitude mode.

Lighting and atmosphere
-----------------------

The default appearance combines atmospheric scattering, a directional sun,
material lighting, filtered imagery, and smooth terrain lighting. Shadows,
reflective water, and additional distance fog are disabled by default.
Terrain normals follow the elevation data and Earth's curvature. Adjacent tiles
share elevation samples for their lighting gradients, independently of mesh LOD;
missing neighbours use the available slope instead of introducing a cliff.

Imagery uses trilinear mipmap filtering and up to 8x anisotropic filtering when
supported by the current GL or GLES context. The mip chain adds approximately
one third to imagery atlas storage. Transparent or missing tiles are filtered
with premultiplied linear colors to avoid dark borders.

Colors supplied through the API and color images are interpreted as sRGB.
Lighting, fog, imagery transitions, and transparency blending run in linear
light. A final display pass applies a gentle highlight roll-off and converts to
sRGB exactly once, accounting for GTK's framebuffer format. Imported model
material factors and vertex colors retain their linear interpretation.

The scene uses a floating-point color buffer when supported, with a linear
RGBA8 fallback on GLES devices without half-float color-buffer support. This
adds one fullscreen pass and a viewport-sized color/depth target (typically
12 bytes per framebuffer pixel with RGBA16F, or 8 with RGBA8).

``set_sun_position(azimuth, elevation)``
  Set the sun manually. Azimuth is clockwise from geographic north and
  elevation is degrees above the horizon.

``set_sun_time_of_day(hour)``
  Use an approximate local solar hour in ``[0, 24)``.

``set_atmosphere_enabled()``
  Toggle the shared sky and surface atmosphere. Enabled by default. Rayleigh
  and aerosol single scattering provide a curved horizon, distance haze,
  twilight, and a dark sky above the atmosphere. Disabling uses a gradient sky.

``set_atmosphere_density()`` and ``set_atmosphere_haze()``
  Adjust air density and aerosol scattering independently in ``[0, 4]``;
  both default to ``1``. The atmosphere uses eight integration samples per
  shaded pixel, increasing to sixteen for views from space. This is an approximation, without clouds or multiple scattering.

``set_fog_enabled()`` and ``set_fog_range(start, end)``
  Enable and shape additional distance fog in metres. Disabled by default to
  let the atmosphere supply distance haze. The fog color/range controls remain
  useful for stylized weather; disabling atmosphere does not enable fog.

``set_fog_color(red, green, blue)``
  Set sRGB fog color components in ``[0, 1]``.

``set_shadows_enabled()``
  Enable directional shadows for local terrain and scene nodes. Three
  overlapping cascades follow the camera, with texel snapping, normal-offset
  bias, and filtered edges. Shadows fade out beyond the covered distance
  (5--60 km depending on altitude and terrain coverage). Transparent cutouts
  use a 50% alpha threshold in the shadow pass. This adds three geometry passes,
  up to two 25-tap filters per shaded pixel, and about 48 MiB of depth textures.
  Disabling releases the shadow textures on the next frame.

``set_terrain_normal_smoothing()``
  Blend between triangle face normals (``0``) and smooth elevation-derived
  normals (``1``). The default is ``0.88``. Smoothing preserves terrain slopes.

Node materials
~~~~~~~~~~~~~~

Solids and imported models use roughness/metallic lighting with a GGX sun
highlight and a hemispherical environment approximation. On a node,
``set_roughness(value)`` and ``set_metallic(value)`` accept ``[0, 1]``; ``-1``
restores the imported material or primitive default (roughness ``0.65``,
metallic ``0``). Both overrides default to ``-1``. These settings affect lit
geometry, including lines and filled shapes; image billboards, text labels,
and ground overlays keep their image colors. Imported scalar roughness and
metallic factors are supported; normal maps and packed material textures are
not currently sampled.

Optional water
~~~~~~~~~~~~~~

``set_water_enabled(true)`` enables reflective water and automatic boundary
loading. The default source is OSM-derived `OpenFreeMap <https://openfreemap.org/>`_
vector tiles, using the `OpenMapTiles water schema
<https://openmaptiles.org/schema/#water>`_. No API key is required. Oceans,
lakes, and river polygons are rasterized at near, middle, and far ranges;
islands are preserved. Intermittent water and tunnels are excluded. Land is
never classified as water merely because its elevation is flat or missing.

Water uses Fresnel sky reflections, a sun highlight, and animated surface
normals while retaining the imagery's water tint. ``set_water_wave_strength()``
accepts ``[0, 2]`` and defaults to ``0.35``; zero gives still water and stops
animation-driven redraws. Reflections contain the sky and sun, not scene
objects. This surface shading follows the terrain mesh: it does not flatten
lake elevations, displace waves, simulate tides, or reconstruct bathymetry.
Map/imagery dates and shoreline alignment may differ.

Downloads run off the UI thread with four concurrent requests, bounded tile
sizes, cancellation, and retry backoff. Raw tiles live under
``<cache-directory>/water/<provider-hash>/<z>/<x>/<y>.pbf``. They are refreshed
when reloaded after seven days; valid stale tiles remain usable during an
outage. ``cache-enabled`` also controls water disk caching. Turning water off
cancels pending work and releases its GPU resources on the next frame.
Without available boundaries, the normal imagery remains visible.

Three mipmapped coverage atlases use up to about 32 MiB of GPU storage, in
addition to up to 24 MiB of decoded masks on the CPU. These allocations are
separate from the imagery texture budget. Animated water requires continuous
redrawing while loaded water is in the requested region. Surface reflections
also evaluate the atmosphere, so water has a higher fragment cost.

``set_water_tile_url_template(url)`` accepts an HTTP(S) MVT template with
``{z}``, ``{x}``, and ``{y}``, zooms 0--14, and the OpenMapTiles water layer.
Pass ``NULL`` to restore the default. Changing providers cancels previous
work and selects a separate cache namespace. Custom providers may need
additional attribution from the application. The widget displays clickable
OpenFreeMap, OpenMapTiles, and OpenStreetMap credits when using the default
source; the latter two remain for compatible custom sources.

Picking
-------

``GWorldSceneView`` emits signals for terrain and node picking:

``ground-clicked(latitude, longitude, altitude_amsl, button)``
  Single click on terrain.

``ground-double-clicked(latitude, longitude, altitude_amsl, button)``
  Double click on terrain.

``node-clicked(node, latitude, longitude, altitude_amsl, button)``
  Single click on a renderable scene node.

``node-double-clicked(node, latitude, longitude, altitude_amsl, button)``
  Double click on a renderable scene node.

The reported latitude, longitude, and altitude are geodetic coordinates for the
hit point.
