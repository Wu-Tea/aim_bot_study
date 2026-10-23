#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace pipeline_contract {
// Immutable after worker startup. Hue is OpenCV's 0..180, fixed point /256.
// Compile HEX once; the pixel path has no division, allocation or HSV float conversion.
struct MarkerColorBand {
    int lower_q = 142 * 256, upper_q = 158 * 256;
    int saturation_min = 120, value_min = 120;
    // Selected once when compiling the palette, never inferred per pixel.
    int fast_sector = 1; // 0: general hue, 1: magenta, 2: cyan
    static int channel(char c) {
        if (c >= '0' && c <= '9') return c-'0';
        if (c >= 'a' && c <= 'f') return c-'a'+10;
        if (c >= 'A' && c <= 'F') return c-'A'+10;
        throw std::invalid_argument("marker color must be RRGGBB or #RRGGBB");
    }
    static MarkerColorBand from_hex(std::string hex, int saturation, int value) {
        if (!hex.empty() && hex[0]=='#') hex.erase(0,1);
        if (hex.size()!=6) throw std::invalid_argument("marker color must be RRGGBB or #RRGGBB");
        int rgb[3]; for(int i=0;i<3;++i) rgb[i]=16*channel(hex[i*2])+channel(hex[i*2+1]);
        const int hi=std::max({rgb[0],rgb[1],rgb[2]}),lo=std::min({rgb[0],rgb[1],rgb[2]}),d=hi-lo;
        if (hi<value || 255*d<saturation*hi || d==0)
            throw std::invalid_argument("marker color is too dark or unsaturated for detection");
        double h=30.0*(hi==rgb[0] ? double(rgb[1]-rgb[2])/d : hi==rgb[1] ? 2.0+double(rgb[2]-rgb[0])/d : 4.0+double(rgb[0]-rgb[1])/d);
        if(h<0)h+=180;
        const int centre=static_cast<int>(std::lround(h*256)) % (180*256);
        return {(centre-8*256+180*256)%(180*256),(centre+8*256)%(180*256),saturation,value,
            centre==150*256 ? 1 : centre==90*256 ? 2 : 0};
    }
    bool matches(int r,int g,int b) const {
        if(fast_sector==1) {
            if(g>=r || g>=b)return false;
            const int hi=std::max(r,b),d=hi-g;
            return hi>=value_min && 255*d>=saturation_min*hi && 15*std::abs(r-b)<=4*d;
        }
        if(fast_sector==2) {
            if(r>=g || r>=b)return false;
            const int hi=std::max(g,b),d=hi-r;
            return hi>=value_min && 255*d>=saturation_min*hi && 15*std::abs(g-b)<=4*d;
        }
        const int hi=std::max({r,g,b}),lo=std::min({r,g,b}),d=hi-lo;
        if(hi<value_min || d==0 || 255*d<saturation_min*hi)return false;
        int n=hi==r ? g-b : hi==g ? 2*d+b-r : 4*d+r-g;
        if(n<0)n+=6*d;
        const int hq=30*256*n,low=lower_q*d,high=upper_q*d;
        return lower_q<=upper_q ? hq>=low && hq<=high : hq>=low || hq<=high;
    }
};
struct MarkerPalette {
    MarkerColorBand enemy{};
    MarkerColorBand friendly{82*256,98*256,80,50,2};
    static MarkerPalette from_hex(const std::string& enemy_hex,const std::string& friendly_hex) {
        MarkerPalette p{MarkerColorBand::from_hex(enemy_hex,120,120),MarkerColorBand::from_hex(friendly_hex,80,50)};
        constexpr int circle=180*256;
        int distance=std::abs(p.enemy.lower_q-p.friendly.lower_q);
        distance=std::min(distance,circle-distance);
        if(distance<=16*256) throw std::invalid_argument("enemy and friendly marker hue ranges overlap");
        return p;
    }
};
} // namespace pipeline_contract
