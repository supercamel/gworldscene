API Reference
=============

This page summarizes the public API exported by ``gworldscene.h`` and the
``GWorldScene-0.1`` GIR. C names are canonical. Vala and SQGI names follow the
generated introspection bindings.

Namespace mapping
-----------------

.. list-table::
   :header-rows: 1

   * - Language
     - Import
     - Namespace
   * - C
     - ``#include <gworldscene/gworldscene.h>``
     - ``gworld_scene_*`` / ``GWorldScene*``
   * - Vala
     - ``--pkg GWorldScene-0.1``
     - ``GWorld``
   * - SQGI
     - ``import("GWorldScene", "0.1")``
     - ``GWorldScene``

Enumerations and records
------------------------

``GWorldSceneNodeId``
  Stable unsigned 64-bit node identifier.

``GWorldSceneGeoPoint``
  Public struct containing ``latitude``, ``longitude``, and
  ``altitude_amsl``.

``GWorldSceneCameraMode``
  ``GWORLD_SCENE_CAMERA_MODE_DEFAULT`` and ``GWORLD_SCENE_CAMERA_MODE_FREE``.

``GWorldSceneAltitudeMode``
  ``GWORLD_SCENE_ALTITUDE_AMSL``, ``GWORLD_SCENE_ALTITUDE_AGL``, and
  ``GWORLD_SCENE_ALTITUDE_CLAMP_TO_GROUND``.

``GWorldScenePrimitive``
  ``CUBE``, ``SPHERE``, ``CYLINDER``, ``MODEL``, ``BILLBOARD``,
  ``GROUND_OVERLAY``, ``POLYLINE``, ``POLYGON``, ``CIRCLE``, and
  ``TEXT_LABEL``.

``GWorldSceneView``
-------------------

Inheritance:
  ``GWorldSceneView`` derives from ``GtkGLArea``.

Constructor:
  ``GtkWidget *gworld_scene_view_new(void)``

Camera:
  ``gworld_scene_view_set_camera()``, ``gworld_scene_view_get_camera()``,
  ``gworld_scene_view_set_camera_orientation()``,
  ``gworld_scene_view_get_camera_orientation()``,
  ``gworld_scene_view_set_camera_mode()``,
  ``gworld_scene_view_get_camera_mode()``,
  ``gworld_scene_view_set_free_camera_position()``,
  ``gworld_scene_view_get_free_camera_position()``,
  ``gworld_scene_view_set_free_camera_orientation()``,
  ``gworld_scene_view_get_free_camera_orientation()``,
  ``gworld_scene_view_set_free_camera_azimuth()``,
  ``gworld_scene_view_set_free_camera_pitch()``,
  ``gworld_scene_view_look_at_location()``,
  ``gworld_scene_view_look_at_node()``.

Terrain, map tiles, and cache:
  ``gworld_scene_view_set_terrain_server()``,
  ``gworld_scene_view_get_terrain_server()``,
  ``gworld_scene_view_set_map_tile_url_template()``,
  ``gworld_scene_view_get_map_tile_url_template()``,
  ``gworld_scene_view_set_cache_directory()``,
  ``gworld_scene_view_get_cache_directory()``,
  ``gworld_scene_view_set_cache_enabled()``,
  ``gworld_scene_view_get_cache_enabled()``,
  ``gworld_scene_view_sample_terrain_altitude()``.

Lighting and atmosphere:
  ``gworld_scene_view_set_sun_position()``,
  ``gworld_scene_view_get_sun_position()``,
  ``gworld_scene_view_set_sun_time_of_day()``,
  ``gworld_scene_view_get_sun_time_of_day()``,
  ``gworld_scene_view_set_fog_enabled()``,
  ``gworld_scene_view_get_fog_enabled()``,
  ``gworld_scene_view_set_fog_range()``,
  ``gworld_scene_view_get_fog_range()``,
  ``gworld_scene_view_set_fog_color()``,
  ``gworld_scene_view_get_fog_color()``,
  ``gworld_scene_view_set_shadows_enabled()``,
  ``gworld_scene_view_get_shadows_enabled()``,
  ``gworld_scene_view_set_terrain_normal_smoothing()``,
  ``gworld_scene_view_get_terrain_normal_smoothing()``.

Node creation and lifetime:
  ``gworld_scene_view_add_cube()``, ``gworld_scene_view_add_sphere()``,
  ``gworld_scene_view_add_cylinder()``, ``gworld_scene_view_add_model()``,
  ``gworld_scene_view_add_billboard()``,
  ``gworld_scene_view_add_ground_overlay()``,
  ``gworld_scene_view_add_polyline()``, ``gworld_scene_view_add_polygon()``,
  ``gworld_scene_view_add_circle()``, ``gworld_scene_view_add_text_label()``,
  ``gworld_scene_view_remove_node()``, ``gworld_scene_view_clear_nodes()``.

Properties:
  ``latitude``, ``longitude``, ``altitude-amsl``, ``terrain-server``,
  ``map-tile-url-template``, ``cache-directory``, ``cache-enabled``,
  ``sun-azimuth-deg``, ``sun-elevation-deg``, ``sun-time-of-day``,
  ``fog-enabled``, ``shadows-enabled``, and
  ``terrain-normal-smoothing``.

Signals:
  ``ground-clicked(double latitude, double longitude, double altitude_amsl, uint button)``,
  ``ground-double-clicked(double latitude, double longitude, double altitude_amsl, uint button)``,
  ``node-clicked(GWorldSceneNode *node, double latitude, double longitude, double altitude_amsl, uint button)``,
  ``node-double-clicked(GWorldSceneNode *node, double latitude, double longitude, double altitude_amsl, uint button)``.

``GWorldSceneNode``
-------------------

Inheritance:
  Abstract-ish public base type for all view-owned scene nodes. It derives
  from ``GObject`` and is derivable at the C ABI level.

Common methods:
  ``gworld_scene_node_get_id()``,
  ``gworld_scene_node_get_primitive()``,
  ``gworld_scene_node_set_position()``,
  ``gworld_scene_node_get_position()``,
  ``gworld_scene_node_translate_ned()``,
  ``gworld_scene_node_slew_position()``,
  ``gworld_scene_node_set_orientation_ned()``,
  ``gworld_scene_node_get_orientation_ned()``,
  ``gworld_scene_node_rotate_ned()``,
  ``gworld_scene_node_set_scale()``,
  ``gworld_scene_node_get_scale()``,
  ``gworld_scene_node_set_color()``,
  ``gworld_scene_node_get_color()``.

Compatibility helpers:
  ``gworld_scene_node_set_dimensions()``,
  ``gworld_scene_node_get_dimensions()``,
  ``gworld_scene_node_set_diameter()``,
  ``gworld_scene_node_get_model_path()``.

Properties:
  ``id`` and ``primitive`` are read-only.

Signals:
  ``changed()`` is emitted when a node mutation affects rendering.

``GWorldSceneCubeNode``
-----------------------

Creation:
  ``gworld_scene_view_add_cube()``

Methods:
  ``gworld_scene_cube_node_set_dimensions()``,
  ``gworld_scene_cube_node_get_dimensions()``.

``GWorldSceneSphereNode``
-------------------------

Creation:
  ``gworld_scene_view_add_sphere()``

Methods:
  ``gworld_scene_sphere_node_set_diameter()``,
  ``gworld_scene_sphere_node_get_diameter()``.

``GWorldSceneCylinderNode``
---------------------------

Creation:
  ``gworld_scene_view_add_cylinder()``

Methods:
  ``gworld_scene_cylinder_node_set_size()``,
  ``gworld_scene_cylinder_node_get_size()``.

``GWorldSceneModelNode``
------------------------

Creation:
  ``gworld_scene_view_add_model()``

Methods:
  ``gworld_scene_model_node_set_model_path()``,
  ``gworld_scene_model_node_get_model_path()``.

Models are loaded through Assimp. The exact import formats depend on the
installed Assimp build.

``GWorldSceneBillboardNode``
----------------------------

Creation:
  ``gworld_scene_view_add_billboard()``

Methods:
  ``gworld_scene_billboard_node_set_image_path()``,
  ``gworld_scene_billboard_node_get_image_path()``,
  ``gworld_scene_billboard_node_set_size_limits()``,
  ``gworld_scene_billboard_node_get_size_limits()``,
  ``gworld_scene_billboard_node_set_reference_size()``,
  ``gworld_scene_billboard_node_get_reference_size()``,
  ``gworld_scene_billboard_node_set_max_visible_distance()``,
  ``gworld_scene_billboard_node_get_max_visible_distance()``,
  ``gworld_scene_billboard_node_set_altitude_mode()``,
  ``gworld_scene_billboard_node_get_altitude_mode()``.

``GWorldSceneGroundOverlayNode``
--------------------------------

Creation:
  ``gworld_scene_view_add_ground_overlay()``

Methods:
  ``gworld_scene_ground_overlay_node_set_image_path()``,
  ``gworld_scene_ground_overlay_node_get_image_path()``,
  ``gworld_scene_ground_overlay_node_set_corners()``,
  ``gworld_scene_ground_overlay_node_get_corners()``,
  ``gworld_scene_ground_overlay_node_set_opacity()``,
  ``gworld_scene_ground_overlay_node_get_opacity()``,
  ``gworld_scene_ground_overlay_node_set_altitude_offset()``,
  ``gworld_scene_ground_overlay_node_get_altitude_offset()``.

``GWorldScenePolylineNode``
---------------------------

Creation:
  ``gworld_scene_view_add_polyline()``

Methods:
  ``gworld_scene_polyline_node_clear_points()``,
  ``gworld_scene_polyline_node_append_point()``,
  ``gworld_scene_polyline_node_set_points()``,
  ``gworld_scene_polyline_node_get_points()``,
  ``gworld_scene_polyline_node_set_width()``,
  ``gworld_scene_polyline_node_get_width()``,
  ``gworld_scene_polyline_node_set_opacity()``,
  ``gworld_scene_polyline_node_get_opacity()``,
  ``gworld_scene_polyline_node_set_altitude_mode()``,
  ``gworld_scene_polyline_node_get_altitude_mode()``.

``GWorldScenePolygonNode``
--------------------------

Creation:
  ``gworld_scene_view_add_polygon()``

Methods:
  ``gworld_scene_polygon_node_clear_points()``,
  ``gworld_scene_polygon_node_append_point()``,
  ``gworld_scene_polygon_node_set_points()``,
  ``gworld_scene_polygon_node_get_points()``,
  ``gworld_scene_polygon_node_set_fill_color()``,
  ``gworld_scene_polygon_node_get_fill_color()``,
  ``gworld_scene_polygon_node_set_outline_color()``,
  ``gworld_scene_polygon_node_get_outline_color()``,
  ``gworld_scene_polygon_node_set_outline_width()``,
  ``gworld_scene_polygon_node_get_outline_width()``,
  ``gworld_scene_polygon_node_set_altitude_mode()``,
  ``gworld_scene_polygon_node_get_altitude_mode()``.

``GWorldSceneCircleNode``
-------------------------

Creation:
  ``gworld_scene_view_add_circle()``

Methods:
  ``gworld_scene_circle_node_set_radius()``,
  ``gworld_scene_circle_node_get_radius()``,
  ``gworld_scene_circle_node_set_segments()``,
  ``gworld_scene_circle_node_get_segments()``,
  ``gworld_scene_circle_node_set_fill_color()``,
  ``gworld_scene_circle_node_get_fill_color()``,
  ``gworld_scene_circle_node_set_outline_color()``,
  ``gworld_scene_circle_node_get_outline_color()``,
  ``gworld_scene_circle_node_set_outline_width()``,
  ``gworld_scene_circle_node_get_outline_width()``,
  ``gworld_scene_circle_node_set_altitude_mode()``,
  ``gworld_scene_circle_node_get_altitude_mode()``.

``GWorldSceneTextLabelNode``
----------------------------

Creation:
  ``gworld_scene_view_add_text_label()``

Methods:
  ``gworld_scene_text_label_node_set_text()``,
  ``gworld_scene_text_label_node_get_text()``,
  ``gworld_scene_text_label_node_set_font()``,
  ``gworld_scene_text_label_node_get_font()``,
  ``gworld_scene_text_label_node_set_text_color()``,
  ``gworld_scene_text_label_node_get_text_color()``,
  ``gworld_scene_text_label_node_set_background_color()``,
  ``gworld_scene_text_label_node_get_background_color()``,
  ``gworld_scene_text_label_node_set_padding()``,
  ``gworld_scene_text_label_node_get_padding()``,
  ``gworld_scene_text_label_node_set_size_limits()``,
  ``gworld_scene_text_label_node_get_size_limits()``,
  ``gworld_scene_text_label_node_set_reference_size()``,
  ``gworld_scene_text_label_node_get_reference_size()``,
  ``gworld_scene_text_label_node_set_max_visible_distance()``,
  ``gworld_scene_text_label_node_get_max_visible_distance()``,
  ``gworld_scene_text_label_node_set_altitude_mode()``,
  ``gworld_scene_text_label_node_get_altitude_mode()``.

Binding notes
-------------

Vala classes:
  ``GWorld.SceneView``, ``GWorld.SceneNode``,
  ``GWorld.SceneCubeNode``, ``GWorld.SceneSphereNode``,
  ``GWorld.SceneCylinderNode``, ``GWorld.SceneModelNode``,
  ``GWorld.SceneBillboardNode``, ``GWorld.SceneGroundOverlayNode``,
  ``GWorld.ScenePolylineNode``, ``GWorld.ScenePolygonNode``,
  ``GWorld.SceneCircleNode``, and ``GWorld.SceneTextLabelNode``.

SQGI classes:
  ``GWorldScene.SceneView``, ``GWorldScene.SceneNode``,
  ``GWorldScene.SceneCubeNode``, ``GWorldScene.SceneSphereNode``,
  ``GWorldScene.SceneCylinderNode``, ``GWorldScene.SceneModelNode``,
  ``GWorldScene.SceneBillboardNode``,
  ``GWorldScene.SceneGroundOverlayNode``,
  ``GWorldScene.ScenePolylineNode``, ``GWorldScene.ScenePolygonNode``,
  ``GWorldScene.SceneCircleNode``, and
  ``GWorldScene.SceneTextLabelNode``.

Some generated Vala getters for multiple ``double*`` outputs currently lack
``out`` annotations in the generated VAPI. Prefer the setters in new examples,
or use the C API directly for exhaustive getter tests until the annotations are
tightened.

Full public C declarations
--------------------------

The prose reference above is generated from the same public surface as the GIR.
The complete C declarations are included here as the final source of truth.

``gworldscene.h``
~~~~~~~~~~~~~~~~~

.. literalinclude:: ../src/gworldscene.h
   :language: c

``gworld-scene-view.h``
~~~~~~~~~~~~~~~~~~~~~~~

.. literalinclude:: ../src/gworld-scene-view.h
   :language: c

``gworld-scene-node.h``
~~~~~~~~~~~~~~~~~~~~~~~

.. literalinclude:: ../src/gworld-scene-node.h
   :language: c
