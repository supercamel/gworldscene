#include "gworld-scene-view.h"
#include "stalled-tile-server-private.h"
#include <epoxy/gl.h>
#include <glib/gstdio.h>
#include <cmath>
#include <functional>
#include <limits>

static bool wait_until(const std::function<bool()> &ready) {
  const auto end=g_get_monotonic_time()+20*G_USEC_PER_SEC;
  while(g_get_monotonic_time()<end) {
    for(int i=0;i<100 && g_main_context_pending(nullptr);++i) g_main_context_iteration(nullptr,FALSE);
    if(ready()) return true;
    g_usleep(1000);
  }
  return false;
}
static int colored(GdkPixbuf *image,int c) {
  int count=0;
  for(int y=0;y<gdk_pixbuf_get_height(image);++y) {
    auto *p=gdk_pixbuf_get_pixels(image)+y*gdk_pixbuf_get_rowstride(image);
    for(int x=0;x<gdk_pixbuf_get_width(image);++x,p+=4)
      if(p[c]>50 && p[c]>p[(c+1)%3]*2 && p[c]>p[(c+2)%3]*2) ++count;
  }
  return count;
}
static std::pair<double,double> red_center(GdkPixbuf *image) {
  double sx=0,sy=0,n=0;
  for(int y=0;y<gdk_pixbuf_get_height(image);++y) {
    auto *p=gdk_pixbuf_get_pixels(image)+y*gdk_pixbuf_get_rowstride(image);
    for(int x=0;x<gdk_pixbuf_get_width(image);++x,p+=4)
      if(p[0]>50 && p[0]>p[1]*2 && p[0]>p[2]*2) {sx+=x+.5;sy+=y+.5;++n;}
  }
  g_assert_cmpfloat(n,>,0);return {sx/n,sy/n};
}
static GWorldSceneFrame *capture(GWorldSceneView *v,int w=320,int h=240,gint64 stamp=123456) {
  GError *error=nullptr;
  auto *frame=gworld_scene_view_capture_frame(v,w,h,stamp,&error);
  g_assert_no_error(error); g_assert_nonnull(frame);
  g_assert_cmpint(gworld_scene_frame_get_timestamp(frame),==,stamp);
  g_assert_cmpint(gworld_scene_frame_get_completed_time(frame),>,0);
  g_assert_cmpint(gdk_pixbuf_get_width(gworld_scene_frame_get_image(frame)),==,w);
  return frame;
}
static void test_camera(gconstpointer data) {
#if GWORLD_SCENE_GTK_MAJOR == 4
  if(!gtk_init_check()) {g_test_skip("No display");return;}
  auto *window=gtk_window_new();
#else
  if(!gtk_init_check(nullptr,nullptr)) {g_test_skip("No display");return;}
  auto *window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
#endif
  StalledTileServer server;
  auto *v=GWORLD_SCENE_VIEW(gworld_scene_view_new());
  auto *area=GTK_GL_AREA(v);
  if(GPOINTER_TO_INT(data)) {
#if GWORLD_SCENE_GTK_MAJOR == 4 && GTK_CHECK_VERSION(4,12,0)
    gtk_gl_area_set_allowed_apis(area,GDK_GL_API_GLES);
#else
    gtk_gl_area_set_use_es(area,TRUE);
#endif
  }
  gworld_scene_view_set_cache_enabled(v,FALSE);
  gworld_scene_view_set_terrain_server(v,(server.base+"terrain/{tile}.hgt").c_str());
  gworld_scene_view_set_map_tile_url_template(v,(server.base+"tiles/{z}/{x}/{y}.png").c_str());
  g_autoptr(GWorldSceneTerrainSource) terrain=gworld_scene_terrain_source_new();
  const guint8 flat[]={0,0,0,0,0,0,0,0};
  g_autoptr(GBytes) tile=g_bytes_new(flat,sizeof flat);
  g_assert_true(gworld_scene_terrain_source_put_tile(terrain,0,0,2,tile,nullptr));
  gworld_scene_view_set_terrain_source(v,terrain);
  g_assert_true(gworld_scene_view_get_terrain_source(v)==terrain);
  auto *other=GWORLD_SCENE_VIEW(gworld_scene_view_new());g_object_ref_sink(other);
  gworld_scene_view_set_terrain_source(other,terrain);
  double height=1;
  g_assert_true(gworld_scene_view_sample_terrain_altitude(other,.5,.5,&height));g_assert_cmpfloat(height,==,0);
  g_object_unref(other);
  gworld_scene_view_set_atmosphere_enabled(v,FALSE);
  gworld_scene_view_set_sun_position(v,180,45);
  GError *error=nullptr;
  g_assert_true(gworld_scene_view_set_camera_pose(v,0.5,0.5,10,2,0,0,0,&error));
  double lat,lon,alt,qw,qx,qy,qz;
  gworld_scene_view_get_camera(v,&lat,&lon,&alt); g_assert_cmpfloat(alt,==,10);
  g_assert_true(gworld_scene_view_get_camera_pose_enabled(v));
  gworld_scene_view_get_camera_quaternion(v,&qw,&qx,&qy,&qz); g_assert_cmpfloat(qw,==,1);
  g_assert_false(gworld_scene_view_set_camera_pose(v,0,0,0,0,0,0,0,&error));
  g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);
  gworld_scene_view_get_camera(v,&lat,&lon,&alt);g_assert_cmpfloat(alt,==,10);
  g_assert_false(gworld_scene_view_set_camera_projection(v,320,240,NAN,120,160,120,.01,100000,&error));
  g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);
  g_assert_true(gworld_scene_view_set_camera_projection(v,320,240,160,160,160,120,.01,100000,&error));
  g_assert_null(gworld_scene_view_capture_frame(v,320,240,0,&error));
  g_assert_error(error,G_IO_ERROR,G_IO_ERROR_FAILED);g_clear_error(&error);
  // A 2 cm object 10 cm in front of the eye. Old 2 m near clipping removes it.
  auto *near_node=gworld_scene_view_add_cube(v,0.5+0.1/110574.0,0.5,10,0.04,0.02,0.04);
  gworld_scene_node_set_color(GWORLD_SCENE_NODE(near_node),1,0,0);
  auto *far_node=gworld_scene_view_add_cube(v,0.5+20/110574.0,0.5,10,20,1,20);
  gworld_scene_node_set_color(GWORLD_SCENE_NODE(far_node),0,1,0);
  gtk_window_set_default_size(GTK_WINDOW(window),320,240);
#if GWORLD_SCENE_GTK_MAJOR == 4
  gtk_window_set_child(GTK_WINDOW(window),GTK_WIDGET(v));
#else
  gtk_container_add(GTK_CONTAINER(window),GTK_WIDGET(v));
#endif
  // Explicit realization: capture before first mapping is supported.
  gtk_widget_realize(window);gtk_widget_realize(GTK_WIDGET(v));
  gworld_scene_view_set_offscreen_enabled(v,TRUE);
  g_assert_true(wait_until([&]{g_autoptr(GWorldSceneFrame) f=capture(v);return colored(gworld_scene_frame_get_image(f),0)>100;}));
  g_autoptr(GWorldSceneFrame) first=capture(v);
  auto *im=gworld_scene_frame_get_image(first);
  g_assert_cmpint(colored(im,1),>,1000);
  auto *center=gdk_pixbuf_get_pixels(im)+120*gdk_pixbuf_get_rowstride(im)+160*4;
  g_assert_cmpint(center[0],>,center[1]*2); // Near red occludes far green.
  const auto sequence=gworld_scene_frame_get_sequence(first);
  g_autoptr(GWorldSceneFrame) large=capture(v,640,480);
  g_assert_cmpuint(gworld_scene_frame_get_sequence(large),>,sequence);
  const int small_red=colored(im,0);
  // Async readback retains the request's pose/projection and timestamp, has a
  // bounded pending slot, and returns an immutable top-down image on polling.
  g_assert_null(gworld_scene_view_poll_capture_frame(v,&error));g_assert_no_error(error);
  g_assert_true(gworld_scene_view_request_capture_frame_scaled(v,640,480,1,320,240,765432,&error));g_assert_no_error(error);
  g_assert_false(gworld_scene_view_request_capture_frame_scaled(v,640,480,1,320,240,0,&error));
  g_assert_error(error,G_IO_ERROR,G_IO_ERROR_BUSY);g_clear_error(&error);
  g_assert_true(gworld_scene_view_set_camera_projection(v,320,240,320,320,160,120,.01,100000,&error));
  GWorldSceneFrame *async_frame=nullptr;
  g_assert_true(wait_until([&]{async_frame=gworld_scene_view_poll_capture_frame(v,&error);g_assert_no_error(error);return async_frame!=nullptr;}));
  g_assert_cmpint(gworld_scene_frame_get_timestamp(async_frame),==,765432);
  g_assert_cmpint(std::abs(colored(gworld_scene_frame_get_image(async_frame),0)-small_red),<,small_red/5);
  g_object_unref(async_frame);
  g_assert_null(gworld_scene_view_poll_capture_frame(v,&error));g_assert_no_error(error);
  g_assert_true(gworld_scene_view_set_camera_projection(v,320,240,160,160,160,120,.01,100000,&error));
  g_assert_cmpint(colored(gworld_scene_frame_get_image(large),0),>,small_red*3);
  // GPU resampling preserves the calibrated sensor FOV; crop changes only framing.
  g_autoptr(GWorldSceneFrame) scaled=gworld_scene_view_capture_frame_scaled(v,640,480,1,320,240,123,&error);
  g_assert_no_error(error);g_assert_nonnull(scaled);
  g_assert_cmpint(gworld_scene_frame_get_timestamp(scaled),==,123);
  g_assert_cmpint(gdk_pixbuf_get_width(gworld_scene_frame_get_image(scaled)),==,320);
  g_assert_cmpint(std::abs(colored(gworld_scene_frame_get_image(scaled),0)-small_red),<,small_red/5);
  g_autoptr(GWorldSceneFrame) cropped=gworld_scene_view_capture_frame_scaled(v,640,480,2,320,240,124,&error);
  g_assert_no_error(error);g_assert_nonnull(cropped);
  g_assert_cmpint(colored(gworld_scene_frame_get_image(cropped),0),>,small_red*3);
  for (double invalid : {0.0,static_cast<double>(NAN),1000.0}) {
    g_assert_null(gworld_scene_view_capture_frame_scaled(v,640,480,invalid,320,240,0,&error));
    g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);
  }
  g_assert_null(gworld_scene_view_capture_frame_scaled(v,640,480,1,0,240,0,&error));
  g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);
  // Portrait output contains the complete unchanged sensor image with black bars.
  g_autoptr(GWorldSceneFrame) portrait=capture(v,320,480);
  auto *p=gdk_pixbuf_get_pixels(gworld_scene_frame_get_image(portrait));
  g_assert_cmpint(p[0]+p[1]+p[2],==,0);g_assert_cmpint(p[3],==,255);
  g_assert_cmpint(colored(gworld_scene_frame_get_image(portrait),0),==,small_red);
  // Immutable CPU image remains unchanged while later projections render.
  g_assert_true(gworld_scene_view_set_camera_projection(v,320,240,320,320,160,120,.01,100000,&error));
  g_autoptr(GWorldSceneFrame) zoom=capture(v);
  g_assert_cmpint(colored(gworld_scene_frame_get_image(zoom),0),>,small_red*3);
  g_assert_cmpint(colored(gworld_scene_frame_get_image(first),0),==,small_red);
  // Remove near geometry by clipping; green remains, without changing its mesh.
  g_assert_true(gworld_scene_view_set_camera_projection(v,320,240,160,160,160,120,.2,100000,&error));
  g_autoptr(GWorldSceneFrame) clipped=capture(v);
  g_assert_cmpint(colored(gworld_scene_frame_get_image(clipped),0),==,0);
  g_assert_cmpint(colored(gworld_scene_frame_get_image(clipped),1),>,1000);
  // Principal point and full roll are verified against independent pixel locations.
  g_assert_true(gworld_scene_view_set_camera_projection(v,320,240,160,160,200,150,.01,100000,&error));
  g_autoptr(GWorldSceneFrame) shifted=capture(v);
  auto centroid=red_center(gworld_scene_frame_get_image(shifted));
  g_assert_cmpfloat_with_epsilon(centroid.first,200,2);
  g_assert_cmpfloat_with_epsilon(centroid.second,150,2);
  g_assert_true(gworld_scene_view_set_camera_projection(v,320,240,160,160,160,120,.01,100000,&error));
  gworld_scene_node_set_position(GWORLD_SCENE_NODE(near_node),.5+.1/110574.0,.5+.03/111315.0,10);
  // No main-loop iteration or terrain job completion between pose edit and capture.
  g_autoptr(GWorldSceneFrame) moved=capture(v);
  g_assert_cmpfloat(red_center(gworld_scene_frame_get_image(moved)).first,>,190);
  g_assert_true(gworld_scene_view_set_camera_pose(v,.5,.5,10,std::sqrt(.5),std::sqrt(.5),0,0,&error));
  g_assert_true(gworld_scene_view_get_camera_pose_enabled(v));
  gworld_scene_view_get_camera_quaternion(v,&qw,&qx,&qy,&qz);
  g_assert_cmpfloat_with_epsilon(qx,std::sqrt(.5),1e-12);
  g_autoptr(GWorldSceneFrame) rolled=capture(v);
  centroid=red_center(gworld_scene_frame_get_image(rolled));
  g_assert_cmpfloat_with_epsilon(centroid.first,160,2);g_assert_cmpfloat(centroid.second,<,90);
  // Near clipping at 1 cm must not collapse metre differences at 10 km.
  gworld_scene_view_clear_nodes(v);
  g_assert_true(gworld_scene_view_set_camera_pose(v,.5,.5,1000,1,0,0,0,&error));
  auto *blue=gworld_scene_view_add_cube(v,.5+10001/110574.0,.5,1000,500,.1,500);
  gworld_scene_node_set_color(GWORLD_SCENE_NODE(blue),0,0,1);
  auto *green=gworld_scene_view_add_cube(v,.5+10000/110574.0,.5,1000,500,.1,500);
  gworld_scene_node_set_color(GWORLD_SCENE_NODE(green),0,1,0);
  g_assert_true(wait_until([&]{g_autoptr(GWorldSceneFrame) f=capture(v);auto *image=gworld_scene_frame_get_image(f);
    auto *c=gdk_pixbuf_get_pixels(image)+120*gdk_pixbuf_get_rowstride(image)+160*4;return c[1]>50 && c[1]>c[2]*2;}));
  gworld_scene_node_set_position(GWORLD_SCENE_NODE(green),.5+10002/110574.0,.5,1000);
  g_assert_true(wait_until([&]{g_autoptr(GWorldSceneFrame) f=capture(v);auto *image=gworld_scene_frame_get_image(f);
    auto *c=gdk_pixbuf_get_pixels(image)+120*gdk_pixbuf_get_rowstride(image)+160*4;return c[2]>50 && c[2]>c[1]*2;}));
#if GWORLD_SCENE_GTK_MAJOR == 4
  gtk_window_present(GTK_WINDOW(window));
#else
  gtk_widget_show_all(window);
#endif
  g_assert_true(wait_until([&]{return gtk_widget_get_mapped(GTK_WIDGET(v));}));
  gtk_widget_hide(GTK_WIDGET(v));
  g_assert_false(gtk_widget_get_mapped(GTK_WIDGET(v)));
  g_autoptr(GWorldSceneFrame) hidden=capture(v);
  g_assert_nonnull(hidden);
  for(const auto &request:server.pending) g_assert_false(g_str_has_prefix(request.first.c_str(),"/terrain/"));
  // Source replacement shares fresh samples and never admits the old source later.
  g_autoptr(GWorldSceneTerrainSource) replacement=gworld_scene_terrain_source_new();
  const guint8 high[]={0,100,0,100,0,100,0,100};
  g_autoptr(GBytes) high_tile=g_bytes_new(high,sizeof high);
  g_assert_true(gworld_scene_terrain_source_put_tile(replacement,0,0,2,high_tile,nullptr));
  gworld_scene_view_set_terrain_source(v,replacement);
  g_assert_true(gworld_scene_view_sample_terrain_altitude(v,.5,.5,&height));g_assert_cmpfloat(height,==,100);
  gworld_scene_terrain_source_remove_tile(terrain,0,0);
  g_assert_true(gworld_scene_view_sample_terrain_altitude(v,.5,.5,&height));g_assert_cmpfloat(height,==,100);
  gworld_scene_view_set_offscreen_enabled(v,FALSE);
  // Retaining just the exported image must keep imagery credits alive too.
  g_autoptr(GWorldSceneTileProvider) provider=gworld_scene_tile_provider_new(0,0,256);
  g_autoptr(GdkPixbuf) source_pixels=gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,256,256);
  gdk_pixbuf_fill(source_pixels,0xb030b0ff);
  g_signal_connect(provider,"tile-requested",G_CALLBACK(+[](GWorldSceneTileProvider *p,guint64 id,int,int,int,gpointer image){
    g_assert_true(gworld_scene_tile_provider_complete_annotated(p,id,GDK_PIXBUF(image),0,"frame-credit"));
  }),source_pixels);
  gworld_scene_view_set_texture_memory_budget_mib(v,32);
  gworld_scene_view_set_tile_provider(v,provider);
  gworld_scene_view_set_offscreen_enabled(v,TRUE);
  g_assert_true(gworld_scene_view_set_camera_pose(v,.5,.5,1000,std::sqrt(.5),0,-std::sqrt(.5),0,&error));
  g_assert_true(wait_until([&]{g_autoptr(GWorldSceneFrame) f=capture(v);auto *image=gworld_scene_frame_get_image(f);
    auto *c=gdk_pixbuf_get_pixels(image)+120*gdk_pixbuf_get_rowstride(image)+160*4;
    return c[0]>50 && c[2]>50 && c[0]>c[1]*2 && c[2]>c[1]*2;}));
  auto *attributed=capture(v);
  auto *retained_image=GDK_PIXBUF(g_object_ref(gworld_scene_frame_get_image(attributed)));
  g_object_unref(attributed);
  gworld_scene_view_set_tile_provider(v,nullptr);
  gworld_scene_tile_provider_close(provider);
  {g_autoptr(GWorldSceneFrame) detached=capture(v);}
  g_assert_true(wait_until([&]{return gworld_scene_tile_provider_get_held_count(provider)==0;}));
  g_assert_cmpuint(gworld_scene_tile_provider_get_annotation_count(provider),==,1);
  g_object_unref(retained_image);
  g_assert_true(wait_until([&]{return gworld_scene_tile_provider_get_annotation_count(provider)==0;}));
  gworld_scene_view_set_offscreen_enabled(v,FALSE);
  g_assert_true(gworld_scene_view_request_capture_frame_scaled(v,320,240,1,160,120,99,&error));
  g_assert_no_error(error);
  gtk_widget_unrealize(GTK_WIDGET(v));
  g_assert_null(gworld_scene_view_poll_capture_frame(v,&error));g_assert_no_error(error);
  gtk_widget_realize(GTK_WIDGET(v));
  g_assert_true(gworld_scene_view_request_capture_frame_scaled(v,320,240,1,160,120,100,&error));
  g_assert_no_error(error);async_frame=nullptr;
  g_assert_true(wait_until([&]{async_frame=gworld_scene_view_poll_capture_frame(v,&error);g_assert_no_error(error);return async_frame!=nullptr;}));
  g_assert_cmpint(gdk_pixbuf_get_width(gworld_scene_frame_get_image(async_frame)),==,160);
  g_assert_cmpint(gworld_scene_frame_get_timestamp(async_frame),==,100);g_object_unref(async_frame);
  gworld_scene_view_reset_camera_projection(v);
  gworld_scene_view_set_free_camera_orientation(v,0,0);
  g_assert_false(gworld_scene_view_get_camera_pose_enabled(v));
  g_assert_null(gworld_scene_view_capture_frame(v,8192,8192,0,&error));
  g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);
#if GWORLD_SCENE_GTK_MAJOR == 4
  gtk_window_destroy(GTK_WINDOW(window));
#else
  gtk_widget_destroy(window);
#endif
  g_assert_cmpint(colored(gworld_scene_frame_get_image(first),0),==,small_red);
}
// A camera and a centimetre-scale model must stay aligned before terrain
// catches up with a relocation of the camera far from the current mesh origin.
static void test_relative_model(gconstpointer data) {
#if GWORLD_SCENE_GTK_MAJOR == 4
  if(!gtk_init_check()) {g_test_skip("No display");return;}
  auto *window=gtk_window_new();
#else
  if(!gtk_init_check(nullptr,nullptr)) {g_test_skip("No display");return;}
  auto *window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
#endif
  StalledTileServer server;
  auto *v=GWORLD_SCENE_VIEW(gworld_scene_view_new());
  if(GPOINTER_TO_INT(data)) {
#if GWORLD_SCENE_GTK_MAJOR == 4 && GTK_CHECK_VERSION(4,12,0)
    gtk_gl_area_set_allowed_apis(GTK_GL_AREA(v),GDK_GL_API_GLES);
#else
    gtk_gl_area_set_use_es(GTK_GL_AREA(v),TRUE);
#endif
  }
  gworld_scene_view_set_cache_enabled(v,FALSE);
  gworld_scene_view_set_shadows_enabled(v,FALSE);
  gworld_scene_view_set_atmosphere_enabled(v,FALSE);
  gworld_scene_view_set_fog_enabled(v,FALSE);
  gworld_scene_view_set_map_tile_url_template(v,(server.base+"tiles/{z}/{x}/{y}.png").c_str());
  gworld_scene_view_set_terrain_server(v,(server.base+"terrain/{tile}.hgt").c_str());
  GError *error=nullptr;
  g_autofree char *directory=g_dir_make_tmp("gworldscene-relative-model-XXXXXX",&error);
  g_assert_no_error(error);
  const std::string path=std::string(directory)+"/near.obj";
  g_assert_true(g_file_set_contents(path.c_str(),
    "v -0.01 -0.01 0.05\nv 0.01 -0.01 0.05\nv 0.01 0.01 0.05\nv -0.01 0.01 0.05\nf 1 2 3\nf 1 3 4\n",-1,&error));
  g_assert_no_error(error);
  auto *node=GWORLD_SCENE_NODE(gworld_scene_view_add_model(v,path.c_str(),0,0,600));
  gworld_scene_node_set_color(node,1,0,0);
  g_assert_true(gworld_scene_view_set_camera_pose(v,0,0,600,1,0,0,0,&error));
  g_assert_true(gworld_scene_view_set_camera_projection(v,320,240,160,160,160,120,.005,100000,&error));
#if GWORLD_SCENE_GTK_MAJOR == 4
  gtk_window_set_child(GTK_WINDOW(window),GTK_WIDGET(v));
#else
  gtk_container_add(GTK_CONTAINER(window),GTK_WIDGET(v));
#endif
  gtk_window_set_default_size(GTK_WINDOW(window),320,240);
  gtk_widget_realize(window);gtk_widget_realize(GTK_WIDGET(v));
  gworld_scene_view_set_offscreen_enabled(v,TRUE);
  struct Footprint {int count=0;double x=0,y=0;};
  auto footprint=[](GdkPixbuf *image){Footprint f;
    for(int y=0;y<240;y++)for(int x=0;x<320;x++){
      const auto *p=gdk_pixbuf_get_pixels(image)+y*gdk_pixbuf_get_rowstride(image)+x*4;
      if(p[0]>p[1]+10 && p[0]>p[2]+10){f.count++;f.x+=x+.5;f.y+=y+.5;}
    }
    if(f.count){f.x/=f.count;f.y/=f.count;}return f;
  };
  g_assert_true(wait_until([&]{g_autoptr(GWorldSceneFrame) f=capture(v);return footprint(gworld_scene_frame_get_image(f)).count>3000;}));
  // No main-context iteration: the old mesh origin cannot rebase between captures.
  for(int i=0;i<8;i++) {
    const double lat=-35.0+i*.000001,lon=149.0;
    gworld_scene_node_set_position(node,lat,lon,600);
    g_assert_true(gworld_scene_view_set_camera_pose(v,lat,lon,600,1,0,0,0,&error));
    g_autoptr(GWorldSceneFrame) f=capture(v);
    auto *image=gworld_scene_frame_get_image(f);
    const auto fprint=footprint(image);
    g_assert_cmpint(fprint.count,>,3500);
    g_assert_cmpint(fprint.count,<,4700);
    g_assert_cmpfloat(std::abs(fprint.x-160),<,2);
    g_assert_cmpfloat(std::abs(fprint.y-120),<,2);
  }
  gworld_scene_view_clear_nodes(v);
  // Sky rays must stay finite with millimetre near clipping and a translated
  // eye. Inverting a float world MVP loses the perspective terms here.
  gworld_scene_view_set_atmosphere_enabled(v,TRUE);
  for(int i=0;i<100;i++) {
    const double pitch=(50+i*.1)*G_PI/180;
    const double yaw=i*3.6*G_PI/180,roll=std::sin(i*.3)*.2;
    const double ch=std::cos(yaw/2),sh=std::sin(yaw/2),cp=std::cos(pitch/2),sp=std::sin(pitch/2),cr=std::cos(roll/2),sr=std::sin(roll/2);
    g_assert_true(gworld_scene_view_set_camera_pose(v,-35+i*.000001,149,600,
      ch*cp*cr+sh*sp*sr,ch*cp*sr-sh*sp*cr,ch*sp*cr+sh*cp*sr,sh*cp*cr-ch*sp*sr,&error));
    g_autoptr(GWorldSceneFrame) f=capture(v);
    const auto *p=gdk_pixbuf_get_pixels(gworld_scene_frame_get_image(f))+120*320*4+160*4;
    g_assert_cmpint(int(p[0])+p[1]+p[2],>,100);
  }
  gworld_scene_view_set_offscreen_enabled(v,FALSE);
#if GWORLD_SCENE_GTK_MAJOR == 4
  gtk_window_destroy(GTK_WINDOW(window));
#else
  gtk_widget_destroy(window);
#endif
  g_remove(path.c_str());g_rmdir(directory);
}
int main(int argc,char **argv) {
  g_test_init(&argc,&argv,nullptr);
  g_test_add_data_func("/camera-capture/automatic",GINT_TO_POINTER(FALSE),test_camera);
  g_test_add_data_func("/camera-capture/gles",GINT_TO_POINTER(TRUE),test_camera);
  g_test_add_data_func("/camera/relative-model",nullptr,test_relative_model);
  g_test_add_data_func("/camera/relative-model-gles",GINT_TO_POINTER(1),test_relative_model);
  return g_test_run();
}
