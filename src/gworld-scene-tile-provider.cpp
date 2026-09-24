#include "gworld-scene-tile-provider-private.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>

namespace {
using Key = std::tuple<int,int,int>;
struct Entry {
  guint64 id = 0;
  Key key;
  GdkPixbuf *image = nullptr;
  int image_zoom = 0;
  std::shared_ptr<gworld_scene::ImageryAnnotation> annotation;
  guint workers = 0;
  bool active = true, sent = false, finished = false;
  ~Entry() { if(image)g_object_unref(image); }
};
struct State {
  int minimum = 0, maximum = 22, size = 256;
  GMainContext *context = g_main_context_ref_thread_default();
  GThread *thread = g_thread_self();
  guint dispatch = 0, overflow = 0, unsupported = 0;
  guint64 serial = 0, epoch = 0;
  bool closed = false, drained = false, reduced = false;
  std::map<guint64,std::unique_ptr<Entry>> entries;
  std::map<Key,guint64> active;
  std::map<std::string,guint> annotations;
  std::string demand;
  guint64 demand_revision = 0;
  std::string coverage;
  ~State() { g_main_context_unref(context); }
};
enum { REQUESTED, RELEASED, CHANGED, COVERAGE, DRAINED, ANNOTATION_RELEASED, DEMAND_CHANGED, N_SIGNALS };
guint signals[N_SIGNALS];
}

struct _GWorldSceneTileProvider { GObject parent_instance; State *state; };
G_DEFINE_TYPE(GWorldSceneTileProvider, gworld_scene_tile_provider, G_TYPE_OBJECT)

static bool on_thread(GWorldSceneTileProvider *self) { return GWORLD_IS_SCENE_TILE_PROVIDER(self) && self->state->thread == g_thread_self(); }
static guint queue_on_context(GMainContext *context, GSourceFunc callback,
                              gpointer data, GDestroyNotify destroy)
{
  // Stay asynchronous even when the caller owns the context or holds a render
  // lock. Normal priority prevents continuous GTK frames from starving requests
  // and resource releases; request dispatch is separately bounded to 16 tiles.
  GSource *source = g_idle_source_new();
  g_source_set_priority(source, G_PRIORITY_DEFAULT);
  g_source_set_callback(source, callback, data, destroy);
  const guint id = g_source_attach(source, context);
  g_source_unref(source);
  return id;
}
static void schedule(GWorldSceneTileProvider *self);
static void announce(GWorldSceneTileProvider *self)
{
  auto *s=self->state;
  std::string value=std::to_string(s->active.size())+"/"+std::to_string(gworld_scene_tile_provider_get_ready_count(self))+"/"+
    std::to_string(gworld_scene_tile_provider_get_failed_count(self))+"/"+std::to_string(s->overflow)+"/"+
    std::to_string(s->unsupported)+"/"+std::to_string(gworld_scene_tile_provider_get_reduced_detail(self))+"/"+std::to_string(s->entries.size());
  if(value!=s->coverage) { s->coverage=value; g_signal_emit(self,signals[COVERAGE],0); }
  std::string demand;
  for(const auto &item:s->active)
    demand+=std::to_string(std::get<0>(item.first))+"/"+std::to_string(std::get<1>(item.first))+"/"+std::to_string(std::get<2>(item.first))+";";
  if(demand!=s->demand) { s->demand=demand; g_signal_emit(self,signals[DEMAND_CHANGED],0,++s->demand_revision); }
  if(s->closed && s->entries.empty() && s->annotations.empty() && !s->drained) { s->drained=true; g_signal_emit(self,signals[DRAINED],0); }
}
static void retire(GWorldSceneTileProvider *self, guint64 id)
{
  auto *s=self->state;
  auto it=s->entries.find(id);if(it==s->entries.end())return;
  auto &entry=*it->second;
  entry.active=false;
  auto active=s->active.find(entry.key);if(active!=s->active.end() && active->second==id)s->active.erase(active);
  // Workers own their own native reference; keep the record until their release
  // callback so the application still accounts the underlying pixels.
  g_clear_object(&entry.image);
  entry.annotation.reset();
  if(entry.workers==0) {
    const bool sent=entry.sent;s->entries.erase(it);
    if(sent)g_signal_emit(self,signals[RELEASED],0,id);
  }
}
static gboolean dispatch(gpointer data)
{
  auto *self=GWORLD_SCENE_TILE_PROVIDER(data);auto *s=self->state;s->dispatch=0;
  std::vector<guint64> pending;guint live=0;
  for(const auto &item:s->entries)if(item.second->active) {
    if(item.second->sent && !item.second->finished)live++;
    else if(!item.second->sent)pending.push_back(item.first);
  }
  for(auto id:pending) {
    if(s->closed || live>=16)break;
    auto it=s->entries.find(id);if(it==s->entries.end() || !it->second->active || it->second->sent)continue;
    auto &e=*it->second;e.sent=true;live++;
    auto key=e.key;
    g_signal_emit(self,signals[REQUESTED],0,id,std::get<0>(key),std::get<1>(key),std::get<2>(key));
  }
  announce(self);return G_SOURCE_REMOVE;
}
static void schedule(GWorldSceneTileProvider *self)
{
  auto *s=self->state;if(s->closed || s->dispatch)return;
  s->dispatch=queue_on_context(s->context,dispatch,g_object_ref(self),g_object_unref);
}
static void stop_dispatch(GWorldSceneTileProvider *self)
{
  auto *s=self->state;
  if(s->dispatch) { GSource *source=g_main_context_find_source_by_id(s->context,s->dispatch);s->dispatch=0;if(source)g_source_destroy(source); }
}

void gworld_scene_tile_provider_clear(GWorldSceneTileProvider *self)
{
  g_return_if_fail(on_thread(self));auto *s=self->state;stop_dispatch(self);s->epoch++;
  std::vector<guint64> old;for(const auto &item:s->entries)if(item.second->active)old.push_back(item.first);
  for(auto id:old)retire(self,id);
  s->overflow=0;s->unsupported=0;s->reduced=false;
  g_signal_emit(self,signals[CHANGED],0);announce(self);
}
void gworld_scene_tile_provider_close(GWorldSceneTileProvider *self)
{
  g_return_if_fail(on_thread(self));if(self->state->closed)return;
  self->state->closed=true;gworld_scene_tile_provider_clear(self);
}
static gboolean complete(GWorldSceneTileProvider *self,guint64 id,GdkPixbuf *image,int source_zoom,const gchar *annotation)
{
  g_return_val_if_fail(on_thread(self),FALSE);auto *s=self->state;
  auto it=s->entries.find(id);
  if(s->closed || it==s->entries.end() || !it->second->active || !it->second->sent || it->second->finished)return FALSE;
  auto &entry=*it->second;
  const int requested=std::get<0>(entry.key);
  if(source_zoom==-1)source_zoom=requested;
  bool valid=source_zoom>=s->minimum && source_zoom<=requested;
  if(annotation!=nullptr) {
    const auto length=strnlen(annotation,129);valid&=length>0 && length<=128;
    for(std::size_t i=0;i<length && valid;i++)valid=annotation[i]>=33 && annotation[i]<=126;
  }
  valid&=image==nullptr || (GDK_IS_PIXBUF(image) && gdk_pixbuf_get_width(image)==s->size && gdk_pixbuf_get_height(image)==s->size &&
    gdk_pixbuf_get_colorspace(image)==GDK_COLORSPACE_RGB && gdk_pixbuf_get_bits_per_sample(image)==8);
  entry.finished=true;
  if(image && valid) {
    entry.image=GDK_PIXBUF(g_object_ref(image));entry.image_zoom=source_zoom;
    if(annotation!=nullptr) {
      auto tag=std::shared_ptr<gworld_scene::ImageryAnnotation>(new gworld_scene::ImageryAnnotation);
      tag->provider=GWORLD_SCENE_TILE_PROVIDER(g_object_ref(self));tag->token=annotation;
      s->annotations[tag->token]++;entry.annotation=std::move(tag);
    }
  }
  g_signal_emit(self,signals[CHANGED],0);announce(self);schedule(self);return valid;
}
gboolean gworld_scene_tile_provider_complete_tile(GWorldSceneTileProvider *self,guint64 id,GdkPixbuf *image)
{
  return complete(self,id,image,-1,nullptr);
}
gboolean gworld_scene_tile_provider_complete_annotated(GWorldSceneTileProvider *self,guint64 id,GdkPixbuf *image,gint source_zoom,const gchar *annotation)
{
  // -1 is reserved for the original exact-level API, never an external value.
  return complete(self,id,image,source_zoom<0?-2:source_zoom,annotation);
}
gchar **gworld_scene_tile_provider_dup_demand(GWorldSceneTileProvider *self)
{
  g_return_val_if_fail(on_thread(self),nullptr);
  auto **out=g_new0(gchar *,self->state->active.size()+1);guint index=0;
  for(const auto &item:self->state->active)
    out[index++]=g_strdup_printf("%d/%d/%d",std::get<0>(item.first),std::get<1>(item.first),std::get<2>(item.first));
  return out;
}
guint gworld_scene_tile_provider_get_annotation_count(GWorldSceneTileProvider *self)
{
  g_return_val_if_fail(on_thread(self),0);return self->state->annotations.size();
}

namespace gworld_scene {
struct AnnotationRelease { GWorldSceneTileProvider *provider; std::string token; };
static gboolean annotation_release_on_context(gpointer data)
{
  auto *r=static_cast<AnnotationRelease *>(data);auto *s=r->provider->state;
  auto it=s->annotations.find(r->token);g_assert(it!=s->annotations.end() && it->second>0);
  if(--it->second==0) { s->annotations.erase(it);g_signal_emit(r->provider,signals[ANNOTATION_RELEASED],0,r->token.c_str()); }
  announce(r->provider);return G_SOURCE_REMOVE;
}
static void annotation_release_free(gpointer data)
{
  auto *r=static_cast<AnnotationRelease *>(data);g_object_unref(r->provider);delete r;
}
ImageryAnnotation::~ImageryAnnotation()
{
  auto *r=new AnnotationRelease{provider,std::move(token)};
  queue_on_context(provider->state->context,annotation_release_on_context,r,annotation_release_free);
}
void imagery_update(GWorldSceneTileProvider *self,const std::vector<ImageryTile> &wanted)
{
  g_return_if_fail(on_thread(self));auto *s=self->state;if(s->closed)return;
  std::set<Key> keys;std::vector<Key> ordered;s->reduced=false;s->overflow=0;s->unsupported=0;
  for(auto tile:wanted) {
    if(tile.z<0 || tile.z>22 || tile.y<0 || tile.y>=(1<<tile.z))continue;
    if(tile.z<s->minimum) { s->unsupported++;continue; }
    const int level=std::min(tile.z,s->maximum),gap=tile.z-level,n=1<<tile.z;
    tile.x=((tile.x%n)+n)%n;Key key{level,tile.x>>gap,tile.y>>gap};s->reduced|=gap>0;
    if(keys.insert(key).second)ordered.push_back(key);
  }
  std::vector<guint64> obsolete;for(const auto &item:s->active)if(!keys.count(item.first))obsolete.push_back(item.second);
  const auto epoch=s->epoch;for(auto id:obsolete)retire(self,id);
  if(s->closed || s->epoch!=epoch)return;
  for(const auto &key:ordered) {
    if(s->active.count(key))continue;
    // Retired worker-held records also consume the bound.
    if(s->entries.size()>=1024) { s->overflow++;continue; }
    auto entry=std::make_unique<Entry>();entry->key=key;entry->id=++s->serial;
    s->active[key]=entry->id;s->entries.emplace(entry->id,std::move(entry));
  }
  schedule(self);announce(self);
}
struct Release { GWorldSceneTileProvider *provider; GdkPixbuf *image; guint64 id; };
static gboolean release_on_context(gpointer data)
{
  auto *release=static_cast<Release *>(data);auto *self=release->provider;auto *s=self->state;
  // Drop the worker's actual pixel reference before releasing the cache lease.
  g_clear_object(&release->image);
  auto it=s->entries.find(release->id);
  if(it!=s->entries.end()) {
    g_assert(it->second->workers>0);it->second->workers--;
    if(!it->second->active && it->second->workers==0)retire(self,release->id);
  }
  announce(self);return G_SOURCE_REMOVE;
}
static void release_free(gpointer data)
{
  auto *release=static_cast<Release *>(data);g_clear_object(&release->image);g_object_unref(release->provider);delete release;
}
ImageryLease::~ImageryLease()
{
  auto *release=new Release{provider,image,id};
  queue_on_context(provider->state->context,release_on_context,release,release_free);
}
ImagerySnapshot imagery_snapshot(GWorldSceneTileProvider *self)
{
  g_return_val_if_fail(on_thread(self),ImagerySnapshot());ImagerySnapshot result;
  for(auto &item:self->state->entries) {
    auto &e=*item.second;if(!e.active || !e.image)continue;e.workers++;
    // Direct allocation: an aggregate temporary would invoke the lease destructor.
    auto lease=std::shared_ptr<ImageryLease>(new ImageryLease);
    lease->provider=GWORLD_SCENE_TILE_PROVIDER(g_object_ref(self));lease->image=GDK_PIXBUF(g_object_ref(e.image));lease->id=e.id;
    std::tie(lease->coverage_z,lease->coverage_x,lease->coverage_y)=e.key;
    const int gap=lease->coverage_z-e.image_zoom;
    lease->z=e.image_zoom;lease->x=lease->coverage_x>>gap;lease->y=lease->coverage_y>>gap;
    lease->annotation=e.annotation;result.push_back(std::move(lease));
  }
  return result;
}
// sRGB storage, linear-light premultiplication/interpolation, matching the
// GL_SRGB8_ALPHA8 atlas path. Immutable lookup tables are shared by workers.
struct ColorTables {
  std::array<double,256> linear;
  std::array<unsigned char,65536> encoded;
  std::array<unsigned char,65536> premultiplied;
  ColorTables() {
    for(int i=0;i<256;i++) {
      double c=i/255.0;
      linear[i]=c<=0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4);
    }
    for(int i=0;i<65536;i++) {
      double c=i/65535.0;
      encoded[i]=static_cast<unsigned char>(std::lround(255*(c<=0.0031308 ? c*12.92 : 1.055*std::pow(c,1/2.4)-0.055)));
    }
    for(int c=0;c<256;c++)for(int a=0;a<256;a++)
      premultiplied[c*256+a]=encoded[std::lround(linear[c]*a/255*65535)];
  }
};
bool imagery_sample(const ImagerySnapshot &snapshot,int z,int x,int y,std::vector<unsigned char> &rgba,
                    std::shared_ptr<ImageryAnnotation> *annotation)
{
  if(annotation)annotation->reset();
  if(z<0 || z>22 || y<0 || y>=(1<<z))return false;
  const int n=1<<z;x=((x%n)+n)%n;
  const ImageryLease *best=nullptr;
  for(const auto &lease:snapshot) {
    if(lease->z>z || lease->coverage_z>z || (best && lease->z<=best->z))continue;
    const int coverage_gap=z-lease->coverage_z;
    if((x>>coverage_gap)!=lease->coverage_x || (y>>coverage_gap)!=lease->coverage_y)continue;
    const int gap=z-lease->z;
    if((x>>gap)==lease->x && (y>>gap)==lease->y)best=lease.get();
  }
  if(!best)return false;
  if(annotation!=nullptr)*annotation=best->annotation;
  static const ColorTables colors;
  const int gap=z-best->z,size=gdk_pixbuf_get_width(best->image),divisor=1<<gap;
  const int width=std::max(1,size>>gap),left=static_cast<int>((static_cast<int64_t>(x%divisor)*size)/divisor),
    top=static_cast<int>((static_cast<int64_t>(y%divisor)*size)/divisor);
  const int channels=gdk_pixbuf_get_n_channels(best->image),stride=gdk_pixbuf_get_rowstride(best->image);
  const guchar *pixels=gdk_pixbuf_read_pixels(best->image);rgba.resize(256*256*4);
  auto exact=[&](const guchar *p,unsigned char *out) {
    const int alpha=channels==4?p[3]:255;
    for(int c=0;c<3;c++)out[c]=alpha==255?p[c]:colors.premultiplied[p[c]*256+alpha];
    out[3]=alpha;
  };
  if(width==1) {
    exact(pixels+top*stride+left*channels,rgba.data());
    for(int i=1;i<256*256;i++)std::memcpy(rgba.data()+i*4,rgba.data(),4);
    return true;
  }
  if(width==256) {
    for(int py=0;py<256;py++)for(int px=0;px<256;px++)
      exact(pixels+(top+py)*stride+(left+px)*channels,rgba.data()+(py*256+px)*4);
    return true;
  }
  // Pixel-center bilinear sampling confined to the selected crop. Premultiply
  // before interpolation so transparent RGB cannot bleed into visible pixels.
  for(int py=0;py<256;py++)for(int px=0;px<256;px++) {
    const double sx=std::clamp((px+0.5)*width/256.0-0.5,0.0,double(width-1)),sy=std::clamp((py+0.5)*width/256.0-0.5,0.0,double(width-1));
    const int x0=static_cast<int>(sx),y0=static_cast<int>(sy),x1=std::min(x0+1,width-1),y1=std::min(y0+1,width-1);
    const double fx=sx-x0,fy=sy-y0;
    double color[4]={0,0,0,0};
    for(int corner=0;corner<4;corner++) {
      const int xx=(corner&1)?x1:x0,yy=(corner&2)?y1:y0;
      const double weight=((corner&1)?fx:1-fx)*((corner&2)?fy:1-fy);
      const guchar *p=pixels+(top+yy)*stride+(left+xx)*channels;const double a=channels==4?p[3]/255.0:1.0;
      for(int c=0;c<3;c++)color[c]+=weight*colors.linear[p[c]]*a;
      color[3]+=weight*a;
    }
    auto *out=rgba.data()+(py*256+px)*4;
    for(int c=0;c<3;c++)out[c]=colors.encoded[std::lround(std::clamp(color[c],0.0,1.0)*65535)];
    out[3]=static_cast<unsigned char>(std::lround(color[3]*255));
  }
  return true;
}
}

gint gworld_scene_tile_provider_get_min_zoom(GWorldSceneTileProvider *self){return self->state->minimum;}
gint gworld_scene_tile_provider_get_max_zoom(GWorldSceneTileProvider *self){return self->state->maximum;}
gint gworld_scene_tile_provider_get_tile_size(GWorldSceneTileProvider *self){return self->state->size;}
guint gworld_scene_tile_provider_get_tile_count(GWorldSceneTileProvider *self){return self->state->active.size();}
guint gworld_scene_tile_provider_get_ready_count(GWorldSceneTileProvider *self){guint n=0;for(const auto &v:self->state->entries)if(v.second->active && v.second->image)n++;return n;}
guint gworld_scene_tile_provider_get_failed_count(GWorldSceneTileProvider *self){guint n=0;for(const auto &v:self->state->entries)if(v.second->active && v.second->finished && !v.second->image)n++;return n;}
guint gworld_scene_tile_provider_get_held_count(GWorldSceneTileProvider *self){return self->state->entries.size();}
guint gworld_scene_tile_provider_get_worker_count(GWorldSceneTileProvider *self){guint n=0;for(const auto &v:self->state->entries)n+=v.second->workers;return n;}
guint gworld_scene_tile_provider_get_overflow_count(GWorldSceneTileProvider *self){return self->state->overflow;}
guint gworld_scene_tile_provider_get_unsupported_count(GWorldSceneTileProvider *self){return self->state->unsupported;}
gboolean gworld_scene_tile_provider_get_reduced_detail(GWorldSceneTileProvider *self){
  if(self->state->reduced)return TRUE;
  for(const auto &item:self->state->entries)
    if(item.second->active && item.second->image && item.second->image_zoom<std::get<0>(item.second->key))return TRUE;
  return FALSE;
}
gboolean gworld_scene_tile_provider_get_closed(GWorldSceneTileProvider *self){return self->state->closed;}

static void gworld_scene_tile_provider_finalize(GObject *object)
{
  auto *self=GWORLD_SCENE_TILE_PROVIDER(object);delete self->state;G_OBJECT_CLASS(gworld_scene_tile_provider_parent_class)->finalize(object);
}
static void gworld_scene_tile_provider_class_init(GWorldSceneTileProviderClass *klass)
{
  G_OBJECT_CLASS(klass)->finalize=gworld_scene_tile_provider_finalize;
  const GType type=G_TYPE_FROM_CLASS(klass);
  signals[REQUESTED]=g_signal_new("tile-requested",type,G_SIGNAL_RUN_LAST,0,nullptr,nullptr,nullptr,G_TYPE_NONE,4,G_TYPE_UINT64,G_TYPE_INT,G_TYPE_INT,G_TYPE_INT);
  signals[RELEASED]=g_signal_new("tile-released",type,G_SIGNAL_RUN_LAST,0,nullptr,nullptr,nullptr,G_TYPE_NONE,1,G_TYPE_UINT64);
  signals[CHANGED]=g_signal_new("changed",type,G_SIGNAL_RUN_LAST,0,nullptr,nullptr,nullptr,G_TYPE_NONE,0);
  signals[COVERAGE]=g_signal_new("coverage-changed",type,G_SIGNAL_RUN_LAST,0,nullptr,nullptr,nullptr,G_TYPE_NONE,0);
  signals[ANNOTATION_RELEASED]=g_signal_new("annotation-released",type,G_SIGNAL_RUN_LAST,0,nullptr,nullptr,nullptr,G_TYPE_NONE,1,G_TYPE_STRING);
  signals[DEMAND_CHANGED]=g_signal_new("demand-changed",type,G_SIGNAL_RUN_LAST,0,nullptr,nullptr,nullptr,G_TYPE_NONE,1,G_TYPE_UINT64);
  signals[DRAINED]=g_signal_new("drained",type,G_SIGNAL_RUN_LAST,0,nullptr,nullptr,nullptr,G_TYPE_NONE,0);
}
static void gworld_scene_tile_provider_init(GWorldSceneTileProvider *self){self->state=new State;}
GWorldSceneTileProvider *gworld_scene_tile_provider_new(gint minimum,gint maximum,gint size)
{
  if(minimum<0 || maximum<minimum || maximum>22 || (size!=256 && size!=512))return nullptr;
  auto *self=GWORLD_SCENE_TILE_PROVIDER(g_object_new(GWORLD_TYPE_SCENE_TILE_PROVIDER,nullptr));
  self->state->minimum=minimum;self->state->maximum=maximum;self->state->size=size;return self;
}
