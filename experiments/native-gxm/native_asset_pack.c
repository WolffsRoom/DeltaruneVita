/* VAP1 app0:assets/prologue.bin — native GXM-only, no extraction needed.
 * Tiny cached index and seek/read preserve low working set.
 * Original loose paths remain supported for DEV testing on older layouts. */
#include "native_asset_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define VAP_HEADER 12
#define VAP_RECORD 160
#define VAP_MAX_ENTRIES 500
typedef struct {
    char name[144];
    uint32_t offset,size,crc;
} VAPentry;
static FILE* packFile=NULL;
static VAPentry* packItems=NULL;
static uint32_t packCount=0;
static int packState=0;
static uint32_t readLE32(const uint8_t*p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int packOpen(void){
    if(packState)return packState==1;
    packState=-1;
    FILE*f=fopen("app0:assets/prologue.bin","rb");
    if(!f)return 0;
    uint8_t hdr[VAP_HEADER];
    if(fread(hdr,1,sizeof(hdr),f)!=sizeof(hdr)||
       memcmp(hdr,"VAP1",4)!=0){fclose(f);return 0;}
    uint32_t count=readLE32(hdr+4),start=readLE32(hdr+8);
    if(!count||count>VAP_MAX_ENTRIES||
       start!=VAP_HEADER+VAP_RECORD*count){fclose(f);return 0;}
    VAPentry*entries=(VAPentry*)calloc(count,sizeof(VAPentry));
    if(!entries){fclose(f);return 0;}
    int valid=1;
    long previous=(long)start;
    for(uint32_t i=0;i<count;i++){
        uint8_t raw[VAP_RECORD];
        if(fread(raw,1,sizeof(raw),f)!=sizeof(raw)){valid=0;break;}
        if(!memchr(raw,0,144)){valid=0;break;}
        memcpy(entries[i].name,raw,144);
        entries[i].offset=readLE32(raw+144);
        entries[i].size=readLE32(raw+148);
        entries[i].crc=readLE32(raw+152);
        if(!entries[i].size||entries[i].size>4*1024*1024 ||
            (long)entries[i].offset<previous||
            (i && strcmp(entries[i-1].name,entries[i].name)>=0)){
            valid=0;break;
        }
        previous=(long)entries[i].offset+entries[i].size;
    }
    if(valid){
        if(fseek(f,0,SEEK_END)!=0 || ftell(f)<previous)valid=0;
    }
    if(!valid){free(entries);fclose(f);return 0;}
    packFile=f;packItems=entries;packCount=count;packState=1;
    return 1;
}
int GxmAssetPack_read(const char*path,unsigned char**bytes,size_t*length){
    static const char prefix[]="app0:assets/prologue/";
    if(bytes)*bytes=NULL;
    if(length)*length=0;
    if(!path||!bytes||!length||
       strncmp(path,prefix,sizeof(prefix)-1)!=0||!packOpen())return 0;
    const char*key=path+sizeof(prefix)-1;
    uint32_t lo=0,hi=packCount;
    while(lo<hi){
        uint32_t mid=lo+(hi-lo)/2;
        int cmp=strcmp(key,packItems[mid].name);
        if(!cmp){
            VAPentry*e=&packItems[mid];
            unsigned char*data=malloc(e->size);
            if(!data)return 0;
            if(fseek(packFile,(long)e->offset,SEEK_SET)!=0||
               fread(data,1,e->size,packFile)!=e->size){
                free(data);return 0;
            }
            *bytes=data;*length=e->size;
            return 1;
        }
        if(cmp<0)hi=mid;
        else lo=mid+1;
    }
    return 0;
}
void GxmAssetPack_close(void){
    if(packFile)fclose(packFile);
    packFile=NULL;
    free(packItems);packItems=NULL;packCount=0;packState=0;
}
