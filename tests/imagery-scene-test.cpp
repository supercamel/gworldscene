#include "gworld-scene-view.h"
#include <epoxy/gl.h>
#include <libsoup/soup.h>
#include <filesystem>
#include <functional>
#include <set>
#include <vector>
#include <array>
namespace {
bool init_display() {
#if GWORLD_SCENE_GTK_MAJOR == 4
  static const bool initialized = gtk_init_check();
#else
  static const bool initialized = gtk_init_check(nullptr, nullptr);
#endif
  if (!initialized)
    g_test_skip("No GTK display available; run under xvfb-run");
  return initialized;
}
GWorldSceneView *new_test_view() {
  auto *view = GWORLD_SCENE_VIEW(gworld_scene_view_new());
  if (g_getenv("GWORLD_SCENE_TEST_GLES") != nullptr) {
#if GWORLD_SCENE_GTK_MAJOR == 4 && GTK_CHECK_VERSION(4, 12, 0)
    gtk_gl_area_set_allowed_apis(GTK_GL_AREA(view), GDK_GL_API_GLES);
#else
    gtk_gl_area_set_use_es(GTK_GL_AREA(view), TRUE);
#endif
  }
  return view;
}
bool wait(const std::function<bool()> &ready,int seconds=20) {
  gint64 end=g_get_monotonic_time()+seconds*G_USEC_PER_SEC;
  while(g_get_monotonic_time()<end) {
    for(int i=0;i<20 && g_main_context_pending(nullptr);i++)g_main_context_iteration(nullptr,FALSE);
    if(ready())return true;
    g_usleep(1000);
  }
  return false;
}
void pump_for(int ms) {
  gint64 end=g_get_monotonic_time()+ms*1000;
  while(g_get_monotonic_time()<end){g_main_context_iteration(nullptr,FALSE);g_usleep(1000);}
}
int colored(GtkGLArea *area,int channel) {
  if(!gtk_widget_get_realized(GTK_WIDGET(area)))return 0;
  gtk_gl_area_make_current(area);g_assert_no_error(gtk_gl_area_get_error(area));gtk_gl_area_attach_buffers(area);
  GLint draw,read;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,draw);
#if GWORLD_SCENE_GTK_MAJOR == 4
  int width=gtk_widget_get_width(GTK_WIDGET(area)),height=gtk_widget_get_height(GTK_WIDGET(area));
#else
  int width=gtk_widget_get_allocated_width(GTK_WIDGET(area)),height=gtk_widget_get_allocated_height(GTK_WIDGET(area));
#endif
  int scale=gtk_widget_get_scale_factor(GTK_WIDGET(area));width*=scale;height*=scale;
  std::vector<unsigned char> pixels(width*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER,read);g_assert_cmpuint(glGetError(),==,GL_NO_ERROR);
  int count=0;
  // Exclude the native footer; inspect actual ground pixels.
  for(int y=height/3;y<height*2/3;y++)for(int x=width/3;x<width*2/3;x++) {
    auto *p=pixels.data()+(y*width+x)*4;
    if(p[channel]>40 && p[channel]>p[(channel+1)%3]*3 && p[channel]>p[(channel+2)%3]*3)count++;
  }
  return count;
}
int presented_colored(GtkGLArea *area,int channel) {
#if GWORLD_SCENE_GTK_MAJOR == 4
  // GtkGLArea rotates render attachments. attach_buffers() can expose an old
  // backing image after a frame, so inspect the widget's presented paintable.
  auto *widget=GTK_WIDGET(area);
  g_autoptr(GdkPaintable) paintable=gtk_widget_paintable_new(widget);
  auto *snapshot=gtk_snapshot_new();
  gdk_paintable_snapshot(paintable,GDK_SNAPSHOT(snapshot),gtk_widget_get_width(widget),gtk_widget_get_height(widget));
  GskRenderNode *node=gtk_snapshot_free_to_node(snapshot);
  // Empty paintables are valid (for example before mapping or after unmapping).
  if (node == nullptr) return 0;
  auto *surface=gtk_native_get_surface(gtk_widget_get_native(widget));
  g_autoptr(GskRenderer) renderer=gsk_renderer_new_for_surface(surface);
  g_autoptr(GdkTexture) texture=gsk_renderer_render_texture(renderer,node,nullptr);
  g_autoptr(GdkPixbuf) image=gdk_pixbuf_get_from_texture(texture);
  gsk_render_node_unref(node);gsk_renderer_unrealize(renderer);
  int width=gdk_pixbuf_get_width(image),height=gdk_pixbuf_get_height(image),stride=gdk_pixbuf_get_rowstride(image),channels=gdk_pixbuf_get_n_channels(image);
  auto *pixels=gdk_pixbuf_read_pixels(image);int count=0;
  for(int y=height/3;y<height*2/3;y++)for(int x=width/3;x<width*2/3;x++) {
    auto *p=pixels+y*stride+x*channels;
    if(p[channel]>40 && p[channel]>p[(channel+1)%3]*3 && p[channel]>p[(channel+2)%3]*3)count++;
  }
  return count;
#else
  return colored(area,channel);
#endif
}

struct PresentedFrame {
  GtkGLArea *area;
  GdkFrameClock *clock;
  gulong handler;
  std::array<int, 3> colors{};

  explicit PresentedFrame(GtkGLArea *area) : area(area) {
    clock = gtk_widget_get_frame_clock(GTK_WIDGET(area));
    g_assert_nonnull(clock);
    g_object_ref(clock);
    handler = g_signal_connect_after(clock, "after-paint", G_CALLBACK(+[](
      GdkFrameClock *, gpointer data) {
      auto &frame = *static_cast<PresentedFrame *>(data);
      for (int channel = 0; channel < 3; ++channel)
        frame.colors[channel] = presented_colored(frame.area, channel);
    }), this);
  }
  ~PresentedFrame() {
    // Window destruction may already have disposed the frame clock.
    if (g_signal_handler_is_connected(clock, handler))
      g_signal_handler_disconnect(clock, handler);
    g_object_unref(clock);
  }
};

struct Supply {
  GWorldSceneTileProvider *provider;
  GdkPixbuf *pixels;
  std::set<guint64> requests,released;
  guint drained=0;
  guint annotation_releases=0;
  const PresentedFrame *presentation=nullptr;
  int released_channel=-1;
  bool pixels_released_while_visible=false;
  bool complete=true;
  int ancestor=-1;
  Supply(int size,guint32 color,int max_zoom=0,int source_zoom=-1):provider(gworld_scene_tile_provider_new(0,max_zoom,size)),pixels(gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,size,size)),ancestor(source_zoom) {
    gdk_pixbuf_fill(pixels,color);
    g_signal_connect(provider,"tile-requested",G_CALLBACK(+[](GWorldSceneTileProvider *p,guint64 id,int zoom,int,int,gpointer data){
      auto &s=*static_cast<Supply *>(data);g_assert_true(s.requests.insert(id).second);
      if(s.complete)g_assert_true(gworld_scene_tile_provider_complete_annotated(p,id,s.pixels,s.ancestor<0?zoom:s.ancestor,"source-credit"));
    }),this);
    g_signal_connect(provider,"tile-released",G_CALLBACK(+[](GWorldSceneTileProvider *,guint64 id,gpointer data){
      auto &s=*static_cast<Supply *>(data);g_assert_true(s.released.insert(id).second);
      if(s.presentation && s.released_channel>=0 && s.presentation->colors[s.released_channel]>2000)
        s.pixels_released_while_visible=true;
    }),this);
    g_signal_connect(provider,"annotation-released",G_CALLBACK(+[](GWorldSceneTileProvider *,const char *token,gpointer data){
      auto &s=*static_cast<Supply *>(data);g_assert_cmpstr(token,==,"source-credit");s.annotation_releases++;
      // Inspect the last completed paint. A fresh snapshot inside a resource
      // callback may be empty while GTK is invalidating the next frame.
      if(s.presentation && s.released_channel>=0)
        g_assert_cmpint(s.presentation->colors[s.released_channel],==,0);
    }),this);
    g_signal_connect(provider,"drained",G_CALLBACK(+[](GWorldSceneTileProvider *,gpointer data){static_cast<Supply *>(data)->drained++;}),this);
  }
  ~Supply(){g_signal_handlers_disconnect_by_data(provider,this);g_object_unref(provider);g_object_unref(pixels);}
  void close(){gworld_scene_tile_provider_close(provider);}
};
void test_scene() {
  if (!init_display()) return;
#if GWORLD_SCENE_GTK_MAJOR == 4
  GtkWidget *window=gtk_window_new();
#else
  GtkWidget *window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
#endif
  auto *view=new_test_view();auto *area=GTK_GL_AREA(view);g_object_ref_sink(view);
  g_autofree char *directory=g_dir_make_tmp("gworldscene-provider-XXXXXX",nullptr);
  struct Requests { unsigned total=0,imagery=0; std::vector<SoupServerMessage *> held; } requests;
  g_autoptr(SoupServer) trap=soup_server_new(nullptr,nullptr);
  soup_server_add_handler(trap,nullptr,+[](SoupServer *,SoupServerMessage *msg,const char *path,GHashTable *,gpointer data){
    auto &requests=*static_cast<Requests *>(data);requests.total++;
    if(g_str_has_prefix(path,"/legacy/")) {
      requests.imagery++;requests.held.push_back(SOUP_SERVER_MESSAGE(g_object_ref(msg)));soup_server_message_pause(msg);
    } else soup_server_message_set_status(msg,SOUP_STATUS_NOT_FOUND,nullptr);
  },&requests,nullptr);
  g_assert_true(soup_server_listen_local(trap,0,static_cast<SoupServerListenOptions>(0),nullptr));
  GSList *uris=soup_server_get_uris(trap);g_autofree char *base=g_uri_to_string(static_cast<GUri *>(uris->data));g_slist_free_full(uris,reinterpret_cast<GDestroyNotify>(g_uri_unref));
  gworld_scene_view_set_map_tile_url_template(view,(std::string(base)+"legacy/{z}/{x}/{y}").c_str());
  gworld_scene_view_set_terrain_server(view,(std::string(base)+"terrain/{tile}").c_str());
  gworld_scene_view_set_cache_directory(view,directory);
  gworld_scene_view_set_cache_enabled(view,FALSE);
  gworld_scene_view_set_texture_memory_budget_mib(view,32);
  gworld_scene_view_set_atmosphere_enabled(view,FALSE);gworld_scene_view_set_fog_enabled(view,FALSE);
  gworld_scene_view_set_water_enabled(view,FALSE);gworld_scene_view_set_shadows_enabled(view,FALSE);
  gworld_scene_view_set_camera(view,0.5,0.5,1000);gworld_scene_view_set_camera_mode(view,GWORLD_SCENE_CAMERA_MODE_FREE);
  gworld_scene_view_set_camera_orientation(view,0,-89);gworld_scene_view_set_sun_position(view,90,60);
  Supply green(256,0x00ff00ff),blue(512,0x0000ffff),busy(512,0xff0000ff,22),failed(256,0xff0000ff),nested(256,0x0000ffff);
  Supply regional(256,0x0000ffff,22,0);
  gworld_scene_view_set_tile_provider(view,green.provider);
  g_assert_false(gworld_scene_view_get_imagery_ready(view));
  pump_for(100);g_assert_true(green.requests.empty()); // hidden views request nothing
  gtk_window_set_default_size(GTK_WINDOW(window),256,256);
#if GWORLD_SCENE_GTK_MAJOR == 4
  gtk_window_set_child(GTK_WINDOW(window),GTK_WIDGET(view));gtk_window_present(GTK_WINDOW(window));
#else
  gtk_container_add(GTK_CONTAINER(window),GTK_WIDGET(view));gtk_widget_show_all(window);
#endif
  PresentedFrame presentation(area);
  g_assert_true(wait([&](){return gworld_scene_view_get_imagery_ready(view) && presentation.colors[1]>2000;}));
  if (g_getenv("GWORLD_SCENE_TEST_GLES") != nullptr)
    g_assert_true(gdk_gl_context_get_use_es(gtk_gl_area_get_context(area)));
  g_assert_cmpuint(green.requests.size(),==,1);g_assert_true(gworld_scene_tile_provider_get_reduced_detail(green.provider));
  g_assert_cmpuint(gworld_scene_tile_provider_get_annotation_count(green.provider),==,1);
  g_assert_true(wait([&](){return gworld_scene_tile_provider_get_worker_count(green.provider)==0;}));
  green.presentation=&presentation;green.released_channel=1;
  g_assert_cmpuint(requests.total,==,0);g_assert_true(std::filesystem::is_empty(directory));
  // Regional metadata may limit each independent requested cell to level zero.
  // The normal atlas levels remain demanded, with native per-cell parent crops.
  gworld_scene_view_set_tile_provider(view,regional.provider);green.close();
  g_assert_true(wait([&](){return gworld_scene_view_get_imagery_ready(view) && colored(area,2)>2000 && green.drained==1;}));
  g_assert_cmpuint(regional.requests.size(),>,16);
  g_assert_true(gworld_scene_tile_provider_get_reduced_detail(regional.provider));
  g_auto(GStrv) regional_demand=gworld_scene_tile_provider_dup_demand(regional.provider);
  g_assert_cmpuint(g_strv_length(regional_demand),==,regional.requests.size());
  for(guint i=0;regional_demand[i];i++)g_assert_false(g_str_has_prefix(regional_demand[i],"0/"));
  // Launch high-detail workers, then change generation while their leases exist.
  gworld_scene_view_set_tile_provider(view,busy.provider);regional.close();
  g_assert_true(wait([&](){return gworld_scene_tile_provider_get_worker_count(busy.provider)>0;}));
  gworld_scene_view_set_tile_provider(view,blue.provider);busy.close();
  g_assert_false(gworld_scene_view_get_imagery_ready(view));
  g_assert_true(wait([&](){return gworld_scene_view_get_imagery_ready(view) && colored(area,2)>2000 && regional.drained==1 && busy.drained==1;}));
  g_assert_cmpuint(green.requests.size(),==,green.released.size());g_assert_cmpuint(busy.requests.size(),==,busy.released.size());
  g_assert_true(green.pixels_released_while_visible);g_assert_cmpuint(green.annotation_releases,==,1);
  g_assert_cmpuint(gworld_scene_tile_provider_get_annotation_count(green.provider),==,0);
  g_assert_cmpuint(gworld_scene_tile_provider_get_annotation_count(busy.provider),==,0);
  pump_for(200);g_assert_cmpint(colored(area,0),==,0); // stale red atlas never appears
  double lat,lon,alt;gworld_scene_view_get_camera(view,&lat,&lon,&alt);
  g_assert_cmpfloat(lat,==,0.5);g_assert_cmpfloat(lon,==,0.5);g_assert_cmpfloat(alt,==,1000);
  gtk_widget_set_visible(GTK_WIDGET(view),FALSE);
  g_assert_true(wait([&](){return gworld_scene_tile_provider_get_held_count(blue.provider)==0 && gworld_scene_tile_provider_get_annotation_count(blue.provider)==0;}));
  auto old=blue.requests.size();pump_for(150);g_assert_cmpuint(blue.requests.size(),==,old);
  g_assert_false(gworld_scene_view_get_imagery_ready(view));gtk_widget_set_visible(GTK_WIDGET(view),TRUE);
  g_assert_true(wait([&](){return gworld_scene_view_get_imagery_ready(view) && colored(area,2)>2000;}));
  g_assert_cmpuint(blue.requests.size(),>,old);
  g_assert_cmpuint(gworld_scene_tile_provider_get_annotation_count(blue.provider),==,1);
  // Terrain's native switch does not disable the application imagery path.
  // Conversely, enabling it must not reactivate the native imagery downloader.
  gworld_scene_view_set_cache_enabled(view,TRUE);
  g_assert_true(wait([&](){return requests.total>0 && gworld_scene_view_get_imagery_ready(view);}));
  g_assert_cmpuint(requests.imagery,==,0);g_assert_true(std::filesystem::is_empty(directory));
  // Legacy mode is opt-in. Its held HTTP responses cannot affect a replacement.
  gworld_scene_view_set_map_tile_url_template(view,(std::string(base)+"legacy/{z}/{x}/{y}").c_str());
  g_assert_true(wait([&](){return !requests.held.empty();}));
  gworld_scene_view_set_tile_provider(view,blue.provider);
  for(auto *msg:requests.held) {
    g_autofree char *png=nullptr;gsize length=0;
    g_assert_true(gdk_pixbuf_save_to_buffer(busy.pixels,&png,&length,"png",nullptr,nullptr));
    soup_server_message_set_status(msg,SOUP_STATUS_OK,nullptr);
    soup_server_message_set_response(msg,"image/png",SOUP_MEMORY_COPY,png,length);
    soup_server_message_unpause(msg);g_object_unref(msg);
  }
  requests.held.clear();
  g_assert_true(wait([&](){return gworld_scene_view_get_imagery_ready(view) && colored(area,2)>2000;}));
  auto legacy_count=requests.imagery;
  pump_for(200);g_assert_cmpuint(requests.imagery,==,legacy_count);g_assert_true(std::filesystem::is_empty(directory));
  // Failed and neutral sources must clear displayed old imagery and stay unready.
  failed.complete=false;gworld_scene_view_set_tile_provider(view,failed.provider);blue.close();
  g_assert_true(wait([&](){return !failed.requests.empty();}));
  for(auto id:failed.requests)g_assert_true(gworld_scene_tile_provider_complete_tile(failed.provider,id,nullptr));
  g_assert_true(wait([&](){return colored(area,2)==0 && blue.drained==1;}));
  g_assert_false(gworld_scene_view_get_imagery_ready(view));
  // A release callback may accept a newer source during a legacy transition.
  // The outer setter must not overwrite that reentrant transition afterwards.
  struct Replacement { GWorldSceneView *view; GWorldSceneTileProvider *provider; } replacement{view,nested.provider};
  auto handler=g_signal_connect(failed.provider,"tile-released",G_CALLBACK(+[](GWorldSceneTileProvider *,guint64,gpointer data){
    auto *r=static_cast<Replacement *>(data);gworld_scene_view_set_tile_provider(r->view,r->provider);
  }),&replacement);
  gworld_scene_view_set_map_tile_url_template(view,(std::string(base)+"legacy/{z}/{x}/{y}").c_str());
  g_signal_handler_disconnect(failed.provider,handler);failed.close();
  g_assert_true(wait([&](){return gworld_scene_view_get_imagery_ready(view) && colored(area,2)>2000;}));
  gworld_scene_view_set_tile_provider(view,nullptr);nested.close();
  g_assert_true(wait([&](){return nested.drained==1;}));
  g_assert_true(wait([&](){return failed.drained==1;}));pump_for(150);
  g_assert_cmpuint(requests.imagery,==,legacy_count);g_assert_true(std::filesystem::is_empty(directory));
  g_assert_false(gworld_scene_view_get_imagery_ready(view));
#if GWORLD_SCENE_GTK_MAJOR == 4
  gtk_window_destroy(GTK_WINDOW(window));
#else
  gtk_widget_destroy(window);
#endif
  g_object_unref(view);pump_for(100);std::filesystem::remove_all(directory);
}

void test_legacy_unmap() {
  if (!init_display()) return;
#if GWORLD_SCENE_GTK_MAJOR == 4
  auto *window = gtk_window_new();
#else
  auto *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#endif
  auto *view = new_test_view();
  g_object_ref_sink(view);
  gworld_scene_view_set_cache_enabled(view, FALSE);
  gworld_scene_view_set_texture_memory_budget_mib(view, 32);
  gworld_scene_view_set_camera(view, 0.5, 0.5, 1000);
  gworld_scene_view_set_camera_mode(view, GWORLD_SCENE_CAMERA_MODE_FREE);
  gworld_scene_view_set_camera_orientation(view, 0, -89);
  Supply source(256, 0x0000ffff);
  gworld_scene_view_set_tile_provider(view, source.provider);
  gtk_window_set_default_size(GTK_WINDOW(window), 256, 256);
#if GWORLD_SCENE_GTK_MAJOR == 4
  gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(view));
  gtk_window_present(GTK_WINDOW(window));
#else
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));
  gtk_widget_show_all(window);
#endif
  PresentedFrame presentation(GTK_GL_AREA(view));
  g_assert_true(wait([&] { return gworld_scene_view_get_imagery_ready(view) &&
    presentation.colors[2] > 2000; }));
  g_assert_cmpuint(gworld_scene_tile_provider_get_annotation_count(source.provider), ==, 1);
  // Do not iterate between changing modes and unmapping: the last external
  // frame is still presented, but the view is already in native URL mode.
  gworld_scene_view_set_map_tile_url_template(view, "http://127.0.0.1:9/{z}/{x}/{y}");
  gtk_widget_set_visible(GTK_WIDGET(view), FALSE);
  source.close();
  g_assert_true(wait([&] { return source.drained == 1; }, 2));
  g_assert_cmpuint(source.annotation_releases, ==, 1);
  g_assert_cmpuint(source.released.size(), ==, source.requests.size());
  g_assert_cmpuint(gworld_scene_tile_provider_get_held_count(source.provider), ==, 0);
  g_assert_cmpuint(gworld_scene_tile_provider_get_annotation_count(source.provider), ==, 0);
#if GWORLD_SCENE_GTK_MAJOR == 4
  gtk_window_destroy(GTK_WINDOW(window));
#else
  gtk_widget_destroy(window);
#endif
  g_object_unref(view);
  pump_for(100);
}

void test_empty_presentation() {
  if (!init_display()) return;
  // An unmapped widget has an empty paintable, not a render node to download.
  auto *view = new_test_view();
  g_object_ref_sink(view);
  g_assert_cmpint(presented_colored(GTK_GL_AREA(view), 1), ==, 0);
  g_object_unref(view);
}
}
int main(int argc,char **argv) {
  g_test_init(&argc,&argv,nullptr);
  g_test_add_func("/imagery/external-scene",test_scene);
  g_test_add_func("/imagery/legacy-unmap",test_legacy_unmap);
  g_test_add_func("/imagery/empty-presentation",test_empty_presentation);
  return g_test_run();
}
