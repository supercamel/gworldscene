#include <gtk/gtk.h>

#include "gworldscene.h"

static void
activate(GtkApplication *app, gpointer user_data)
{
  (void)user_data;

  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "GWorldScene");
  gtk_window_set_default_size(GTK_WINDOW(window), 1100, 760);

  GtkWidget *view = gworld_scene_view_new();
  gworld_scene_view_set_camera(GWORLD_SCENE_VIEW(view), -35.0024, 147.4648, 5200.0);
  gworld_scene_view_set_camera_orientation(GWORLD_SCENE_VIEW(view), 35.0, -26.0);
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
