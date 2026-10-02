#include "gworld-scene-mipmaps-private.h"
#include <glib.h>
#include <algorithm>
#include <cmath>

static void color_and_alpha() {
  std::vector<unsigned char> p={0,0,0,0,255,255,255,255,0,0,0,0,255,255,255,255};
  auto base=p;g_assert_true(gworld_scene::append_srgb_mipmaps(p,2,2));
  g_assert_cmpuint(p.size(),==,20);g_assert_true(std::equal(base.begin(),base.end(),p.begin()));
  for(int c=0;c<3;c++)g_assert_cmpuint(p[16+c],==,188);
  g_assert_cmpuint(p[19],==,128);
}
static void constant_and_edges() {
  for(unsigned c=0;c<256;c++){
    std::vector<unsigned char> p(7*3*4,c);g_assert_true(gworld_scene::append_srgb_mipmaps(p,7,3));
    g_assert_cmpuint(p.size(),==,gworld_scene::mip_chain_bytes(7,3));
    for(auto value:p)g_assert_cmpuint(value,==,c);
  }
  std::vector<unsigned char> p(3*3*4,0);p[32]=255;p[35]=255;
  g_assert_true(gworld_scene::append_srgb_mipmaps(p,3,3));
  const auto expected=std::lround((1.055*std::pow(1.0/9.0,1.0/2.4)-0.055)*255);
  g_assert_cmpint(std::abs(int(p[36])-int(expected)),<=,1);g_assert_cmpuint(p[39],==,28);
  g_assert_cmpuint(gworld_scene::mip_chain_bytes(8,1),==,60);
}
static void cancellation_and_validation() {
  std::vector<unsigned char> p(256*256*4,127);
  g_assert_false(gworld_scene::append_srgb_mipmaps(p,256,256,[]{return true;}));
  bool rejected=false;try{gworld_scene::append_srgb_mipmaps(p,0,256);}catch(const std::invalid_argument&){rejected=true;}
  g_assert_true(rejected);
}
int main(int argc,char**argv){g_test_init(&argc,&argv,nullptr);
 g_test_add_func("/mipmaps/linear-color-alpha",color_and_alpha);
 g_test_add_func("/mipmaps/constant-odd-edges",constant_and_edges);
 g_test_add_func("/mipmaps/cancel-validation",cancellation_and_validation);
 return g_test_run();}
