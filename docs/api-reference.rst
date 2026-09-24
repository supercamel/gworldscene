API Reference
=============

This page summarizes the public API exported by ``gworldscene.h`` and the
``GWorldSceneGtk3-0.1`` / ``GWorldSceneGtk4-0.1`` GIRs. C names are canonical.
Vala and SQGI names follow the generated introspection bindings.

The C symbol names are shared between backends. Choose exactly one GTK package
for an application: ``gworldscene-gtk3-0.1`` for GTK 3 or
``gworldscene-gtk4-0.1`` for GTK 4.

Namespace mapping
-----------------

.. list-table::
   :header-rows: 1

   * - Language
     - Import
     - Namespace
   * - C
     - ``pkg-config --libs gworldscene-gtk3-0.1`` or ``pkg-config --libs gworldscene-gtk4-0.1``
     - ``gworld_scene_*`` / ``GWorldScene*``
   * - Vala
     - ``--pkg GWorldSceneGtk3-0.1`` or ``--pkg GWorldSceneGtk4-0.1``
     - ``GWorld``
   * - SQGI
     - ``import("GWorldSceneGtk3", "0.1")`` or ``import("GWorldSceneGtk4", "0.1")``
     - ``GWorldSceneGtk3`` or ``GWorldSceneGtk4``

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
  ``GWorldSceneView`` derives from ``GtkGLArea`` in the selected GTK backend.

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
  ``gworld_scene_view_set_tile_provider()``,
  ``gworld_scene_view_get_imagery_ready()``,
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
  ``gworld_scene_view_set_atmosphere_enabled()``, ``gworld_scene_view_get_atmosphere_enabled()``,
  ``gworld_scene_view_set_atmosphere_density()``, ``gworld_scene_view_get_atmosphere_density()``,
  ``gworld_scene_view_set_atmosphere_haze()``, ``gworld_scene_view_get_atmosphere_haze()``,
  ``gworld_scene_view_set_water_enabled()``, ``gworld_scene_view_get_water_enabled()``,
  ``gworld_scene_view_set_water_wave_strength()``, ``gworld_scene_view_get_water_wave_strength()``,
  ``gworld_scene_view_set_water_tile_url_template()``, ``gworld_scene_view_get_water_tile_url_template()``,
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
  ``fog-enabled``, ``shadows-enabled``, ``atmosphere-enabled``,
  ``atmosphere-density``, ``atmosphere-haze``, ``water-enabled``,
  ``water-wave-strength``, ``water-tile-url-template``, and
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
  ``gworld_scene_node_get_color()``,
  ``gworld_scene_node_set_roughness()``, ``gworld_scene_node_get_roughness()``,
  ``gworld_scene_node_set_metallic()``, ``gworld_scene_node_get_metallic()``.

Compatibility helpers:
  ``gworld_scene_node_set_dimensions()``,
  ``gworld_scene_node_get_dimensions()``,
  ``gworld_scene_node_set_diameter()``,
  ``gworld_scene_node_get_model_path()``.

Properties:
  ``id`` and ``primitive`` are read-only. ``roughness`` and ``metallic``
  are read/write doubles in ``[-1, 1]``; ``-1`` inherits material defaults.

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

``GWorldSceneTileProvider``
---------------------------

An application imagery source in the shared core library, exposed by both GTK
namespaces. Attach it to one view with ``gworld_scene_view_set_tile_provider()``.
See :ref:`application-imagery` for threading, ownership, and resource limits.

Creation and lifetime:
  ``gworld_scene_tile_provider_new(minimum_zoom, maximum_zoom, tile_size)``,
  ``gworld_scene_tile_provider_clear()``, ``gworld_scene_tile_provider_close()``.

Completion and demand:
  ``gworld_scene_tile_provider_complete_tile()`` supplies exact-level pixels;
  ``gworld_scene_tile_provider_complete_annotated()`` also accepts regional
  ancestors and an opaque attribution ID. ``gworld_scene_tile_provider_dup_demand()``
  returns an owned string array; C callers release it with ``g_strfreev()``.

State getters (all prefixed ``gworld_scene_tile_provider_``):
  ``get_min_zoom()``, ``get_max_zoom()``, ``get_tile_size()``, ``get_tile_count()``,
  ``get_ready_count()``, ``get_failed_count()``, ``get_held_count()``,
  ``get_worker_count()``, ``get_annotation_count()``, ``get_overflow_count()``,
  ``get_unsupported_count()``, ``get_reduced_detail()``, ``get_closed()``.

Signals:
  ``tile-requested(request_id, zoom, x, y)``, ``tile-released(request_id)``,
  ``annotation-released(annotation)``, ``demand-changed(revision)``,
  ``coverage-changed()``, ``changed()``, ``drained()``.

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
  ``GWorldSceneGtk4.SceneView``, ``GWorldSceneGtk4.SceneNode``,
  ``GWorldSceneGtk4.SceneCubeNode``, ``GWorldSceneGtk4.SceneSphereNode``,
  ``GWorldSceneGtk4.SceneCylinderNode``, ``GWorldSceneGtk4.SceneModelNode``,
  ``GWorldSceneGtk4.SceneBillboardNode``,
  ``GWorldSceneGtk4.SceneGroundOverlayNode``,
  ``GWorldSceneGtk4.ScenePolylineNode``, ``GWorldSceneGtk4.ScenePolygonNode``,
  ``GWorldSceneGtk4.SceneCircleNode``, and
  ``GWorldSceneGtk4.SceneTextLabelNode``. Use ``GWorldSceneGtk3`` instead for
  GTK 3 builds.

Scalar output getters expose ``out`` parameters in C/Vala and multiple results
in SQGI. Both generated GIRs are checked for direction, ownership and optionality.
``SceneTileProvider`` is available alongside ``SceneView`` in each namespace;
the provider's nullable completions, demand arrays and signals are covered by
GIR checks and SQGI runtime tests.

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

``gworld-scene-tile-provider.h``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. literalinclude:: ../src/gworld-scene-tile-provider.h
   :language: c
