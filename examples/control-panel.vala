using Gtk;

namespace GWorldSceneControls {
  private const double HOME_LATITUDE = -35.0024;
  private const double HOME_LONGITUDE = 147.4648;
  private const double HOME_ALTITUDE_AMSL = 5200.0;
  private const string DEFAULT_TERRAIN_SERVER = "https://flightops.silvertone.com.au/terrain/data/";
  private const string GOOGLE_LEGACY_SATELLITE =
    "https://mt.google.com/vt/lyrs=s&x={x}&y={y}&z={z}";
  private const string OSM_STANDARD =
    "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
  private const string OPENTOPOMAP =
    "https://a.tile.opentopomap.org/{z}/{x}/{y}.png";
  private const string MAPTILER_SATELLITE =
    "https://api.maptiler.com/tiles/satellite-v2/{z}/{x}/{y}.jpg?key={key}";
  private const string THUNDERFOREST_OUTDOORS =
    "https://tile.thunderforest.com/outdoors/{z}/{x}/{y}.png?apikey={key}";

  public class ControlPanelWindow : Gtk.ApplicationWindow {
    private GWorld.SceneView scene;
    private Gtk.TextView pick_terminal;
    private Gtk.TextBuffer pick_terminal_buffer;
    private uint pick_log_count;
    private Gtk.ComboBoxText map_provider;
    private Gtk.Entry api_key_entry;
    private Gtk.Entry map_template_entry;
    private Gtk.Label map_status;
    private Gtk.ComboBoxText terrain_provider;
    private Gtk.Entry terrain_entry;
    private Gtk.Label terrain_status;
    private Gtk.CheckButton cache_enabled;
    private Gtk.CheckButton fog_enabled;
    private Gtk.Scale fog_density;
    private Gtk.ComboBoxText fog_color;
    private Gtk.Label fog_status;
    private Gtk.CheckButton manual_sun;
    private Gtk.Scale sun_time;
    private Gtk.Scale sun_azimuth;
    private Gtk.Scale sun_elevation;
    private Gtk.Label sun_status;
    private Gtk.CheckButton shadows_enabled;
    private Gtk.Scale normal_smoothing;

    public ControlPanelWindow (Gtk.Application app) {
      Object (
        application: app,
        title: "GWorldScene Vala Controls",
        default_width: 1360,
        default_height: 840
      );

      scene = new GWorld.SceneView ();
      scene.hexpand = true;
      scene.vexpand = true;
      scene.set_camera (HOME_LATITUDE, HOME_LONGITUDE, HOME_ALTITUDE_AMSL);
      scene.set_camera_orientation (35.0, -32.0);

      seed_scene_nodes ();

      var paned = new Gtk.Paned (Gtk.Orientation.HORIZONTAL);
      var scene_column = new Gtk.Box (Gtk.Orientation.VERTICAL, 0);
      scene_column.append (scene);
      scene_column.append (build_pick_terminal ());
      paned.start_child = scene_column;
      paned.resize_start_child = true;
      paned.shrink_start_child = false;

      var controls = build_controls ();
      var scroll = new Gtk.ScrolledWindow ();
      scroll.set_policy (Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC);
      scroll.min_content_width = 360;
      scroll.child = controls;
      paned.end_child = scroll;
      paned.position = 960;

      child = paned;

      apply_map_provider ();
      apply_terrain_provider ();
      apply_cache ();
      apply_fog ();
      apply_sun ();
      apply_lighting ();
      connect_pick_logging ();
    }

    private Gtk.Widget build_pick_terminal () {
      pick_terminal_buffer = new Gtk.TextBuffer (null);
      pick_terminal = new Gtk.TextView.with_buffer (pick_terminal_buffer);
      pick_terminal.editable = false;
      pick_terminal.cursor_visible = false;
      pick_terminal.monospace = true;
      pick_terminal.wrap_mode = Gtk.WrapMode.WORD_CHAR;
      pick_terminal.left_margin = 10;
      pick_terminal.right_margin = 10;
      pick_terminal.top_margin = 8;
      pick_terminal.bottom_margin = 8;

      var scroll = new Gtk.ScrolledWindow ();
      scroll.set_policy (Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC);
      scroll.height_request = 150;
      scroll.hexpand = true;
      scroll.vexpand = false;
      scroll.child = pick_terminal;

      append_pick_log ("Pick log ready. Click terrain, overlays, billboards, or scene nodes.");
      return scroll;
    }

    private Gtk.Widget build_controls () {
      var box = new Gtk.Box (Gtk.Orientation.VERTICAL, 14);
      box.margin_top = 14;
      box.margin_bottom = 14;
      box.margin_start = 14;
      box.margin_end = 14;

      box.append (section_label ("Map"));
      var map_grid = new Gtk.Grid ();
      map_grid.row_spacing = 8;
      map_grid.column_spacing = 10;
      int row = 0;

      map_provider = new Gtk.ComboBoxText ();
      map_provider.append ("google-legacy", "Google satellite (legacy)");
      map_provider.append ("osm", "OpenStreetMap");
      map_provider.append ("opentopomap", "OpenTopoMap");
      map_provider.append ("maptiler-satellite", "MapTiler satellite");
      map_provider.append ("thunderforest-outdoors", "Thunderforest outdoors");
      map_provider.append ("custom", "Custom template");
      map_provider.active_id = "google-legacy";
      map_provider.changed.connect (() => update_map_field_sensitivity ());
      add_row (map_grid, row++, "Provider", map_provider);

      api_key_entry = new Gtk.Entry ();
      api_key_entry.placeholder_text = "API key for keyed providers";
      add_row (map_grid, row++, "API key", api_key_entry);

      map_template_entry = new Gtk.Entry ();
      map_template_entry.placeholder_text = "https://server/{z}/{x}/{y}.png";
      map_template_entry.text = GOOGLE_LEGACY_SATELLITE;
      add_row (map_grid, row++, "Template", map_template_entry);

      var apply_map = new Gtk.Button.with_label ("Apply map");
      apply_map.clicked.connect (() => apply_map_provider ());
      add_row (map_grid, row++, "", apply_map);

      map_status = status_label ();
      add_row (map_grid, row++, "", map_status);
      box.append (map_grid);

      box.append (section_label ("Terrain"));
      var terrain_grid = new Gtk.Grid ();
      terrain_grid.row_spacing = 8;
      terrain_grid.column_spacing = 10;
      row = 0;

      terrain_provider = new Gtk.ComboBoxText ();
      terrain_provider.append ("silvertone", "Silvertone HGT");
      terrain_provider.append ("custom", "Custom terrain server");
      terrain_provider.active_id = "silvertone";
      terrain_provider.changed.connect (() => update_terrain_field_sensitivity ());
      add_row (terrain_grid, row++, "Provider", terrain_provider);

      terrain_entry = new Gtk.Entry ();
      terrain_entry.text = DEFAULT_TERRAIN_SERVER;
      terrain_entry.placeholder_text = "Base URL or template containing {tile}";
      add_row (terrain_grid, row++, "Server", terrain_entry);

      var apply_terrain = new Gtk.Button.with_label ("Apply terrain");
      apply_terrain.clicked.connect (() => apply_terrain_provider ());
      add_row (terrain_grid, row++, "", apply_terrain);

      cache_enabled = new Gtk.CheckButton.with_label ("Cache terrain and map tiles");
      cache_enabled.active = true;
      cache_enabled.toggled.connect (() => apply_cache ());
      add_row (terrain_grid, row++, "", cache_enabled);

      terrain_status = status_label ();
      add_row (terrain_grid, row++, "", terrain_status);
      box.append (terrain_grid);

      box.append (section_label ("Atmosphere"));
      var fog_grid = new Gtk.Grid ();
      fog_grid.row_spacing = 8;
      fog_grid.column_spacing = 10;
      row = 0;

      fog_enabled = new Gtk.CheckButton.with_label ("Enable fog");
      fog_enabled.active = true;
      fog_enabled.toggled.connect (() => apply_fog ());
      add_row (fog_grid, row++, "", fog_enabled);

      fog_density = new Gtk.Scale.with_range (Gtk.Orientation.HORIZONTAL, 0.0, 1.0, 0.01);
      fog_density.set_value (0.38);
      fog_density.hexpand = true;
      fog_density.draw_value = false;
      fog_density.value_changed.connect (() => apply_fog ());
      add_row (fog_grid, row++, "Density", fog_density);

      fog_color = new Gtk.ComboBoxText ();
      fog_color.append ("sky", "Sky blue");
      fog_color.append ("haze", "White haze");
      fog_color.append ("dusk", "Dusk violet");
      fog_color.append ("dust", "Dust");
      fog_color.active_id = "sky";
      fog_color.changed.connect (() => apply_fog ());
      add_row (fog_grid, row++, "Color", fog_color);

      fog_status = status_label ();
      add_row (fog_grid, row++, "", fog_status);
      box.append (fog_grid);

      box.append (section_label ("Sun"));
      var sun_grid = new Gtk.Grid ();
      sun_grid.row_spacing = 8;
      sun_grid.column_spacing = 10;
      row = 0;

      manual_sun = new Gtk.CheckButton.with_label ("Manual sun position");
      manual_sun.toggled.connect (() => {
        update_sun_field_sensitivity ();
        apply_sun ();
      });
      add_row (sun_grid, row++, "", manual_sun);

      sun_time = new Gtk.Scale.with_range (Gtk.Orientation.HORIZONTAL, 0.0, 24.0, 0.25);
      sun_time.set_value (15.25);
      sun_time.hexpand = true;
      sun_time.draw_value = false;
      sun_time.value_changed.connect (() => apply_sun ());
      add_row (sun_grid, row++, "Time", sun_time);

      sun_azimuth = new Gtk.Scale.with_range (Gtk.Orientation.HORIZONTAL, 0.0, 360.0, 1.0);
      sun_azimuth.set_value (228.0);
      sun_azimuth.hexpand = true;
      sun_azimuth.draw_value = false;
      sun_azimuth.value_changed.connect (() => apply_sun ());
      add_row (sun_grid, row++, "Azimuth", sun_azimuth);

      sun_elevation = new Gtk.Scale.with_range (Gtk.Orientation.HORIZONTAL, -10.0, 90.0, 1.0);
      sun_elevation.set_value (50.0);
      sun_elevation.hexpand = true;
      sun_elevation.draw_value = false;
      sun_elevation.value_changed.connect (() => apply_sun ());
      add_row (sun_grid, row++, "Elevation", sun_elevation);

      shadows_enabled = new Gtk.CheckButton.with_label ("Enable shadows");
      shadows_enabled.active = true;
      shadows_enabled.toggled.connect (() => apply_lighting ());
      add_row (sun_grid, row++, "", shadows_enabled);

      sun_status = status_label ();
      add_row (sun_grid, row++, "", sun_status);
      box.append (sun_grid);

      box.append (section_label ("Terrain Lighting"));
      var terrain_lighting_grid = new Gtk.Grid ();
      terrain_lighting_grid.row_spacing = 8;
      terrain_lighting_grid.column_spacing = 10;
      row = 0;

      normal_smoothing = new Gtk.Scale.with_range (Gtk.Orientation.HORIZONTAL, 0.0, 1.0, 0.01);
      normal_smoothing.set_value (0.92);
      normal_smoothing.hexpand = true;
      normal_smoothing.draw_value = false;
      normal_smoothing.value_changed.connect (() => apply_lighting ());
      add_row (terrain_lighting_grid, row++, "Smoothing", normal_smoothing);
      box.append (terrain_lighting_grid);

      box.append (section_label ("Camera"));
      var camera_grid = new Gtk.Grid ();
      camera_grid.row_spacing = 8;
      camera_grid.column_spacing = 10;
      row = 0;
      var reset_camera = new Gtk.Button.with_label ("Reset camera");
      reset_camera.clicked.connect (() => {
        scene.set_camera (HOME_LATITUDE, HOME_LONGITUDE, HOME_ALTITUDE_AMSL);
        scene.set_camera_orientation (35.0, -32.0);
      });
      add_row (camera_grid, row++, "", reset_camera);
      box.append (camera_grid);

      update_map_field_sensitivity ();
      update_terrain_field_sensitivity ();
      update_sun_field_sensitivity ();

      return box;
    }

    private Gtk.Label section_label (string text) {
      var label = new Gtk.Label (text);
      label.xalign = 0.0f;
      label.add_css_class ("heading");
      return label;
    }

    private Gtk.Label status_label () {
      var label = new Gtk.Label ("");
      label.xalign = 0.0f;
      label.wrap = true;
      label.add_css_class ("dim-label");
      return label;
    }

    private void add_row (Gtk.Grid grid, int row, string text, Gtk.Widget widget) {
      if (text != "") {
        var label = new Gtk.Label (text);
        label.xalign = 0.0f;
        label.valign = Gtk.Align.CENTER;
        grid.attach (label, 0, row, 1, 1);
      }
      widget.hexpand = true;
      grid.attach (widget, 1, row, 1, 1);
    }

    private string active_id (Gtk.ComboBoxText combo, string fallback) {
      string? id = combo.active_id;
      return id ?? fallback;
    }

    private bool provider_uses_api_key (string provider) {
      return provider == "maptiler-satellite" || provider == "thunderforest-outdoors";
    }

    private void update_map_field_sensitivity () {
      var provider = active_id (map_provider, "google-legacy");
      api_key_entry.sensitive = provider_uses_api_key (provider);
      map_template_entry.sensitive = provider == "custom";

      if (provider == "custom" && map_template_entry.text == "") {
        map_template_entry.text = "https://server.example/{z}/{x}/{y}.png";
      }
    }

    private void apply_map_provider () {
      var provider = active_id (map_provider, "google-legacy");
      string template;
      switch (provider) {
      case "osm":
        template = OSM_STANDARD;
        break;
      case "opentopomap":
        template = OPENTOPOMAP;
        break;
      case "maptiler-satellite":
        template = MAPTILER_SATELLITE;
        break;
      case "thunderforest-outdoors":
        template = THUNDERFOREST_OUTDOORS;
        break;
      case "custom":
        template = map_template_entry.text.strip ();
        break;
      case "google-legacy":
      default:
        template = GOOGLE_LEGACY_SATELLITE;
        break;
      }

      if (provider_uses_api_key (provider)) {
        var key = api_key_entry.text.strip ();
        if (key == "") {
          map_status.label = "Enter an API key before applying this provider.";
          return;
        }
        template = template.replace ("{key}", key);
      }

      if (template == "") {
        map_status.label = "Map template is empty.";
        return;
      }

      scene.set_map_tile_url_template (template);
      map_status.label = "Applied map tiles: " + template;
    }

    private void update_terrain_field_sensitivity () {
      var provider = active_id (terrain_provider, "silvertone");
      terrain_entry.sensitive = provider == "custom";
      if (provider == "silvertone") {
        terrain_entry.text = DEFAULT_TERRAIN_SERVER;
      }
    }

    private void apply_terrain_provider () {
      var provider = active_id (terrain_provider, "silvertone");
      var server = provider == "custom" ? terrain_entry.text.strip () : DEFAULT_TERRAIN_SERVER;
      if (server == "") {
        terrain_status.label = "Terrain server is empty.";
        return;
      }

      scene.set_terrain_server (server);
      terrain_status.label = "Applied terrain: " + server;
    }

    private void apply_cache () {
      scene.set_cache_enabled (cache_enabled.active);
    }

    private void apply_fog () {
      var density = fog_density.get_value ();
      var fog_start = 3000.0 + (1.0 - density) * 24000.0;
      var fog_end = 22000.0 + (1.0 - density) * 220000.0;

      scene.set_fog_enabled (fog_enabled.active);
      scene.set_fog_range (fog_start, fog_end);

      switch (active_id (fog_color, "sky")) {
      case "haze":
        scene.set_fog_color (0.82, 0.84, 0.82);
        break;
      case "dusk":
        scene.set_fog_color (0.52, 0.48, 0.66);
        break;
      case "dust":
        scene.set_fog_color (0.72, 0.64, 0.50);
        break;
      case "sky":
      default:
        scene.set_fog_color (0.60, 0.72, 0.86);
        break;
      }

      fog_status.label = "%.0f%%, range %.0f-%.0f m".printf (density * 100.0,
                                                              fog_start,
                                                              fog_end);
    }

    private void update_sun_field_sensitivity () {
      sun_time.sensitive = !manual_sun.active;
      sun_azimuth.sensitive = manual_sun.active;
      sun_elevation.sensitive = manual_sun.active;
    }

    private void apply_sun () {
      if (manual_sun.active) {
        var azimuth = sun_azimuth.get_value ();
        var elevation = sun_elevation.get_value ();
        scene.set_sun_position (azimuth, elevation);
        sun_status.label = "Manual: az %.0f deg, el %.0f deg".printf (azimuth, elevation);
      } else {
        var time = sun_time.get_value ();
        scene.set_sun_time_of_day (time);
        var hour = (int) time;
        var minute = (int) Math.round ((time - hour) * 60.0);
        sun_status.label = "Time of day: %02d:%02d".printf (hour, minute);
      }
    }

    private void apply_lighting () {
      scene.set_shadows_enabled (shadows_enabled.active);
      scene.set_terrain_normal_smoothing (normal_smoothing.get_value ());
    }

    private void connect_pick_logging () {
      scene.ground_clicked.connect ((latitude, longitude, altitude_amsl, button) => {
        append_pick_log ("ground click button=%s lat=%.7f lon=%.7f alt=%.1f m AMSL".printf (
          button_label (button),
          latitude,
          longitude,
          altitude_amsl));
      });
      scene.ground_double_clicked.connect ((latitude, longitude, altitude_amsl, button) => {
        append_pick_log ("ground double-click button=%s lat=%.7f lon=%.7f alt=%.1f m AMSL".printf (
          button_label (button),
          latitude,
          longitude,
          altitude_amsl));
      });
      scene.node_clicked.connect ((node, latitude, longitude, altitude_amsl, button) => {
        append_pick_log ("%s click button=%s id=%llu lat=%.7f lon=%.7f alt=%.1f m AMSL".printf (
          primitive_name (node.get_primitive ()),
          button_label (button),
          (uint64) node.get_id (),
          latitude,
          longitude,
          altitude_amsl));
      });
      scene.node_double_clicked.connect ((node, latitude, longitude, altitude_amsl, button) => {
        append_pick_log ("%s double-click button=%s id=%llu lat=%.7f lon=%.7f alt=%.1f m AMSL".printf (
          primitive_name (node.get_primitive ()),
          button_label (button),
          (uint64) node.get_id (),
          latitude,
          longitude,
          altitude_amsl));
      });
    }

    private string primitive_name (GWorld.ScenePrimitive primitive) {
      switch (primitive) {
      case GWorld.ScenePrimitive.CUBE:
        return "cube";
      case GWorld.ScenePrimitive.SPHERE:
        return "sphere";
      case GWorld.ScenePrimitive.CYLINDER:
        return "cylinder";
      case GWorld.ScenePrimitive.MODEL:
        return "model";
      case GWorld.ScenePrimitive.BILLBOARD:
        return "billboard";
      case GWorld.ScenePrimitive.GROUND_OVERLAY:
        return "ground overlay";
      case GWorld.ScenePrimitive.POLYLINE:
        return "polyline";
      case GWorld.ScenePrimitive.POLYGON:
        return "polygon";
      case GWorld.ScenePrimitive.CIRCLE:
        return "circle";
      case GWorld.ScenePrimitive.TEXT_LABEL:
        return "text label";
      default:
        return "node";
      }
    }

    private string button_label (uint button) {
      switch (button) {
      case 1:
        return "left(1)";
      case 2:
        return "middle(2)";
      case 3:
        return "right(3)";
      default:
        return "button(%u)".printf (button);
      }
    }

    private void append_pick_log (string message) {
      if (pick_terminal_buffer == null) {
        return;
      }

      Gtk.TextIter end;
      pick_terminal_buffer.get_end_iter (out end);
      pick_terminal_buffer.insert (ref end, "%03u  %s\n".printf (++pick_log_count, message), -1);

      pick_terminal_buffer.get_end_iter (out end);
      pick_terminal.scroll_to_iter (end, 0.0, false, 0.0, 1.0);
    }

    private string? find_demo_asset_path (string relative_path) {
      string[] prefixes = { "", "..", "../.." };
      foreach (var prefix in prefixes) {
        var path = prefix == "" ? relative_path : Path.build_filename (prefix, relative_path);
        if (FileUtils.test (path, FileTest.EXISTS)) {
          return path;
        }
      }
      return null;
    }

    private void seed_scene_nodes () {
      var cube = scene.add_cube (HOME_LATITUDE,
                                 HOME_LONGITUDE,
                                 1200.0,
                                 900.0,
                                 900.0,
                                 900.0);
      cube.set_color (1.0, 0.08, 0.02);
      cube.set_orientation_ned (35.0, 0.0, 12.0);

      var sphere = scene.add_sphere (-35.0060, 147.4720, 820.0, 360.0);
      sphere.set_color (0.18, 0.55, 0.95);

      var cylinder = scene.add_cylinder (-34.9980, 147.4560, 760.0, 320.0, 700.0);
      cylinder.set_color (0.40, 0.78, 0.30);

      var route = scene.add_polyline ();
      route.set_color (0.95, 0.86, 0.18);
      route.set_width (35.0);
      route.set_opacity (0.92);
      route.set_altitude_mode (GWorld.SceneAltitudeMode.AGL);
      route.append_point (-35.0105, 147.4480, 90.0);
      route.append_point (-35.0060, 147.4575, 120.0);
      route.append_point (-35.0010, 147.4665, 140.0);
      route.append_point (-34.9960, 147.4785, 110.0);

      var zone = scene.add_polygon ();
      zone.set_altitude_mode (GWorld.SceneAltitudeMode.AGL);
      zone.set_fill_color (0.05, 0.48, 0.95, 0.24);
      zone.set_outline_color (0.10, 0.75, 1.0, 0.9);
      zone.set_outline_width (22.0);
      zone.append_point (-35.0100, 147.4700, 18.0);
      zone.append_point (-35.0040, 147.4820, 18.0);
      zone.append_point (-35.0140, 147.4910, 18.0);
      zone.append_point (-35.0200, 147.4770, 18.0);

      var bubble = scene.add_circle (-34.9908, 147.4620, 15.0, 850.0);
      bubble.set_altitude_mode (GWorld.SceneAltitudeMode.AGL);
      bubble.set_fill_color (0.18, 0.85, 0.42, 0.18);
      bubble.set_outline_color (0.20, 1.0, 0.45, 0.9);
      bubble.set_outline_width (20.0);
      bubble.set_segments (96);

      var label = scene.add_text_label ("Training zone", -34.9908, 147.4620, 220.0);
      label.set_altitude_mode (GWorld.SceneAltitudeMode.AGL);
      label.set_font ("Sans Bold 22");
      label.set_size_limits (54.0, 240.0);
      label.set_reference_size (160.0, 4500.0);
      label.set_max_visible_distance (60000.0);

      var billboard_path = find_demo_asset_path ("examples/assets/BillboardMarker/map-symbol-location-02.png");
      if (billboard_path != null) {
        var billboard = scene.add_billboard (billboard_path, -34.9474, 147.5098, 1400.0);
        billboard.set_altitude_mode (GWorld.SceneAltitudeMode.AGL);
        billboard.set_size_limits (48.0, 180.0);
        billboard.set_reference_size (140.0, 5000.0);
        billboard.set_max_visible_distance (60000.0);
      }

      var overlay_path = find_demo_asset_path ("examples/assets/GroundOverlay/aiga-heliport.png");
      if (overlay_path != null) {
        var ground_overlay = scene.add_ground_overlay (overlay_path,
                                                       -34.9865, 147.4748,
                                                       -34.9865, 147.4847,
                                                       -34.9921, 147.4847,
                                                       -34.9921, 147.4748);
        ground_overlay.set_opacity (0.78);
        ground_overlay.set_altitude_offset (1.4);
      }

      var model_path = find_demo_asset_path ("examples/assets/ToyCar/ToyCar.glb");
      if (model_path != null) {
        var model = scene.add_model (model_path, -35.0010, 147.4585, 1120.0);
        model.set_scale (12000.0, 12000.0, 12000.0);
        model.set_color (1.0, 1.0, 1.0);
        model.set_orientation_ned (120.0, 0.0, 0.0);
      }
    }
  }

  public static int main (string[] args) {
    var app = new Gtk.Application ("au.com.silvertone.GWorldScene.ValaControls",
                                   GLib.ApplicationFlags.DEFAULT_FLAGS);
    app.activate.connect (() => {
      var window = new ControlPanelWindow (app);
      window.present ();
    });

    return app.run (args);
  }
}
