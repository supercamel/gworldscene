#include "gworld-scene-tile-provider-private.h"
#include <cmath>
#include <algorithm>
#include <map>
#include <set>
#include <thread>
#include <tuple>
#include <functional>
using namespace gworld_scene;
namespace {
struct Probe {
  GThread *thread=g_thread_self();
  std::map<guint64,ImageryTile> requested;
  std::set<guint64> released;
  std::vector<std::string> annotations;
  unsigned drained=0;
  GdkPixbuf *auto_image=nullptr;
  bool close_on_request=false;
  void connect(GWorldSceneTileProvider *p) {
    g_signal_connect(p,"annotation-released",G_CALLBACK(+[](GWorldSceneTileProvider *,const char *token,gpointer data) {
      auto &s=*static_cast<Probe *>(data);g_assert_true(g_thread_self()==s.thread);s.annotations.emplace_back(token);
    }),this);
    g_signal_connect(p,"tile-requested",G_CALLBACK(+[](GWorldSceneTileProvider *p,guint64 id,int z,int x,int y,gpointer data) {
      auto &s=*static_cast<Probe *>(data);g_assert_true(g_thread_self()==s.thread);
      g_assert_true(s.requested.emplace(id,ImageryTile{z,x,y}).second);
      if(s.close_on_request)gworld_scene_tile_provider_close(p);
      else if(s.auto_image)g_assert_true(gworld_scene_tile_provider_complete_tile(p,id,s.auto_image));
    }),this);
    g_signal_connect(p,"tile-released",G_CALLBACK(+[](GWorldSceneTileProvider *,guint64 id,gpointer data) {
      auto &s=*static_cast<Probe *>(data);g_assert_true(g_thread_self()==s.thread);
      g_assert_true(s.requested.count(id)==1);g_assert_true(s.released.insert(id).second);
    }),this);
    g_signal_connect(p,"drained",G_CALLBACK(+[](GWorldSceneTileProvider *,gpointer data) {
      auto &s=*static_cast<Probe *>(data);g_assert_true(g_thread_self()==s.thread);s.drained++;
    }),this);
  }
};
void pump(GMainContext *context=nullptr) { for(int i=0;i<10000 && g_main_context_pending(context);i++)g_main_context_iteration(context,FALSE); }
std::vector<ImageryTile> grid(int count) { std::vector<ImageryTile> v;for(int i=0;i<count;i++)v.push_back({6,i%64,i/64});return v; }
void lifecycle() {
  g_assert_null(gworld_scene_tile_provider_new(-1,22,256));
  g_assert_null(gworld_scene_tile_provider_new(4,3,256));
  g_assert_null(gworld_scene_tile_provider_new(0,23,256));
  g_assert_null(gworld_scene_tile_provider_new(0,22,128));
  g_autoptr(GWorldSceneTileProvider) p=gworld_scene_tile_provider_new(0,22,256);Probe s;s.connect(p);
  imagery_update(p,grid(40));g_assert_true(s.requested.empty());pump();
  g_assert_cmpuint(s.requested.size(),==,16);
  auto id=s.requested.begin()->first;
  g_autoptr(GdkPixbuf) wrong=gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,512,512);
  g_assert_false(gworld_scene_tile_provider_complete_tile(p,id,wrong));
  g_assert_false(gworld_scene_tile_provider_complete_tile(p,id,nullptr));
  g_assert_cmpuint(gworld_scene_tile_provider_get_failed_count(p),==,1);
  pump();g_assert_cmpuint(s.requested.size(),==,17);
  gworld_scene_tile_provider_clear(p);
  g_assert_cmpuint(s.released.size(),==,17);g_assert_cmpuint(gworld_scene_tile_provider_get_held_count(p),==,0);
  g_assert_false(gworld_scene_tile_provider_complete_tile(p,id,nullptr));
  imagery_update(p,{{2,-1,1},{2,3,1},{2,0,-1},{23,0,0},{2,0,4}});pump();
  g_assert_cmpuint(gworld_scene_tile_provider_get_tile_count(p),==,1);
  g_assert_cmpuint(s.requested.rbegin()->first,>,id);
  g_assert_cmpint(s.requested.rbegin()->second.x,==,3);
  gworld_scene_tile_provider_close(p);gworld_scene_tile_provider_close(p);
  imagery_update(p,grid(40));pump();g_assert_cmpuint(s.drained,==,1);
  g_assert_cmpuint(s.requested.size(),==,s.released.size());
}
void async_release() {
  GMainContext *context=g_main_context_new();g_main_context_push_thread_default(context);
  auto *p=gworld_scene_tile_provider_new(0,22,256);Probe s;s.connect(p);
  auto *pixels=gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,256,256);gdk_pixbuf_fill(pixels,0xff0000ff);
  bool pixels_gone=false,provider_gone=false;
  g_object_weak_ref(G_OBJECT(pixels),+[](gpointer data,GObject *){*static_cast<bool *>(data)=true;},&pixels_gone);
  g_object_weak_ref(G_OBJECT(p),+[](gpointer data,GObject *){*static_cast<bool *>(data)=true;},&provider_gone);
  imagery_update(p,{{0,0,0}});pump(context);
  g_assert_true(gworld_scene_tile_provider_complete_tile(p,s.requested.begin()->first,pixels));g_object_unref(pixels);
  auto one=imagery_snapshot(p),two=imagery_snapshot(p);
  g_assert_cmpuint(gworld_scene_tile_provider_get_worker_count(p),==,2);
  gworld_scene_tile_provider_close(p);g_assert_true(s.released.empty());g_assert_cmpuint(s.drained,==,0);
  g_object_unref(p);g_assert_false(provider_gone);
  std::thread worker([a=std::move(one),b=std::move(two)]() mutable {
    std::vector<unsigned char> rgba;g_assert_true(imagery_sample(a,14,16383,16383,rgba));
    g_assert_cmpuint(rgba[0],==,255);a.clear();b.clear();
  });worker.join();
  // Even an idle owner context must never execute release inline on the worker.
  g_assert_false(pixels_gone);g_assert_true(s.released.empty());g_assert_false(provider_gone);
  pump(context);g_assert_true(pixels_gone);g_assert_true(provider_gone);
  g_assert_cmpuint(s.released.size(),==,1);g_assert_cmpuint(s.drained,==,1);
  g_main_context_pop_thread_default(context);g_main_context_unref(context);
}
void bound_and_zoom() {
  g_autoptr(GWorldSceneTileProvider) p=gworld_scene_tile_provider_new(0,22,256);
  g_autoptr(GdkPixbuf) pixels=gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,256,256);
  gdk_pixbuf_fill(pixels,0x123456ff);Probe s;s.auto_image=pixels;s.connect(p);
  imagery_update(p,grid(1100));pump();
  g_assert_cmpuint(gworld_scene_tile_provider_get_tile_count(p),==,1024);
  g_assert_cmpuint(gworld_scene_tile_provider_get_overflow_count(p),==,76);
  auto snapshot=imagery_snapshot(p);gworld_scene_tile_provider_clear(p);
  imagery_update(p,{{0,0,0}});pump();
  g_assert_cmpuint(gworld_scene_tile_provider_get_tile_count(p),==,0);
  g_assert_cmpuint(gworld_scene_tile_provider_get_held_count(p),==,1024);
  g_assert_cmpuint(gworld_scene_tile_provider_get_overflow_count(p),==,1);
  snapshot.clear();pump();imagery_update(p,{{0,0,0}});pump();
  g_assert_cmpuint(gworld_scene_tile_provider_get_ready_count(p),==,1);
  gworld_scene_tile_provider_close(p);pump();g_assert_cmpuint(s.drained,==,1);
  g_autoptr(GWorldSceneTileProvider) limited=gworld_scene_tile_provider_new(3,5,256);Probe t;t.connect(limited);
  imagery_update(limited,{{2,1,1},{8,50,60},{8,51,61}});pump();
  g_assert_cmpuint(gworld_scene_tile_provider_get_unsupported_count(limited),==,1);
  g_assert_cmpuint(gworld_scene_tile_provider_get_tile_count(limited),==,1);
  g_assert_true(gworld_scene_tile_provider_get_reduced_detail(limited));
  g_assert_cmpint(t.requested.begin()->second.z,==,5);g_assert_cmpint(t.requested.begin()->second.x,==,6);
  g_assert_cmpint(t.requested.begin()->second.y,==,7);gworld_scene_tile_provider_close(limited);
}
void sampling() {
  for(int size:{256,512})for(bool alpha:{false,true}) {
    g_autoptr(GWorldSceneTileProvider) p=gworld_scene_tile_provider_new(0,22,size);Probe s;s.connect(p);
    g_autoptr(GdkPixbuf) pixels=gdk_pixbuf_new(GDK_COLORSPACE_RGB,alpha,8,size,size);
    int stride=gdk_pixbuf_get_rowstride(pixels),channels=alpha?4:3;auto *bytes=gdk_pixbuf_get_pixels(pixels);
    // Four distinct world quadrants, plus exact coordinate ramps within them.
    for(int y=0;y<size;y++)for(int x=0;x<size;x++) {
      auto *v=bytes+y*stride+x*channels;v[0]=x*256/size;v[1]=y*256/size;v[2]=123;if(alpha)v[3]=255;
    }
    imagery_update(p,{{0,0,0}});pump();g_assert_true(gworld_scene_tile_provider_complete_tile(p,s.requested.begin()->first,pixels));
    auto snapshot=imagery_snapshot(p);std::vector<unsigned char> rgba;
    for(int gap:{1,8,9,14})for(int bottom:{0,1})for(int right:{0,1}) {
      int n=1<<gap,x=right?n-1:0,y=bottom?n-1:0;
      g_assert_true(imagery_sample(snapshot,gap,x+n,y,rgba));
      int width=std::max(1,size>>gap),left=int(int64_t(x)*size/n),top=int(int64_t(y)*size/n);
      double center=std::clamp(128.5*width/256-0.5,0.0,double(width-1));
      g_assert_cmpint(std::abs(int(rgba[(128*256+128)*4])-(left+center)*256/size),<=,1);
      g_assert_cmpint(std::abs(int(rgba[(128*256+128)*4+1])-(top+center)*256/size),<=,1);
      g_assert_cmpint(rgba[(128*256+128)*4+2],==,123);
    }
    // Exact child wins over earlier coarse entry in the snapshot.
    imagery_update(p,{{0,0,0},{1,0,0}});pump();
    g_autoptr(GdkPixbuf) fine=gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,size,size);gdk_pixbuf_fill(fine,0xff000080);
    g_assert_true(gworld_scene_tile_provider_complete_tile(p,s.requested.rbegin()->first,fine));
    auto layered=imagery_snapshot(p);g_assert_true(imagery_sample(layered,1,0,0,rgba));
    g_assert_cmpint(std::abs(int(rgba[0])-188),<=,1);g_assert_cmpint(rgba[1],==,0);g_assert_cmpint(rgba[3],==,128);
    g_assert_false(imagery_sample(layered,3,0,-1,rgba));
    gworld_scene_tile_provider_close(p);snapshot.clear();layered.clear();pump();
  }
  // A 512 image downsample crosses opaque red / invisible green: linear red
  // stays saturated after unpremultiplication and invisible RGB contributes zero.
  g_autoptr(GWorldSceneTileProvider) p=gworld_scene_tile_provider_new(0,0,512);Probe s;s.connect(p);
  g_autoptr(GdkPixbuf) pixels=gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,512,512);
  auto *bytes=gdk_pixbuf_get_pixels(pixels);int stride=gdk_pixbuf_get_rowstride(pixels);
  for(int y=0;y<512;y++)for(int x=0;x<512;x++){auto *v=bytes+y*stride+x*4;v[0]=x%2?0:255;v[1]=x%2?255:0;v[2]=0;v[3]=x%2?0:255;}
  imagery_update(p,{{0,0,0}});pump();gworld_scene_tile_provider_complete_tile(p,s.requested.begin()->first,pixels);
  auto snapshot=imagery_snapshot(p);std::vector<unsigned char> rgba;g_assert_true(imagery_sample(snapshot,0,0,0,rgba));
  g_assert_cmpint(std::abs(int(rgba[0])-188),<=,1);g_assert_cmpint(rgba[1],==,0);g_assert_cmpint(rgba[3],==,128);
  gworld_scene_tile_provider_close(p);snapshot.clear();pump();
}
void reentrant() {
  g_autoptr(GWorldSceneTileProvider) p=gworld_scene_tile_provider_new(0,22,256);Probe s;s.close_on_request=true;s.connect(p);
  imagery_update(p,grid(40));pump();g_assert_cmpuint(s.requested.size(),==,1);g_assert_cmpuint(s.released.size(),==,1);g_assert_cmpuint(s.drained,==,1);
}
void annotations() {
  GMainContext *context=g_main_context_new();g_main_context_push_thread_default(context);
  auto *p=gworld_scene_tile_provider_new(0,22,256);Probe s;s.connect(p);
  auto *pixels=gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,256,256);gdk_pixbuf_fill(pixels,0xabcdefFF);
  bool pixels_gone=false,provider_gone=false;
  g_object_weak_ref(G_OBJECT(pixels),+[](gpointer d,GObject *){*static_cast<bool *>(d)=true;},&pixels_gone);
  g_object_weak_ref(G_OBJECT(p),+[](gpointer d,GObject *){*static_cast<bool *>(d)=true;},&provider_gone);
  imagery_update(p,{{2,0,0},{2,1,0}});pump(context);
  for(auto &request:s.requested)
    g_assert_true(gworld_scene_tile_provider_complete_annotated(p,request.first,pixels,1,"scope-1"));
  g_object_unref(pixels);
  g_assert_cmpuint(gworld_scene_tile_provider_get_annotation_count(p),==,1);
  auto snapshot=imagery_snapshot(p);ImageryAnnotations atlas;
  std::thread worker([&](){
    for(int x:{0,1}) {
      std::vector<unsigned char> rgba;std::shared_ptr<ImageryAnnotation> tag;
      g_assert_true(imagery_sample(snapshot,2,x,0,rgba,&tag));g_assert_nonnull(tag.get());atlas.push_back(std::move(tag));
    }
    snapshot.clear();
  });worker.join();
  gworld_scene_tile_provider_close(p);g_object_unref(p);pump(context);
  // Copied atlas annotations retain neither the original image nor its lease.
  g_assert_true(pixels_gone);g_assert_cmpuint(s.released.size(),==,2);
  g_assert_true(s.annotations.empty());g_assert_cmpuint(s.drained,==,0);g_assert_false(provider_gone);
  atlas.pop_back();pump(context);g_assert_true(s.annotations.empty());
  std::thread final_worker([&](){atlas.clear();});final_worker.join();
  pump();g_assert_true(s.annotations.empty()); // never delivered on another context
  pump(context);g_assert_cmpuint(s.annotations.size(),==,1);g_assert_cmpstr(s.annotations[0].c_str(),==,"scope-1");
  g_assert_cmpuint(s.drained,==,1);g_assert_true(provider_gone);
  g_main_context_pop_thread_default(context);g_main_context_unref(context);
}
void scoped_parent() {
  g_autoptr(GWorldSceneTileProvider) p=gworld_scene_tile_provider_new(0,22,256);Probe s;s.connect(p);
  g_autoptr(GdkPixbuf) pixels=gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,256,256);
  int stride=gdk_pixbuf_get_rowstride(pixels);auto *bytes=gdk_pixbuf_get_pixels(pixels);
  for(int y=0;y<256;y++)for(int x=0;x<256;x++) {
    auto *v=bytes+y*stride+x*4;v[0]=x;v[1]=y;v[2]=123;v[3]=255;
  }
  imagery_update(p,{{2,3,1}});pump();
  g_assert_true(gworld_scene_tile_provider_complete_annotated(p,s.requested.begin()->first,pixels,0,"regional-credit"));
  g_assert_true(gworld_scene_tile_provider_get_reduced_detail(p));
  auto snapshot=imagery_snapshot(p);std::vector<unsigned char> rgba;std::shared_ptr<ImageryAnnotation> tag;
  g_assert_true(imagery_sample(snapshot,2,3,1,rgba,&tag));
  g_assert_cmpint(std::abs(int(rgba[(128*256+128)*4])-224),<=,1);
  g_assert_cmpint(std::abs(int(rgba[(128*256+128)*4+1])-96),<=,1);
  g_assert_cmpstr(tag->token.c_str(),==,"regional-credit");
  g_assert_true(imagery_sample(snapshot,14,16383,8191,rgba,&tag));
  g_assert_cmpint(rgba[0],==,255);g_assert_cmpint(rgba[1],==,127);
  // A world image approved for one requested cell cannot fill another cell.
  g_assert_false(imagery_sample(snapshot,2,2,1,rgba,&tag));g_assert_null(tag.get());
  g_assert_false(imagery_sample(snapshot,1,1,0,rgba,&tag));
  g_assert_false(imagery_sample(snapshot,3,7,4,rgba,&tag));
  gworld_scene_tile_provider_close(p);snapshot.clear();pump();g_assert_cmpuint(s.annotations.size(),==,1);
  for(const auto &bad:std::vector<std::pair<int,std::string>>{{-1,"id"},{3,"id"},{0,""},{0,"has space"},{0,"bad\nline"},{0,std::string(129,'a')}}) {
    g_autoptr(GWorldSceneTileProvider) q=gworld_scene_tile_provider_new(0,22,256);Probe t;t.connect(q);
    imagery_update(q,{{2,3,1}});pump();
    g_assert_false(gworld_scene_tile_provider_complete_annotated(q,t.requested.begin()->first,pixels,bad.first,bad.second.c_str()));
    g_assert_cmpuint(gworld_scene_tile_provider_get_failed_count(q),==,1);g_assert_cmpuint(gworld_scene_tile_provider_get_annotation_count(q),==,0);
    gworld_scene_tile_provider_close(q);pump();g_assert_true(t.annotations.empty());
  }
}
void demand() {
  g_autoptr(GWorldSceneTileProvider) p=gworld_scene_tile_provider_new(0,22,256);Probe s;s.connect(p);
  struct Demand { guint64 revision=0;bool close=false; } d;
  g_signal_connect(p,"demand-changed",G_CALLBACK(+[](GWorldSceneTileProvider *p,guint64 revision,gpointer data){
    auto &d=*static_cast<Demand *>(data);g_assert_cmpuint(revision,>,d.revision);d.revision=revision;
    if(d.close)gworld_scene_tile_provider_close(p);
  }),&d);
  imagery_update(p,grid(40));g_assert_cmpuint(d.revision,==,1);
  g_auto(GStrv) all=gworld_scene_tile_provider_dup_demand(p);g_assert_cmpuint(g_strv_length(all),==,40);
  g_assert_cmpstr(all[0],==,"6/0/0");g_assert_cmpstr(all[39],==,"6/39/0");
  pump();g_assert_cmpuint(s.requested.size(),==,16);g_assert_cmpuint(d.revision,==,1);
  imagery_update(p,grid(40));g_assert_cmpuint(d.revision,==,1);
  imagery_update(p,{{2,-1,1},{2,3,1}});g_assert_cmpuint(d.revision,==,2);
  g_auto(GStrv) wrapped=gworld_scene_tile_provider_dup_demand(p);
  g_assert_cmpuint(g_strv_length(wrapped),==,1);g_assert_cmpstr(wrapped[0],==,"2/3/1");
  imagery_update(p,{{2,0,1}});g_assert_cmpuint(d.revision,==,3); // same count, different footprint
  d.close=true;imagery_update(p,{{2,1,1}});pump();
  g_assert_cmpuint(d.revision,==,5);g_assert_cmpuint(s.drained,==,1);
  g_auto(GStrv) empty=gworld_scene_tile_provider_dup_demand(p);g_assert_cmpuint(g_strv_length(empty),==,0);
}

void continuous_frames() {
  // A continuously ready GTK-priority source must not starve tile delivery or
  // destruction notifications. Use a private context to also check affinity.
  GMainContext *context = g_main_context_new();
  g_main_context_push_thread_default(context);
  auto *provider = gworld_scene_tile_provider_new(0, 22, 256);
  Probe probe;
  probe.connect(provider);
  GSource *frames = g_idle_source_new();
  g_source_set_priority(frames, G_PRIORITY_HIGH_IDLE);
  guint frame_count = 0;
  g_source_set_callback(frames, +[](gpointer data) -> gboolean {
    ++*static_cast<guint *>(data);
    return G_SOURCE_CONTINUE;
  }, &frame_count, nullptr);
  g_source_attach(frames, context);
  auto until = [&](const std::function<bool()> &ready) {
    const gint64 deadline = g_get_monotonic_time() + G_USEC_PER_SEC;
    while (!ready() && g_get_monotonic_time() < deadline) {
      g_main_context_iteration(context, FALSE);
      g_usleep(1000);
    }
    return ready();
  };

  g_assert_true(until([&] { return frame_count > 0; }));
  imagery_update(provider, {{0, 0, 0}});
  g_assert_true(probe.requested.empty()); // dispatch remains asynchronous
  pump(); // another context cannot dispatch this provider
  g_assert_true(probe.requested.empty());
  g_assert_true(until([&] { return probe.requested.size() == 1; }));
  auto *pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 256, 256);
  gdk_pixbuf_fill(pixels, 0x123456ff);
  g_assert_true(gworld_scene_tile_provider_complete_annotated(provider,
    probe.requested.begin()->first, pixels, 0, "busy-credit"));
  g_object_unref(pixels);
  auto snapshot = imagery_snapshot(provider);
  ImageryAnnotations atlas{snapshot.front()->annotation};
  gworld_scene_tile_provider_close(provider);
  std::thread worker([leases = std::move(snapshot)]() mutable { leases.clear(); });
  worker.join();
  g_assert_true(probe.released.empty()); // never called inline on the worker
  pump();
  g_assert_true(probe.released.empty());
  g_assert_true(until([&] { return probe.released.size() == 1; }));
  g_assert_cmpuint(gworld_scene_tile_provider_get_held_count(provider), ==, 0);
  g_assert_true(probe.annotations.empty());
  g_assert_cmpuint(probe.drained, ==, 0);

  std::thread annotation_worker([tags = std::move(atlas)]() mutable { tags.clear(); });
  annotation_worker.join();
  g_assert_true(probe.annotations.empty());
  g_assert_true(until([&] { return probe.drained == 1; }));
  g_assert_cmpuint(probe.annotations.size(), ==, 1);
  g_assert_cmpstr(probe.annotations.front().c_str(), ==, "busy-credit");
  g_assert_cmpuint(gworld_scene_tile_provider_get_annotation_count(provider), ==, 0);
  g_source_destroy(frames);
  g_source_unref(frames);
  g_object_unref(provider);
  g_main_context_pop_thread_default(context);
  g_main_context_unref(context);
}
}
int main(int argc,char **argv) {
  g_test_init(&argc,&argv,nullptr);
  g_test_add_func("/provider/lifecycle",lifecycle);g_test_add_func("/provider/worker-release",async_release);
  g_test_add_func("/provider/bounds-zoom",bound_and_zoom);g_test_add_func("/provider/sampling",sampling);
  g_test_add_func("/provider/reentrant-close",reentrant);
  g_test_add_func("/provider/annotation-lifetime",annotations);g_test_add_func("/provider/scoped-parent",scoped_parent);
  g_test_add_func("/provider/demand",demand);
  g_test_add_func("/provider/continuous-frames",continuous_frames);
  return g_test_run();
}
