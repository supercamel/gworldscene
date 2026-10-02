#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <vector>

namespace gworld_scene {
// RGBA8 levels are concatenated, largest first. Average sRGB in linear light;
// alpha remains linear, as for an SRGB8_ALPHA8 texture's generated mip chain.
inline std::size_t mip_chain_bytes(int w, int h) {
  std::size_t total=0;
  while(w>0 && h>0) { total+=std::size_t(w)*h*4; if(w==1 && h==1)break; w=std::max(1,w/2);h=std::max(1,h/2); }
  return total;
}
inline bool append_srgb_mipmaps(std::vector<unsigned char>& pixels, int w, int h,
                                const std::function<bool()>& cancelled = {}) {
  if(w<=0 || h<=0 || pixels.size()!=std::size_t(w)*h*4)throw std::invalid_argument("Invalid RGBA base level");
  static const auto decode=[] {
    std::array<unsigned,256> a{};
    for(unsigned i=0;i<256;i++){double s=i/255.0;a[i]=std::lround((s<=0.04045?s/12.92:std::pow((s+0.055)/1.055,2.4))*65535);}
    return a;
  }();
  static const auto encode=[] {
    std::array<unsigned char,65536> a{};
    for(unsigned i=0;i<65536;i++){double l=i/65535.0;a[i]=std::lround((l<=0.0031308?l*12.92:1.055*std::pow(l,1/2.4)-0.055)*255);}
    return a;
  }();
  pixels.reserve(mip_chain_bytes(w,h));std::size_t offset=0;
  while(w>1 || h>1){
    int nw=std::max(1,w/2),nh=std::max(1,h/2);auto dest=pixels.size();pixels.resize(dest+std::size_t(nw)*nh*4);
    for(int y=0;y<nh;y++){
      if(cancelled && cancelled())return false;
      for(int x=0;x<nw;x++){
        unsigned sums[4]={0,0,0,0},count=0;
        // Cover every source texel, including odd-sized level edges.
        for(int sy=y*h/nh;sy<(y+1)*h/nh;sy++)for(int sx=x*w/nw;sx<(x+1)*w/nw;sx++){
          auto i=offset+(std::size_t(sy)*w+sx)*4;
          for(int c=0;c<3;c++)sums[c]+=decode[pixels[i+c]];
          sums[3]+=pixels[i+3];count++;
        }
        auto i=dest+(std::size_t(y)*nw+x)*4;
        for(int c=0;c<3;c++)pixels[i+c]=encode[(sums[c]+count/2)/count];
        pixels[i+3]=(sums[3]+count/2)/count;
      }
    }
    offset=dest;w=nw;h=nh;
  }
  return true;
}
}
