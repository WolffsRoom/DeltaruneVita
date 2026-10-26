/* Small platform glue needed by original VitaSettings on native GXM.
 * Do not link VitaGL or pretend unsupported Wi-Fi uploading is implemented. */
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/rtc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#ifndef NATIVE_GXM_PROBE_VERSION
#define NATIVE_GXM_PROBE_VERSION "v0.78-gxm-dev"
#endif
bool g_vitaModernGlActive=false;
bool g_vitaExperimentalGlActive=false;
/* Set by main.c; read by the shared per-room rules in runner.c. */
int g_vitaSpanishModActive=0;
void VitaProbe_logLine(const char* text) {
 if(!text)return;
 SceUID fd=sceIoOpen("ux0:data/deltarune/deltarunevita/native-gxm-probe.log",
                     SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);
 if(fd<0)return;
 sceIoWrite(fd,text,strlen(text));
 sceIoWrite(fd,"\n",1);
 sceIoClose(fd);
}
int VitaBorders_filesAvailable(void) {
 SceIoStat st;
 return sceIoGetstat("ux0:data/deltarune/deltarunevita/borders/border_line_1080_0.png",&st)>=0;
}
/* Room name of the running game, published by main.c for log archives. */
const char* volatile g_gxmCurrentRoomName = NULL;

static unsigned gxm_archive_next_sequence(const char* dir) {
 unsigned next = 1;
 SceUID d = sceIoDopen(dir);
 if (d < 0) return next;
 SceIoDirent entry;
 memset(&entry, 0, sizeof(entry));
 while (sceIoDread(d, &entry) > 0) {
  unsigned value = 0;
  if (sscanf(entry.d_name, "gxmlog%u-", &value) == 1 && value >= next) next = value + 1;
  memset(&entry, 0, sizeof(entry));
 }
 sceIoDclose(d);
 return next;
}

/* "New diagnostic log": archive the active logs into
 * devlogs/chapter N/, like the VitaGL build, then start fresh files that
 * restate the build and chapter. */
void VitaProbe_rotateLog(void) {
 extern int g_vitaActiveChapter;
 const char* gxmPath = "ux0:data/deltarune/deltarunevita/native-gxm-probe.log";
 const char* sharedPath = "ux0:data/deltarune/deltarunevita/butterscotch-probe.log";
 char dir[192];
 sceIoMkdir("ux0:data/deltarune/deltarunevita/devlogs", 0777);
 snprintf(dir, sizeof(dir), "ux0:data/deltarune/deltarunevita/devlogs/chapter %d",
          g_vitaActiveChapter);
 sceIoMkdir(dir, 0777);
 char room[64] = {0};
 const char* source = g_gxmCurrentRoomName ? g_gxmCurrentRoomName : "unknown_room";
 unsigned k = 0;
 for (; source[k] && k + 1 < sizeof(room); k++) {
  char c = source[k];
  room[k] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '_') ? c : '_';
 }
 room[k] = 0;
 SceDateTime now;
 memset(&now, 0, sizeof(now));
 sceRtcGetCurrentClockLocalTime(&now);
 unsigned seq = gxm_archive_next_sequence(dir);
 char stamp[32];
 snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d",
          now.year, now.month, now.day, now.hour, now.minute, now.second);
 char target[384];
 SceIoStat st;
 if (sceIoGetstat(gxmPath, &st) >= 0) {
  snprintf(target, sizeof(target), "%s/gxmlog%u-%s_%s.log", dir, seq, room, stamp);
  sceIoRename(gxmPath, target);
 }
 if (sceIoGetstat(sharedPath, &st) >= 0) {
  snprintf(target, sizeof(target), "%s/gxmlog%u-%s_%s-butterscotch.log", dir, seq, room, stamp);
  sceIoRename(sharedPath, target);
 }
 /* The process keeps running after a rotation; restate which build and
  * chapter the following lines belong to. */
 char line[160];
 int n = snprintf(line, sizeof(line),
   "BACKEND=native-gxm log_rotated version=%s chapter=%d archive=%u\n",
   NATIVE_GXM_PROBE_VERSION, g_vitaActiveChapter, seq);
 SceUID fd = sceIoOpen(gxmPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
 if (fd >= 0) { sceIoWrite(fd, line, (SceSize)n); sceIoClose(fd); }
 fd = sceIoOpen(sharedPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
 if (fd >= 0) { sceIoWrite(fd, line, (SceSize)n); sceIoClose(fd); }
}
int VitaProbe_sendCurrentLog(char* status,size_t size) {
 if(status&&size)snprintf(status,size,"Unavailable in native GXM build (Wi-Fi uploader not ported)");
 return -1;
}
