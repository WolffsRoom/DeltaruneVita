/* Small platform glue needed by original VitaSettings on native GXM.
 * Do not link VitaGL or pretend unsupported Wi-Fi uploading is implemented. */
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
bool g_vitaModernGlActive=false;
int VitaBorders_filesAvailable(void) {
 SceIoStat st;
 return sceIoGetstat("ux0:data/deltarune/deltarunevita/borders/border_line_1080_0.png",&st)>=0;
}
void VitaProbe_rotateLog(void) {
 const char*path="ux0:data/deltarune/deltarunevita/native-gxm-probe.log";
 sceIoRemove("ux0:data/deltarune/deltarunevita/native-gxm-probe-prev.log");
 sceIoRename(path,"ux0:data/deltarune/deltarunevita/native-gxm-probe-prev.log");
}
int VitaProbe_sendCurrentLog(char* status,size_t size) {
 if(status&&size)snprintf(status,size,"Unavailable in native GXM build (Wi-Fi uploader not ported)");
 return -1;
}
