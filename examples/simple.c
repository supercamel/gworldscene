#include <gtk/gtk.h>
#include <libsoup/soup.h>

#include "gworldscene.h"

static gboolean
env_is_set(const char *value)
{
  return value != NULL && value[0] != '\0';
}

static char *
extract_json_string(const char *json, const char *field)
{
  char *needle = g_strdup_printf("\"%s\"", field);
  char *key = strstr(json, needle);
  g_free(needle);
  if (key == NULL)
    return NULL;

  char *colon = strchr(key, ':');
  if (colon == NULL)
    return NULL;

  char *quote = strchr(colon, '"');
  if (quote == NULL)
    return NULL;

  GString *value = g_string_new(NULL);
  for (const char *p = quote + 1; *p != '\0'; ++p) {
    if (*p == '"')
      return g_string_free(value, FALSE);

    if (*p == '\\') {
      ++p;
      if (*p == '\0')
        break;

      switch (*p) {
      case '"':
      case '\\':
      case '/':
        g_string_append_c(value, *p);
        break;
      case 'b':
        g_string_append_c(value, '\b');
        break;
      case 'f':
        g_string_append_c(value, '\f');
        break;
      case 'n':
        g_string_append_c(value, '\n');
        break;
      case 'r':
        g_string_append_c(value, '\r');
        break;
      case 't':
        g_string_append_c(value, '\t');
        break;
      default:
        g_string_append_c(value, *p);
        break;
      }
    } else {
      g_string_append_c(value, *p);
    }
  }

  g_string_free(value, TRUE);
  return NULL;
}

static char *
create_google_maps_session(const char *api_key)
{
  char *escaped_key = g_uri_escape_string(api_key, NULL, FALSE);
  char *url = g_strdup_printf("https://tile.googleapis.com/v1/createSession?key=%s",
                              escaped_key);
  g_free(escaped_key);

  SoupSession *session = soup_session_new();
  soup_session_set_timeout(session, 20);
  soup_session_set_idle_timeout(session, 20);

  SoupMessage *message = soup_message_new("POST", url);
  g_free(url);
  if (message == NULL) {
    g_object_unref(session);
    return NULL;
  }

  static const char *request_json =
    "{\"mapType\":\"satellite\",\"language\":\"en-US\",\"region\":\"US\",\"imageFormat\":\"png\"}";
  GBytes *request_body = g_bytes_new_static(request_json, strlen(request_json));
  soup_message_set_request_body_from_bytes(message, "application/json", request_body);
  g_bytes_unref(request_body);

  GError *error = NULL;
  GBytes *response = soup_session_send_and_read(session, message, NULL, &error);
  const guint status = soup_message_get_status(message);
  g_object_unref(message);
  g_object_unref(session);

  if (response == NULL) {
    g_warning("Failed to create Google Maps session: %s",
              error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return NULL;
  }

  gsize response_size = 0;
  const char *response_data = g_bytes_get_data(response, &response_size);
  char *response_text = g_strndup(response_data, response_size);
  g_bytes_unref(response);

  if (status < 200 || status >= 300) {
    g_warning("Failed to create Google Maps session: HTTP %u: %s",
              status,
              response_text);
    g_free(response_text);
    return NULL;
  }

  char *session_token = extract_json_string(response_text, "session");
  if (session_token == NULL)
    g_warning("Failed to create Google Maps session: response did not include a session token");
  g_free(response_text);
  return session_token;
}

static char *
find_demo_asset_path(const char *relative_path)
{
  const char *prefixes[] = {
    "",
    "..",
    "../..",
    NULL,
  };

  for (int i = 0; prefixes[i] != NULL; ++i) {
    char *path = prefixes[i][0] == '\0'
                   ? g_strdup(relative_path)
                   : g_build_filename(prefixes[i], relative_path, NULL);
    if (g_file_test(path, G_FILE_TEST_EXISTS))
      return path;
    g_free(path);
  }

  return NULL;
}

static void
configure_map_tiles(GWorldSceneView *view)
{
  static const char *legacy_google_satellite_template =
    "https://mt.google.com/vt/lyrs=s&x={x}&y={y}&z={z}";

  const char *tile_template = g_getenv("GWORLD_SCENE_MAP_TILE_URL_TEMPLATE");
  if (env_is_set(tile_template)) {
    gworld_scene_view_set_map_tile_url_template(view, tile_template);
    return;
  }

  const char *google_api_key = g_getenv("GWORLD_SCENE_GOOGLE_MAPS_API_KEY");
  const char *google_session = g_getenv("GWORLD_SCENE_GOOGLE_MAPS_SESSION");
  char *generated_google_session = NULL;
  if (env_is_set(google_api_key) && !env_is_set(google_session)) {
    generated_google_session = create_google_maps_session(google_api_key);
    google_session = generated_google_session;
  }

  if (env_is_set(google_api_key) && env_is_set(google_session)) {
    char *escaped_key = g_uri_escape_string(google_api_key, NULL, FALSE);
    char *escaped_session = g_uri_escape_string(google_session, NULL, FALSE);
    char *google_template = g_strdup_printf("https://tile.googleapis.com/v1/2dtiles/{z}/{x}/{y}?session=%s&key=%s",
                                            escaped_session,
                                            escaped_key);
    gworld_scene_view_set_map_tile_url_template(view, google_template);
    g_free(google_template);
    g_free(escaped_session);
    g_free(escaped_key);
    g_free(generated_google_session);
    return;
  } else if (env_is_set(google_api_key) || env_is_set(google_session)) {
    g_warning("Google satellite tiles require both GWORLD_SCENE_GOOGLE_MAPS_API_KEY and GWORLD_SCENE_GOOGLE_MAPS_SESSION");
  }
  g_free(generated_google_session);

  /* Temporary demo default for quickly eyeballing satellite imagery. */
  gworld_scene_view_set_map_tile_url_template(view, legacy_google_satellite_template);
}

static void
activate(GtkApplication *app, gpointer user_data)
{
  (void)user_data;

  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "GWorldScene");
  gtk_window_set_default_size(GTK_WINDOW(window), 1100, 760);

  const double initial_latitude = -35.0024;
  const double initial_longitude = 147.4648;
  const double initial_altitude_amsl = 5200.0;
  const double marker_altitude_amsl = 1200.0;

  GtkWidget *view = gworld_scene_view_new();
  configure_map_tiles(GWORLD_SCENE_VIEW(view));
  gworld_scene_view_set_camera(GWORLD_SCENE_VIEW(view),
                               initial_latitude,
                               initial_longitude,
                               initial_altitude_amsl);
  gworld_scene_view_set_camera_orientation(GWORLD_SCENE_VIEW(view), 35.0, -68.0);
  gworld_scene_view_set_sun_time_of_day(GWORLD_SCENE_VIEW(view), 15.25);
  gworld_scene_view_set_fog_range(GWORLD_SCENE_VIEW(view), 9000.0, 90000.0);
  gworld_scene_view_set_shadows_enabled(GWORLD_SCENE_VIEW(view), TRUE);
  gworld_scene_view_set_terrain_normal_smoothing(GWORLD_SCENE_VIEW(view), 0.92);
  GWorldSceneCubeNode *cube = gworld_scene_view_add_cube(GWORLD_SCENE_VIEW(view),
                                                         initial_latitude,
                                                         initial_longitude,
                                                         marker_altitude_amsl,
                                                         900.0,
                                                         900.0,
                                                         900.0);
  gworld_scene_node_set_color(GWORLD_SCENE_NODE(cube), 1.0, 0.08, 0.02);
  gworld_scene_node_set_orientation_ned(GWORLD_SCENE_NODE(cube), 35.0, 0.0, 12.0);

  GWorldSceneSphereNode *sphere = gworld_scene_view_add_sphere(GWORLD_SCENE_VIEW(view),
                                                               -35.0060,
                                                               147.4720,
                                                               820.0,
                                                               360.0);
  gworld_scene_node_set_color(GWORLD_SCENE_NODE(sphere), 0.18, 0.55, 0.95);

  GWorldSceneCylinderNode *cylinder = gworld_scene_view_add_cylinder(GWORLD_SCENE_VIEW(view),
                                                                     -34.9980,
                                                                     147.4560,
                                                                     760.0,
                                                                     320.0,
                                                                     700.0);
  gworld_scene_node_set_color(GWORLD_SCENE_NODE(cylinder), 0.40, 0.78, 0.30);

  g_autofree char *billboard_path =
    find_demo_asset_path("examples/assets/BillboardMarker/map-symbol-location-02.png");
  if (billboard_path != NULL) {
    GWorldSceneBillboardNode *billboard =
      gworld_scene_view_add_billboard(GWORLD_SCENE_VIEW(view),
                                      billboard_path,
                                      -34.9924,
                                      147.4748,
                                      1800.0);
    gworld_scene_billboard_node_set_altitude_mode(billboard, GWORLD_SCENE_ALTITUDE_AGL);
    gworld_scene_billboard_node_set_size_limits(billboard, 48.0, 180.0);
    gworld_scene_billboard_node_set_reference_size(billboard, 140.0, 5000.0);
    gworld_scene_billboard_node_set_max_visible_distance(billboard, 60000.0);
  } else {
    g_warning("Billboard marker demo image was not found");
  }

  g_autofree char *overlay_path =
    find_demo_asset_path("examples/assets/GroundOverlay/aiga-heliport.png");
  if (overlay_path != NULL) {
    GWorldSceneGroundOverlayNode *ground_overlay =
      gworld_scene_view_add_ground_overlay(GWORLD_SCENE_VIEW(view),
                                           overlay_path,
                                           -34.9962,
                                           147.4660,
                                           -34.9962,
                                           147.4729,
                                           -34.9998,
                                           147.4729,
                                           -34.9998,
                                           147.4660);
    gworld_scene_ground_overlay_node_set_opacity(ground_overlay, 0.78);
    gworld_scene_ground_overlay_node_set_altitude_offset(ground_overlay, 1.4);
  } else {
    g_warning("Ground overlay demo image was not found");
  }

  g_autofree char *model_path =
    find_demo_asset_path("examples/assets/ToyCar/ToyCar.glb");
  if (model_path != NULL) {
    GWorldSceneModelNode *model = gworld_scene_view_add_model(GWORLD_SCENE_VIEW(view),
                                                              model_path,
                                                              -35.0010,
                                                              147.4585,
                                                              1120.0);
    gworld_scene_node_set_scale(GWORLD_SCENE_NODE(model), 12000.0, 12000.0, 12000.0);
    gworld_scene_node_set_color(GWORLD_SCENE_NODE(model), 1.0, 1.0, 1.0);
    gworld_scene_node_set_orientation_ned(GWORLD_SCENE_NODE(model), 120.0, 0.0, 0.0);
  } else {
    g_warning("ToyCar demo model was not found");
  }
  gtk_window_set_child(GTK_WINDOW(window), view);

  gtk_window_present(GTK_WINDOW(window));
}

int
main(int argc, char **argv)
{
  GtkApplication *app = gtk_application_new("au.com.silvertone.GWorldScene.Demo",
                                           G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
