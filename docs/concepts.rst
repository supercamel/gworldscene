Concepts
========

Scene view
----------

``GWorldSceneView`` is a GTK 4 widget derived from ``GtkGLArea``. Add it to a
normal GTK widget hierarchy and then configure the camera, map tiles, terrain,
lighting, and scene nodes.

The widget owns every node returned from an ``add_*`` method. Node pointers are
borrowed handles: keep them while you want to modify a node, but do not unref
them. Remove nodes with ``gworld_scene_view_remove_node()`` or clear them all
with ``gworld_scene_view_clear_nodes()``.

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
  Set the base RGB color used by primitives and compatibility paths.

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

The view supports fog, a directional sun, shadows, and shader-side terrain
normal smoothing.

``set_sun_position(azimuth, elevation)``
  Set the sun manually. Azimuth is clockwise from geographic north and
  elevation is degrees above the horizon.

``set_sun_time_of_day(hour)``
  Use an approximate local solar hour in ``[0, 24)``.

``set_fog_enabled()`` and ``set_fog_range(start, end)``
  Toggle and shape distance fog in metres.

``set_fog_color(red, green, blue)``
  Set fog color components in ``[0, 1]``.

``set_shadows_enabled()``
  Toggle directional shadows for local terrain and scene nodes.

``set_terrain_normal_smoothing()``
  Set shader-side terrain lighting normal smoothing in ``[0, 1]``.

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
