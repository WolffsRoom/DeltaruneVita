/* v0.84: PC <-> Vita save sync over the local network (see vita_save_sync.h
 * and docs/save-sync-protocol.md). Runs in a low-priority thread only while
 * the game is open and the "save_sync" option is on. Built for the Vita and,
 * with SAVE_SYNC_DESKTOP_TEST, as a Windows test server for the PC client. */
#include "vita_save_sync.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SAVE_SYNC_DESKTOP_TEST)
#include <winsock2.h>
#include <windows.h>
#include <direct.h>
typedef SOCKET ss_sock;
#define SS_BAD ((ss_sock)INVALID_SOCKET)
static void ss_sleep_ms(int ms) { Sleep((DWORD)ms); }
static void ss_mkdir(const char *p) { _mkdir(p); }
#else
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
typedef int ss_sock;
#define SS_BAD (-1)
static void ss_sleep_ms(int ms) { sceKernelDelayThread((SceUInt)ms * 1000U); }
static void ss_mkdir(const char *p) { sceIoMkdir(p, 0777); }
#endif

#define SS_MAX_FILE (4U * 1024U * 1024U)
#define SS_LINE 256

static volatile bool g_enabled = false;
static volatile bool g_threadRunning = false;
static char g_dir[192] = "";
static char g_status[64] = "off";

static void set_status(const char *s) { snprintf(g_status, sizeof(g_status), "%s", s); }
bool SaveSync_isEnabled(void) { return g_enabled; }
const char *SaveSync_status(void) { return g_enabled ? g_status : "off"; }

/* Same set the PC app syncs: dr.ini, vacationmemories.png, filech<chapter>_<slot>.
 * Never true_config.ini / keyconfig / trophies (platform specific). */
bool SaveSync_isSaveName(const char *name) {
    if (name == NULL) return false;
    if (strcmp(name, "dr.ini") == 0 || strcmp(name, "vacationmemories.png") == 0) return true;
    if (strncmp(name, "filech", 6) != 0) return false;
    const char *p = name + 6;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') ++p;
    if (*p != '_') return false;
    ++p;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') ++p;
    return *p == '\0' && strlen(name) < 32;
}

unsigned SaveSync_crc32(const unsigned char *data, unsigned long size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned long i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

static unsigned char *read_whole(const char *path, unsigned long *size) {
    *size = 0;
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || (unsigned long)n > SS_MAX_FILE || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    unsigned char *data = (unsigned char *)malloc(n > 0 ? (size_t)n : 1);
    if (data == NULL) { fclose(f); return NULL; }
    if (n > 0 && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); fclose(f); return NULL; }
    fclose(f);
    *size = (unsigned long)n;
    return data;
}

/* ---- sockets ---- */
#if defined(SAVE_SYNC_DESKTOP_TEST)
static ss_sock ss_listen(int port) {
    ss_sock s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SS_BAD) return SS_BAD;
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons((u_short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s, 1) != 0) { closesocket(s); return SS_BAD; }
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    return s;
}
static ss_sock ss_accept(ss_sock l) {
    ss_sock c = accept(l, NULL, NULL);
    if (c == SS_BAD) return SS_BAD;
    u_long nb = 0; ioctlsocket(c, FIONBIO, &nb);
    DWORD t = 15000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof(t));
    return c;
}
static ss_sock ss_udp(void) {
    ss_sock s = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1; if (s != SS_BAD) setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof(one));
    return s;
}
static void ss_broadcast(ss_sock s, const char *msg) {
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(SAVE_SYNC_DISCOVERY_PORT); a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(s, msg, (int)strlen(msg), 0, (struct sockaddr *)&a, sizeof(a));
}
static int ss_recv(ss_sock s, void *b, int n) { return recv(s, (char *)b, n, 0); }
static int ss_send(ss_sock s, const void *b, int n) { return send(s, (const char *)b, n, 0); }
static void ss_close(ss_sock s) { if (s != SS_BAD) closesocket(s); }
#else
static ss_sock ss_listen(int port) {
    int s = sceNetSocket("drsync_tcp", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (s < 0) return SS_BAD;
    int one = 1; sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR, &one, sizeof(one));
    SceNetSockaddrIn a; memset(&a, 0, sizeof(a));
    a.sin_len = sizeof(a); a.sin_family = SCE_NET_AF_INET;
    a.sin_port = sceNetHtons((unsigned short)port); a.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
    if (sceNetBind(s, (SceNetSockaddr *)&a, sizeof(a)) < 0 || sceNetListen(s, 1) < 0) {
        sceNetSocketClose(s); return SS_BAD;
    }
    sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &one, sizeof(one));
    return s;
}
static ss_sock ss_accept(ss_sock l) {
    SceNetSockaddrIn a; unsigned int len = sizeof(a);
    int c = sceNetAccept(l, (SceNetSockaddr *)&a, &len);
    if (c < 0) return SS_BAD;
    int zero = 0; sceNetSetsockopt(c, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &zero, sizeof(zero));
    int t = 15 * 1000 * 1000; sceNetSetsockopt(c, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &t, sizeof(t));
    return c;
}
static ss_sock ss_udp(void) {
    int s = sceNetSocket("drsync_udp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    if (s < 0) return SS_BAD;
    int one = 1; sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &one, sizeof(one));
    return s;
}
static void ss_broadcast(ss_sock s, const char *msg) {
    SceNetSockaddrIn a; memset(&a, 0, sizeof(a));
    a.sin_len = sizeof(a); a.sin_family = SCE_NET_AF_INET;
    a.sin_port = sceNetHtons(SAVE_SYNC_DISCOVERY_PORT); a.sin_addr.s_addr = 0xFFFFFFFFu;
    sceNetSendto(s, msg, (unsigned int)strlen(msg), 0, (SceNetSockaddr *)&a, sizeof(a));
}
static int ss_recv(ss_sock s, void *b, int n) { return sceNetRecv(s, b, (unsigned int)n, 0); }
static int ss_send(ss_sock s, const void *b, int n) { return sceNetSend(s, b, (unsigned int)n, 0); }
static void ss_close(ss_sock s) { if (s >= 0) sceNetSocketClose(s); }
#endif

static bool send_all(ss_sock s, const void *data, unsigned long n) {
    const char *p = (const char *)data;
    while (n > 0) {
        int chunk = n > 65536 ? 65536 : (int)n;
        int sent = ss_send(s, p, chunk);
        if (sent <= 0) return false;
        p += sent; n -= (unsigned long)sent;
    }
    return true;
}
static bool send_text(ss_sock s, const char *text) { return send_all(s, text, strlen(text)); }
static bool recv_exact(ss_sock s, unsigned char *buf, unsigned long n) {
    while (n > 0) {
        int got = ss_recv(s, buf, n > 65536 ? 65536 : (int)n);
        if (got <= 0) return false;
        buf += got; n -= (unsigned long)got;
    }
    return true;
}
static bool recv_line(ss_sock s, char *line, int max) {
    int n = 0;
    while (n < max - 1) {
        char c;
        if (ss_recv(s, &c, 1) != 1) return false;
        if (c == '\n') break;
        if (c != '\r') line[n++] = c;
    }
    line[n] = '\0';
    return true;
}

/* ---- directory listing ---- */
typedef void (*ss_entry_fn)(const char *name, void *user);
static void list_dir(const char *dir, ss_entry_fn fn, void *user) {
#if defined(SAVE_SYNC_DESKTOP_TEST)
    char pattern[256]; snprintf(pattern, sizeof(pattern), "%s*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) fn(fd.cFileName, user); } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    SceUID d = sceIoDopen(dir);
    if (d < 0) return;
    SceIoDirent e;
    while (sceIoDread(d, &e) > 0)
        if (!SCE_S_ISDIR(e.d_stat.st_mode)) fn(e.d_name, user);
    sceIoDclose(d);
#endif
}

struct list_ctx { ss_sock sock; bool ok; };
static void list_one(const char *name, void *user) {
    struct list_ctx *ctx = (struct list_ctx *)user;
    if (!ctx->ok || !SaveSync_isSaveName(name)) return;
    char path[256]; snprintf(path, sizeof(path), "%s%s", g_dir, name);
    unsigned long size = 0;
    unsigned char *data = read_whole(path, &size);
    if (data == NULL) return;
    char line[SS_LINE];
    snprintf(line, sizeof(line), "FILE %s %lu %08x\n", name, size, SaveSync_crc32(data, size));
    free(data);
    ctx->ok = send_text(ctx->sock, line);
}

/* Writes atomically: <name>.sync_tmp, old copy to sync_backup/<name>.bak, rename. */
static bool put_file(const char *name, const unsigned char *data, unsigned long size) {
    char path[256], tmp[256], bakDir[256], bak[256];
    snprintf(path, sizeof(path), "%s%s", g_dir, name);
    snprintf(tmp, sizeof(tmp), "%s%s.sync_tmp", g_dir, name);
    snprintf(bakDir, sizeof(bakDir), "%ssync_backup", g_dir);
    snprintf(bak, sizeof(bak), "%ssync_backup/%s.bak", g_dir, name);
    FILE *f = fopen(tmp, "wb");
    if (f == NULL) return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); return false; }
    ss_mkdir(bakDir);
    remove(bak);
    rename(path, bak);          /* may fail when the file is new: fine */
    remove(path);
    if (rename(tmp, path) != 0) { rename(bak, path); remove(tmp); return false; }
    return true;
}

static void serve_client(ss_sock c) {
    char line[SS_LINE];
    set_status("PC connected");
    while (g_enabled && recv_line(c, line, sizeof(line))) {
        char name[64]; unsigned long size = 0;
        if (strncmp(line, "HELLO", 5) == 0) {
            char reply[96];
            snprintf(reply, sizeof(reply), "OK DRSYNC %d PSVita\n", SAVE_SYNC_PROTOCOL);
            if (!send_text(c, reply)) break;
        } else if (strcmp(line, "LIST") == 0) {
            struct list_ctx ctx = { c, true };
            list_dir(g_dir, list_one, &ctx);
            if (!ctx.ok || !send_text(c, "END\n")) break;
        } else if (sscanf(line, "GET %63s", name) == 1) {
            char path[256]; snprintf(path, sizeof(path), "%s%s", g_dir, name);
            unsigned char *data = SaveSync_isSaveName(name) ? read_whole(path, &size) : NULL;
            if (data == NULL) { if (!send_text(c, "ERR missing\n")) break; continue; }
            char head[64]; snprintf(head, sizeof(head), "DATA %lu\n", size);
            bool ok = send_text(c, head) && send_all(c, data, size);
            free(data);
            if (!ok) break;
        } else if (sscanf(line, "PUT %63s %lu", name, &size) == 2) {
            if (!SaveSync_isSaveName(name) || size > SS_MAX_FILE) { send_text(c, "ERR refused\n"); break; }
            unsigned char *data = (unsigned char *)malloc(size > 0 ? size : 1);
            if (data == NULL || !recv_exact(c, data, size)) { free(data); break; }
            bool ok = put_file(name, data, size);
            free(data);
            if (!send_text(c, ok ? "OK\n" : "ERR write\n")) break;
        } else if (strcmp(line, "BYE") == 0) {
            break;
        } else {
            if (!send_text(c, "ERR unknown\n")) break;
        }
    }
    ss_close(c);
    set_status("waiting for PC");
}

#if !defined(SAVE_SYNC_DESKTOP_TEST)
static bool net_ready(void) {
    static bool inited = false;
    if (!inited) {
        sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
        static void *pool = NULL;
        if (pool == NULL) pool = malloc(256 * 1024);
        if (pool == NULL) return false;
        SceNetInitParam p; memset(&p, 0, sizeof(p));
        p.memory = pool; p.size = 256 * 1024;
        int rc = sceNetInit(&p);
        if (rc < 0 && rc != (int)SCE_NET_ERROR_EBUSY) return false;
        sceNetCtlInit();
        inited = true;
    }
    int state = SCE_NETCTL_STATE_DISCONNECTED;
    return sceNetCtlInetGetState(&state) >= 0 && state == SCE_NETCTL_STATE_CONNECTED;
}
#endif

static void server_loop(void) {
    ss_sock listener = SS_BAD, udp = SS_BAD;
    unsigned tick = 0;
    while (g_enabled) {
#if !defined(SAVE_SYNC_DESKTOP_TEST)
        if (!net_ready()) { set_status("no Wi-Fi"); ss_close(listener); ss_close(udp);
                            listener = udp = SS_BAD; ss_sleep_ms(3000); continue; }
#endif
        if (listener == SS_BAD) listener = ss_listen(SAVE_SYNC_TCP_PORT);
        if (udp == SS_BAD) udp = ss_udp();
        if (listener == SS_BAD || udp == SS_BAD) { set_status("network error"); ss_sleep_ms(3000); continue; }
        set_status("waiting for PC");
        if ((tick++ % 10U) == 0U) {   /* every ~2 s */
            char msg[64];
            snprintf(msg, sizeof(msg), "DRSYNC %d %d PSVita\n", SAVE_SYNC_PROTOCOL, SAVE_SYNC_TCP_PORT);
            ss_broadcast(udp, msg);
        }
        ss_sock c = ss_accept(listener);
        if (c != SS_BAD) serve_client(c);
        else ss_sleep_ms(200);
    }
    ss_close(listener);
    ss_close(udp);
    set_status("off");
}

#if defined(SAVE_SYNC_DESKTOP_TEST)
/* Test harness: serve <dir> on localhost until killed. */
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: save_sync_test <dir\\>\n"); return 2; }
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    snprintf(g_dir, sizeof(g_dir), "%s", argv[1]);
    g_enabled = true;
    fprintf(stderr, "SAVE_SYNC_TEST serving %s on %d\n", g_dir, SAVE_SYNC_TCP_PORT);
    server_loop();
    return 0;
}
void SaveSync_setEnabled(bool enabled, const char *saveDir) { (void)enabled; (void)saveDir; }
#else
static int sync_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    server_loop();
    g_threadRunning = false;
    return sceKernelExitDeleteThread(0);
}

void SaveSync_setEnabled(bool enabled, const char *saveDir) {
    if (saveDir != NULL) snprintf(g_dir, sizeof(g_dir), "%s", saveDir);
    g_enabled = enabled;
    if (!enabled || g_threadRunning) return;
    /* Lowest practical user priority: never competes with the game thread. */
    SceUID t = sceKernelCreateThread("drsync", sync_thread, 0x10000100 + 20, 64 * 1024, 0, 0, NULL);
    if (t >= 0 && sceKernelStartThread(t, 0, NULL) >= 0) g_threadRunning = true;
}
#endif
