/* BC3/PVR payloads produced by PVRTexTool are linear arrays of 4x4 blocks.
 * sceGxmTextureInitSwizzledArbitrary expects Morton/twiddled block order.
 * Do not confuse the 4x4 BC3 block itself with individual pixels.
 * Width and height must be powers of two (validated by the caller).
 */
#ifndef DELTARUNE_NATIVE_BC3_SWIZZLE_H
#define DELTARUNE_NATIVE_BC3_SWIZZLE_H
#include <stdint.h>
#include <string.h>
#include <stddef.h>
static size_t nativeBc3MortonIndex(unsigned x,unsigned y,unsigned bx,unsigned by){
    unsigned side=bx<by?bx:by;
    unsigned i=0, bit=1, outBit=1;
    while(bit<side){
        if(x&bit)i|=outBit;
        outBit<<=1;
        if(y&bit)i|=outBit;
        outBit<<=1;
        bit<<=1;
    }
    unsigned mask=side-1;
    if(bx>by)i|=(x&~mask)*side;
    else if(by>bx)i|=(y&~mask)*side;
    return i;
}
static int nativeBc3Swizzle(uint8_t* dst,const uint8_t* src,unsigned width,unsigned height){
    if(!src||!dst||width<4||height<4||
       width>4096||height>4096||(width&(width-1))||(height&(height-1)))
        return 0;
    unsigned bx=width/4,by=height/4;
    for(unsigned y=0;y<by;y++)
       for(unsigned x=0;x<bx;x++){
         size_t source=((size_t)y*bx+x)*16;
         size_t dest=nativeBc3MortonIndex(x,y,bx,by)*16;
         memcpy(dst+dest,src+source,16);
       }
    return 1;
}
#endif
