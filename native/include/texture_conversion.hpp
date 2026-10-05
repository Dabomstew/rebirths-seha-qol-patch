#pragma once
#include <cstdint>

namespace rebirths::textures {

inline uint32_t Rgba565(uint32_t value) noexcept {
    const uint32_t r=value>>11, g=(value>>5)&63, b=value&31;
    return ((r<<3)|(r>>2)) | (((g<<2)|(g>>4))<<8) | (((b<<3)|(b>>2))<<16) | 0xff000000u;
}

// Inputs are complete 4x4 BC1 blocks; the caller guards dimensions and format.
inline void DecodeBc1(uint32_t* output, int width, int height, const unsigned char* source) noexcept {
    for (int y=0;y<height;y+=4) for (int x=0;x<width;x+=4,source+=8) {
        const uint32_t c0=source[0]|(uint32_t(source[1])<<8), c1=source[2]|(uint32_t(source[3])<<8);
        uint32_t palette[4]={Rgba565(c0),Rgba565(c1),0xff000000u,c0>c1?0xff000000u:0};
        for (uint32_t shift=0;shift<24;shift+=8) {
            const uint32_t a=(palette[0]>>shift)&255, b=(palette[1]>>shift)&255;
            if (c0>c1) {
                // Exact floor /3 for the bounded channel sums [0,765].
                palette[2]|=(((2*a+b)*683)>>11)<<shift;
                palette[3]|=(((a+2*b)*683)>>11)<<shift;
            } else palette[2]|=((a+b)>>1)<<shift;
        }
        uint32_t bits=source[4]|(uint32_t(source[5])<<8)|(uint32_t(source[6])<<16)|(uint32_t(source[7])<<24);
        uint32_t* row=output+y*width+x;
        for (int yy=0;yy<4;yy++,row+=width) {
            row[0]=palette[bits&3];bits>>=2;
            row[1]=palette[bits&3];bits>>=2;
            row[2]=palette[bits&3];bits>>=2;
            row[3]=palette[bits&3];bits>>=2;
        }
    }
}

// BC2/BC3 always use four opaque colors, independently of endpoint ordering.
template<uint32_t Mode>
inline void DecodeBcAlpha(uint32_t* output,int width,int height,const unsigned char* source) noexcept {
    static_assert(Mode==2 || Mode==4);
    for(int y=0;y<height;y+=4) for(int x=0;x<width;x+=4,source+=16) {
        const uint32_t c0=source[8]|(uint32_t(source[9])<<8),c1=source[10]|(uint32_t(source[11])<<8);
        uint32_t colors[4]={Rgba565(c0)&0xffffffu,Rgba565(c1)&0xffffffu,0,0};
        for(uint32_t shift=0;shift<24;shift+=8) {
            const uint32_t a=(colors[0]>>shift)&255,b=(colors[1]>>shift)&255;
            colors[2]|=(((2*a+b)*683)>>11)<<shift;colors[3]|=(((a+2*b)*683)>>11)<<shift;
        }
        uint32_t colorBits=source[12]|(uint32_t(source[13])<<8)|(uint32_t(source[14])<<16)|(uint32_t(source[15])<<24);
        uint32_t alpha[8]={},alphaLow=0,alphaHigh=0;
        if constexpr(Mode==4) {
            alpha[0]=source[0];alpha[1]=source[1];
            if(alpha[0]>alpha[1]) {
                for(uint32_t n=2;n<8;n++) alpha[n]=((8-n)*alpha[0]+(n-1)*alpha[1])/7;
            } else {
                for(uint32_t n=2;n<6;n++) alpha[n]=((6-n)*alpha[0]+(n-1)*alpha[1])/5;
                alpha[6]=0;alpha[7]=255;
            }
            alphaLow=source[2]|(uint32_t(source[3])<<8)|(uint32_t(source[4])<<16);
            alphaHigh=source[5]|(uint32_t(source[6])<<8)|(uint32_t(source[7])<<16);
        }
        uint32_t* row=output+y*width+x;
        for(int yy=0;yy<4;yy++,row+=width) {
            uint32_t alphaBits;
            if constexpr(Mode==2) alphaBits=source[yy*2]|(uint32_t(source[yy*2+1])<<8);
            else alphaBits=(yy<2?alphaLow:alphaHigh)>>((yy&1)*12);
            for(int xx=0;xx<4;xx++) {
                uint32_t opacity;
                if constexpr(Mode==2) {opacity=(alphaBits&15)*17;alphaBits>>=4;}
                else {opacity=alpha[alphaBits&7];alphaBits>>=3;}
                row[xx]=colors[colorBits&3]|(opacity<<24);colorBits>>=2;
            }
        }
    }
}

inline void Untwiddle32(uint32_t* output,const uint32_t* source,int width,int height,
                        uint32_t xmask,uint32_t ymask) noexcept {
    xmask>>=4;ymask>>=4;uint32_t row=0;
    for (int y=0;y<height;y+=4) {
        uint32_t column=0;
        for (int x=0;x<width;x+=4) {
            const uint32_t* s=source+(row+column)*16;
            uint32_t* a=output+y*width+x;uint32_t* b=a+width;uint32_t* c=b+width;uint32_t* d=c+width;
            a[0]=s[0];a[1]=s[2];a[2]=s[8];a[3]=s[10];
            b[0]=s[1];b[1]=s[3];b[2]=s[9];b[3]=s[11];
            c[0]=s[4];c[1]=s[6];c[2]=s[12];c[3]=s[14];
            d[0]=s[5];d[1]=s[7];d[2]=s[13];d[3]=s[15];
            column=(column-xmask)&xmask;
        }
        row=(row-ymask)&ymask;
    }
}

// Compose native row-to-tile packing and untwiddling without the tile buffer.
// Width/height must be powers of two >=4; other layouts retain native padding.
inline void Compose32(uint32_t* output,const uint32_t* source,int width,int height,
                      uint32_t xmask,uint32_t ymask) noexcept {
    xmask>>=4;ymask>>=4;
    const uint32_t columns=uint32_t(width)/4;uint32_t shift=0;
    for(uint32_t n=columns;n>1;n>>=1) ++shift;
    uint32_t row=0;
    for(int y=0;y<height;y+=4) {
        uint32_t column=0;
        for(int x=0;x<width;x+=4) {
            const uint32_t block=row+column;
            const uint32_t* s=source+(((block>>shift)*uint32_t(width)+(block&(columns-1)))*4);
            uint32_t* d=output+y*width+x;
            for(int yy=0;yy<4;yy++,s+=width,d+=width) {
                d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];
            }
            column=(column-xmask)&xmask;
        }
        row=(row-ymask)&ymask;
    }
}
}
