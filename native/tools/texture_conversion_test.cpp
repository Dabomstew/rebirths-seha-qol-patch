#include "texture_conversion.hpp"
#include <cstdio>
#include <vector>
#include <utility>

// Scalar coordinate oracle: consume each axis's bits while both axes remain,
// then append the larger axis's remaining bits. X occupies the odd bit first.
static uint32_t Address(uint32_t x,uint32_t y,uint32_t ex,uint32_t ey) {
    uint32_t result=0,bit=0;
    while(ex>1 || ey>1) {
        if(ey>1) {result|=(y&1)<<bit++;y>>=1;ey>>=1;}
        if(ex>1) {result|=(x&1)<<bit++;x>>=1;ex>>=1;}
    }
    return result;
}
static uint32_t Extent(int n) {uint32_t e=1;while(e<uint32_t(n)) e*=2;return e;}
int main() {
    using namespace rebirths::textures;
    for(uint32_t n=0;n<=765;n++) if(((n*683)>>11)!=n/3) return 1;
    const unsigned char blocks[]={255,255,0,0,0xe4,0xe4,0xe4,0xe4,0,0,255,255,0xe4,0xe4,0xe4,0xe4};
    std::vector<uint32_t> output(36,0x12345678);
    DecodeBc1(output.data(),8,4,blocks);
    const uint32_t expected[]={0xffffffff,0xff000000,0xffaaaaaa,0xff555555,0xff000000,0xffffffff,0xff7f7f7f,0};
    for(int y=0;y<4;y++) for(int x=0;x<8;x++) if(output[y*8+x]!=expected[x]) return 2;
    for(int n=32;n<36;n++) if(output[n]!=0x12345678) return 3;
    unsigned char alphaBlock[16]={};
    alphaBlock[10]=alphaBlock[11]=255;
    for(int i=12;i<16;i++) alphaBlock[i]=0xe4;
    const uint32_t rgb[]={0,0xffffff,0x555555,0xaaaaaa};
    for(int i=0;i<8;i++) alphaBlock[i]=static_cast<unsigned char>(i*2|((i*2+1)<<4));
    DecodeBcAlpha<2>(output.data(),4,4,alphaBlock);
    for(int i=0;i<16;i++) if(output[i]!=(rgb[i&3]|(uint32_t(i*17)<<24))) return 6;
    uint64_t indices=0;for(int i=0;i<16;i++) indices|=uint64_t(i&7)<<(3*i);
    for(int i=0;i<6;i++) alphaBlock[2+i]=static_cast<unsigned char>(indices>>(8*i));
    for(bool seven:{false,true}) {
        alphaBlock[0]=seven?210:0;alphaBlock[1]=seven?0:200;
        const uint32_t a7[]={210,0,180,150,120,90,60,30},a5[]={0,200,40,80,120,160,0,255};
        DecodeBcAlpha<4>(output.data(),4,4,alphaBlock);
        for(int i=0;i<16;i++) if(output[i]!=(rgb[i&3]|((seven?a7:a5)[i&7]<<24))) return 7;
    }
    for(auto size:{std::pair<int,int>{4,4},{8,4},{4,64},{64,4},{12,20},{20,12},{128,64},{512,1024},{2048,2048}}) {
        const int w=size.first,h=size.second;const uint32_t ex=Extent(w),ey=Extent(h);
        std::vector<uint32_t> source(size_t(ex)*ey),actual(size_t(w)*h+16,0xdeadbeef);
        for(size_t i=0;i<source.size();i++) source[i]=uint32_t(i*1664525u+1013904223u);
        Untwiddle32(actual.data(),source.data(),w,h,Address(ex-1,0,ex,ey),Address(0,ey-1,ex,ey));
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) if(actual[size_t(y)*w+x]!=source[Address(x,y,ex,ey)]) return 4;
        for(size_t i=size_t(w)*h;i<actual.size();i++) if(actual[i]!=0xdeadbeef) return 5;
        if(!(w&(w-1)) && !(h&(h-1))) {
            std::fill(actual.begin(),actual.end(),0xdeadbeef);
            Compose32(actual.data(),source.data(),w,h,Address(ex-1,0,ex,ey),Address(0,ey-1,ex,ey));
            for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
                const uint32_t block=Address(uint32_t(x/4),uint32_t(y/4),uint32_t(w/4),uint32_t(h/4));
                const size_t originalRow=(block/uint32_t(w/4))*4+uint32_t(y&3);
                const size_t originalColumn=(block%uint32_t(w/4))*4+uint32_t(x&3);
                if(actual[size_t(y)*w+x]!=source[originalRow*w+originalColumn]) return 8;
            }
            for(size_t i=size_t(w)*h;i<actual.size();i++) if(actual[i]!=0xdeadbeef) return 9;
        }
    }
    std::puts("Texture conversion kernels OK: palettes, transparency, stride, rectangular Morton mapping, padding");
    return 0;
}
