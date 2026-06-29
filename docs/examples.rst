Examples
========

Minimal application
-------------------

The snippets below use GTK 4 APIs and the GTK 4 package names. For GTK 3,
link ``gworldscene-gtk3-0.1`` / ``GWorldSceneGtk3-0.1`` and use GTK 3 window
packing such as ``gtk_container_add()`` and ``gtk_widget_show_all()``.

C
~

.. code-block:: c

   #include <gtk/gtk.h>
   #include <gworldscene/gworldscene.h>

   static void
   activate(GtkApplication *app)
   {
     GtkWidget *window = gtk_application_window_new(app);
     gtk_window_set_title(GTK_WINDOW(window), "GWorldScene C");
     gtk_window_set_default_size(GTK_WINDOW(window), 1100, 760);

     GWorldSceneView *view = GWORLD_SCENE_VIEW(gworld_scene_view_new());
     gworld_scene_view_set_camera(view, -16.8878, 145.7048, 7800.0);
     gworld_scene_view_set_camera_orientation(view, 72.0, -66.0);

     gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(view));
     gtk_window_present(GTK_WINDOW(window));
   }

   int
   main(int argc, char **argv)
   {
     GtkApplication *app = gtk_application_new("org.example.GWorldSceneC", 0);
     g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
     int status = g_application_run(G_APPLICATION(app), argc, argv);
     g_object_unref(app);
     return status;
   }

Vala
~~~~

.. code-block:: vala

   using Gtk;

   public class DemoWindow : Gtk.ApplicationWindow {
     public DemoWindow(Gtk.Application app) {
       Object(application: app, title: "GWorldScene Vala",
              default_width: 1100, default_height: 760);

       var view = new GWorld.SceneView();
       view.set_camera(-16.8878, 145.7048, 7800.0);
       view.set_camera_orientation(72.0, -66.0);
       child = view;
     }
   }

   int main(string[] args) {
     var app = new Gtk.Application("org.example.GWorldSceneVala", 0);
     app.activate.connect(() => new DemoWindow(app).present());
     return app.run(args);
   }

SQGI
~~~~

.. code-block:: text

   local Gio = import("Gio")
   local Gtk = import("Gtk", "4.0")
   local GWorldScene = import("GWorldSceneGtk4", "0.1")

   local app = null
   local W = { window = null, view = null }

   app = Gtk.Application.new("org.example.GWorldSceneSQGI",
                             Gio.ApplicationFlags.flags_none)

   app.connect("activate", function() {
     W.window = Gtk.ApplicationWindow.new(app)
     W.window.set_title("GWorldScene SQGI")
     W.window.set_default_size(1100, 760)

     W.view = GWorldScene.SceneView.new()
     W.view.set_camera(-16.8878, 145.7048, 7800.0)
     W.view.set_camera_orientation(72.0, -66.0)

     W.window.set_child(W.view)
     W.window.present()
   })

   local argv = ["gworldscene-sqgi"]
   foreach (arg in vargv) argv.append(arg)
   app.run(argv.len(), argv)

Add primitives
--------------

C
~

.. code-block:: c

   GWorldSceneCubeNode *cube =
     gworld_scene_view_add_cube(view, -16.8878, 145.7048, 650.0,
                                900.0, 900.0, 900.0);
   gworld_scene_node_set_color(GWORLD_SCENE_NODE(cube), 1.0, 0.08, 0.02);
   gworld_scene_node_set_orientation_ned(GWORLD_SCENE_NODE(cube),
                                         35.0, 0.0, 12.0);

   GWorldSceneSphereNode *sphere =
     gworld_scene_view_add_sphere(view, -16.8780, 145.7200, 450.0, 360.0);
   gworld_scene_node_set_color(GWORLD_SCENE_NODE(sphere), 0.18, 0.55, 0.95);

   GWorldSceneCylinderNode *cylinder =
     gworld_scene_view_add_cylinder(view, -16.9040, 145.6880, 420.0,
                                    320.0, 700.0);
   gworld_scene_node_set_color(GWORLD_SCENE_NODE(cylinder), 0.40, 0.78, 0.30);

Vala
~~~~

.. code-block:: vala

   var cube = view.add_cube(-16.8878, 145.7048, 650.0,
                            900.0, 900.0, 900.0);
   cube.set_color(1.0, 0.08, 0.02);
   cube.set_orientation_ned(35.0, 0.0, 12.0);

   var sphere = view.add_sphere(-16.8780, 145.7200, 450.0, 360.0);
   sphere.set_color(0.18, 0.55, 0.95);

   var cylinder = view.add_cylinder(-16.9040, 145.6880, 420.0, 320.0, 700.0);
   cylinder.set_color(0.40, 0.78, 0.30);

SQGI
~~~~

.. code-block:: text

   local cube = view.add_cube(-16.8878, 145.7048, 650.0,
                              900.0, 900.0, 900.0)
   cube.set_color(1.0, 0.08, 0.02)
   cube.set_orientation_ned(35.0, 0.0, 12.0)

   local sphere = view.add_sphere(-16.8780, 145.7200, 450.0, 360.0)
   sphere.set_color(0.18, 0.55, 0.95)

   local cylinder = view.add_cylinder(-16.9040, 145.6880, 420.0, 320.0, 700.0)
   cylinder.set_color(0.40, 0.78, 0.30)

Load a model
------------

C
~

.. code-block:: c

   GWorldSceneModelNode *model =
     gworld_scene_view_add_model(view, "examples/assets/ToyCar/ToyCar.glb",
                                 -16.8910, 145.7040, 650.0);
   gworld_scene_node_set_scale(GWORLD_SCENE_NODE(model),
                               12000.0, 12000.0, 12000.0);
   gworld_scene_node_set_orientation_ned(GWORLD_SCENE_NODE(model),
                                         120.0, 0.0, 0.0);

Vala
~~~~

.. code-block:: vala

   var model = view.add_model("examples/assets/ToyCar/ToyCar.glb",
                              -16.8910, 145.7040, 650.0);
   model.set_scale(12000.0, 12000.0, 12000.0);
   model.set_orientation_ned(120.0, 0.0, 0.0);

SQGI
~~~~

.. code-block:: text

   local model = view.add_model("examples/assets/ToyCar/ToyCar.glb",
                                -16.8910, 145.7040, 650.0)
   model.set_scale(12000.0, 12000.0, 12000.0)
   model.set_orientation_ned(120.0, 0.0, 0.0)

Billboards and ground overlays
------------------------------

C
~

.. code-block:: c

   GWorldSceneBillboardNode *marker =
     gworld_scene_view_add_billboard(view, "marker.png", -16.8840, 145.7510, 500.0);
   gworld_scene_billboard_node_set_altitude_mode(marker, GWORLD_SCENE_ALTITUDE_AGL);
   gworld_scene_billboard_node_set_size_limits(marker, 28.0, 96.0);
   gworld_scene_billboard_node_set_reference_size(marker, 64.0, 1800.0);

   GWorldSceneGroundOverlayNode *overlay =
     gworld_scene_view_add_ground_overlay(view, "overlay.png",
                                          -16.8890, 145.7460,
                                          -16.8890, 145.7545,
                                          -16.8950, 145.7545,
                                          -16.8950, 145.7460);
   gworld_scene_ground_overlay_node_set_opacity(overlay, 0.82);
   gworld_scene_ground_overlay_node_set_altitude_offset(overlay, 5.0);

Vala
~~~~

.. code-block:: vala

   var marker = view.add_billboard("marker.png", -16.8840, 145.7510, 500.0);
   marker.set_altitude_mode(GWorld.SceneAltitudeMode.AGL);
   marker.set_size_limits(28.0, 96.0);
   marker.set_reference_size(64.0, 1800.0);

   var overlay = view.add_ground_overlay("overlay.png",
                                         -16.8890, 145.7460,
                                         -16.8890, 145.7545,
                                         -16.8950, 145.7545,
                                         -16.8950, 145.7460);
   overlay.set_opacity(0.82);
   overlay.set_altitude_offset(5.0);

SQGI
~~~~

.. code-block:: text

   local marker = view.add_billboard("marker.png", -16.8840, 145.7510, 500.0)
   marker.set_altitude_mode(GWorldScene.SceneAltitudeMode.agl)
   marker.set_size_limits(28.0, 96.0)
   marker.set_reference_size(64.0, 1800.0)

   local overlay = view.add_ground_overlay("overlay.png",
                                           -16.8890, 145.7460,
                                           -16.8890, 145.7545,
                                           -16.8950, 145.7545,
                                           -16.8950, 145.7460)
   overlay.set_opacity(0.82)
   overlay.set_altitude_offset(5.0)

Lines, polygons, circles, and labels
------------------------------------

C
~

.. code-block:: c

   GWorldScenePolylineNode *route = gworld_scene_view_add_polyline(view);
   gworld_scene_polyline_node_set_altitude_mode(route, GWORLD_SCENE_ALTITUDE_AGL);
   gworld_scene_polyline_node_set_width(route, 35.0);
   gworld_scene_polyline_node_append_point(route, -16.8290, 145.6460, 90.0);
   gworld_scene_polyline_node_append_point(route, -16.8500, 145.6755, 120.0);

   GWorldScenePolygonNode *area = gworld_scene_view_add_polygon(view);
   gworld_scene_polygon_node_set_altitude_mode(area, GWORLD_SCENE_ALTITUDE_AGL);
   gworld_scene_polygon_node_set_fill_color(area, 0.95, 0.25, 0.85, 0.25);
   gworld_scene_polygon_node_append_point(area, -16.8650, 145.6795, 1.5);
   gworld_scene_polygon_node_append_point(area, -16.8505, 145.7040, 1.5);
   gworld_scene_polygon_node_append_point(area, -16.8845, 145.7310, 1.5);

   GWorldSceneCircleNode *circle =
     gworld_scene_view_add_circle(view, -16.8280, 145.6530, 1.5, 520.0);
   gworld_scene_circle_node_set_altitude_mode(circle, GWORLD_SCENE_ALTITUDE_AGL);

   GWorldSceneTextLabelNode *label =
     gworld_scene_view_add_text_label(view, "Depot", -16.8280, 145.6530, 220.0);
   gworld_scene_text_label_node_set_size_limits(label, 20.0, 84.0);

Vala
~~~~

.. code-block:: vala

   var route = view.add_polyline();
   route.set_altitude_mode(GWorld.SceneAltitudeMode.AGL);
   route.set_width(35.0);
   route.append_point(-16.8290, 145.6460, 90.0);
   route.append_point(-16.8500, 145.6755, 120.0);

   var area = view.add_polygon();
   area.set_altitude_mode(GWorld.SceneAltitudeMode.AGL);
   area.set_fill_color(0.95, 0.25, 0.85, 0.25);
   area.append_point(-16.8650, 145.6795, 1.5);
   area.append_point(-16.8505, 145.7040, 1.5);
   area.append_point(-16.8845, 145.7310, 1.5);

   var circle = view.add_circle(-16.8280, 145.6530, 1.5, 520.0);
   circle.set_altitude_mode(GWorld.SceneAltitudeMode.AGL);

   var label = view.add_text_label("Depot", -16.8280, 145.6530, 220.0);
   label.set_size_limits(20.0, 84.0);

SQGI
~~~~

.. code-block:: text

   local route = view.add_polyline()
   route.set_altitude_mode(GWorldScene.SceneAltitudeMode.agl)
   route.set_width(35.0)
   route.append_point(-16.8290, 145.6460, 90.0)
   route.append_point(-16.8500, 145.6755, 120.0)

   local area = view.add_polygon()
   area.set_altitude_mode(GWorldScene.SceneAltitudeMode.agl)
   area.set_fill_color(0.95, 0.25, 0.85, 0.25)
   area.append_point(-16.8650, 145.6795, 1.5)
   area.append_point(-16.8505, 145.7040, 1.5)
   area.append_point(-16.8845, 145.7310, 1.5)

   local circle = view.add_circle(-16.8280, 145.6530, 1.5, 520.0)
   circle.set_altitude_mode(GWorldScene.SceneAltitudeMode.agl)

   local label = view.add_text_label("Depot", -16.8280, 145.6530, 220.0)
   label.set_size_limits(20.0, 84.0)

Lighting and atmosphere
-----------------------

C
~

.. code-block:: c

   gworld_scene_view_set_sun_time_of_day(view, 15.25);
   gworld_scene_view_set_fog_enabled(view, TRUE);
   gworld_scene_view_set_fog_range(view, 9000.0, 90000.0);
   gworld_scene_view_set_fog_color(view, 0.60, 0.72, 0.86);
   gworld_scene_view_set_shadows_enabled(view, TRUE);
   gworld_scene_view_set_terrain_normal_smoothing(view, 0.92);

Vala
~~~~

.. code-block:: vala

   view.set_sun_time_of_day(15.25);
   view.set_fog_enabled(true);
   view.set_fog_range(9000.0, 90000.0);
   view.set_fog_color(0.60, 0.72, 0.86);
   view.set_shadows_enabled(true);
   view.set_terrain_normal_smoothing(0.92);

SQGI
~~~~

.. code-block:: text

   view.set_sun_time_of_day(15.25)
   view.set_fog_enabled(true)
   view.set_fog_range(9000.0, 90000.0)
   view.set_fog_color(0.60, 0.72, 0.86)
   view.set_shadows_enabled(true)
   view.set_terrain_normal_smoothing(0.92)

Picking
-------

C
~

.. code-block:: c

   static void
   on_node_clicked(GWorldSceneView *view,
                   GWorldSceneNode *node,
                   double latitude,
                   double longitude,
                   double altitude_amsl,
                   guint button,
                   gpointer user_data)
   {
     g_print("node %" G_GUINT64_FORMAT " at %.6f %.6f %.1f, button %u\n",
             gworld_scene_node_get_id(node),
             latitude,
             longitude,
             altitude_amsl,
             button);
   }

   g_signal_connect(view, "node-clicked", G_CALLBACK(on_node_clicked), NULL);

Vala
~~~~

.. code-block:: vala

   view.node_clicked.connect((node, latitude, longitude, altitude_amsl, button) => {
     stdout.printf("node %llu at %.6f %.6f %.1f, button %u\n",
                   node.id, latitude, longitude, altitude_amsl, button);
   });

SQGI
~~~~

.. code-block:: text

   view.connect("node-clicked", function(node, latitude, longitude, altitude_amsl, button) {
     print("node " + node.get_id() + " at " + latitude + " " +
           longitude + " " + altitude_amsl + ", button " + button + "\n")
   })
