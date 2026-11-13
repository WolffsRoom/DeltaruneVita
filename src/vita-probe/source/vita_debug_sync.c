/* v0.84-3: "Send reports locally" (see vita_debug_sync.h and
 * docs/debug-sync-protocol.md). Read-only server for the DELTARUNE Debug PC
 * app: it lists devlogs/ and the game's screenshot folder, sends files on
 * request and serves the active probe log from an offset (live view). It never
 * writes anything on the Vita. Runs in a low-priority thread only while the
 * option is on. Built for the Vita and, with DEBUG_SYNC_DESKTOP_TEST, as a
 * Windows test server for the PC app. */
#include "vita_debug_sync.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(DEBUG_SYNC_DESKTOP_TEST)
#include <winsock2.h>
#include <windows.h>
typedef SOCKET ds_sock;
#define DS_BAD ((ds_sock)INVALID_SOCKET)
static void ds_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
typedef int ds_sock;
#define DS_BAD (-1)
static void ds_sleep_ms(int ms) { sceKernelDelayThread((SceUInt)ms * 1000U); }
#endif

#define DS_LINE 512
#define DS_TAIL_MAX (64U * 1024U)
#define DS_MAX_DEPTH 4

static volatile bool g_enabled = false;
static volatile bool g_threadRunning = false;
static char g_devDir[192] = DEBUG_SYNC_DEVLOG_DIR;
static char g_shotDir[192] = DEBUG_SYNC_SHOT_DIR;
static char g_dataDir[192] = DEBUG_SYNC_DATA_ROOT;
static char g_status[64] = "off";

static void set_status(const char *s) { snprintf(g_status, sizeof(g_status), "%s", s); }

/* ---- v0.86 remote control (auto-test) ----
 * The PC queues commands (go to room, capture a devlog); the game's main
 * thread takes them with DebugSync_takeCommand and publishes a status line.
 * Each PC client has its own thread; one lock serialises command handling. */
#define DS_CMD_RING 8
static DebugSyncCommand g_cmdRing[DS_CMD_RING];
static volatile unsigned g_cmdHead = 0, g_cmdTail = 0, g_cmdSeq = 0;
static char g_statusLine[384] = "starting";
static const char *(*g_roomNameFn)(int) = NULL;
static volatile int g_roomCount = 0;
static volatile int g_clientCount = 0;
#if defined(DEBUG_SYNC_DESKTOP_TEST)
static CRITICAL_SECTION g_lock; static volatile LONG g_lockInit = 0;
static void ds_lock(void) {
    if (InterlockedCompareExchange(&g_lockInit, 1, 0) == 0) InitializeCriticalSection(&g_lock), g_lockInit = 2;
    while (g_lockInit != 2) Sleep(0);
    EnterCriticalSection(&g_lock);
}
static void ds_unlock(void) { LeaveCriticalSection(&g_lock); }
#define DS_BARRIER() MemoryBarrier()
#define DS_INC(p) InterlockedIncrement((volatile LONG *)(p))
#define DS_DEC(p) InterlockedDecrement((volatile LONG *)(p))
#else
static SceUID g_lock = -1;
static void ds_lock(void) {
    if (g_lock < 0) g_lock = sceKernelCreateMutex("drdebug_lock", 0, 0, NULL);
    sceKernelLockMutex(g_lock, 1, NULL);
}
static void ds_unlock(void) { sceKernelUnlockMutex(g_lock, 1); }
#define DS_BARRIER() __sync_synchronize()
#define DS_INC(p) __sync_fetch_and_add((p), 1)
#define DS_DEC(p) __sync_fetch_and_sub((p), 1)
#endif

static unsigned queue_command(int kind, const char *arg, int seconds) {
    ds_lock();
    unsigned seq = 0;
    if (g_cmdHead - g_cmdTail < DS_CMD_RING) {
        DebugSyncCommand *c = &g_cmdRing[g_cmdHead % DS_CMD_RING];
        c->kind = kind; c->seconds = seconds; c->seq = ++g_cmdSeq;
        snprintf(c->arg, sizeof(c->arg), "%s", arg ? arg : "");
        seq = c->seq;
        DS_BARRIER();
        g_cmdHead++;
    }
    ds_unlock();
    return seq;
}

bool DebugSync_takeCommand(DebugSyncCommand *out) {
    if (g_cmdTail == g_cmdHead) return false;
    DS_BARRIER();
    *out = g_cmdRing[g_cmdTail % DS_CMD_RING];
    g_cmdTail++;
    return true;
}

void DebugSync_publishStatus(const char *line) {
    ds_lock();
    snprintf(g_statusLine, sizeof(g_statusLine), "%s", line ? line : "");
    ds_unlock();
}

void DebugSync_setRoomNames(const char *(*nameAt)(int), int count) {
    g_roomNameFn = nameAt;
    g_roomCount = count;
}
bool DebugSync_isEnabled(void) { return g_enabled; }
const char *DebugSync_status(void) { return g_enabled ? g_status : "off"; }

/* ---- files ---- */
static bool ds_stat(const char *path, unsigned long long *size, unsigned long long *mtime) {
#if defined(DEBUG_SYNC_DESKTOP_TEST)
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return false;
    *size = ((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    *mtime = (((unsigned long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime) / 10000000ULL;
    return true;
#else
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) return false;
    *size = (unsigned long long)st.st_size;
    const SceDateTime *t = &st.st_mtime;
    *mtime = ((((((unsigned long long)t->year * 100ULL + t->month) * 100ULL + t->day) * 100ULL + t->hour)
              * 100ULL + t->minute) * 100ULL + t->second);
    return true;
#endif
}

/* Relative names coming from the PC: no parent dirs, no absolute paths. */
static bool safe_rel(const char *rel) {
    if (rel == NULL || rel[0] == '\0' || rel[0] == '/' || rel[0] == '\\') return false;
    if (strstr(rel, "..") != NULL || strchr(rel, ':') != NULL) return false;
    return strlen(rel) < 160;
}

static const char *area_root(const char *area) {
    if (strcmp(area, "devlog") == 0) return g_devDir;
    if (strcmp(area, "shot") == 0) return g_shotDir;
    return NULL;
}

static bool is_log_name(const char *name) {
    return strcmp(name, "butterscotch-probe.log") == 0 || strcmp(name, "native-gxm-probe.log") == 0;
}

/* ---- sockets (same shape as vita_save_sync.c) ---- */
#if defined(DEBUG_SYNC_DESKTOP_TEST)
static ds_sock ds_listen(int port) {
    ds_sock s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == DS_BAD) return DS_BAD;
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons((u_short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s, 1) != 0) { closesocket(s); return DS_BAD; }
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    return s;
}
static ds_sock ds_accept(ds_sock l) {
    ds_sock c = accept(l, NULL, NULL);
    if (c == DS_BAD) return DS_BAD;
    u_long nb = 0; ioctlsocket(c, FIONBIO, &nb);
    DWORD t = 15000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof(t));
    return c;
}
static ds_sock ds_udp(void) {
    ds_sock s = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1; if (s != DS_BAD) setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof(one));
    return s;
}
static void ds_broadcast(ds_sock s, const char *msg) {
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(DEBUG_SYNC_DISCOVERY_PORT); a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(s, msg, (int)strlen(msg), 0, (struct sockaddr *)&a, sizeof(a));
}
static int ds_recv(ds_sock s, void *b, int n) { return recv(s, (char *)b, n, 0); }
static int ds_send(ds_sock s, const void *b, int n) { return send(s, (const char *)b, n, 0); }
static void ds_close(ds_sock s) { if (s != DS_BAD) closesocket(s); }
#else
static ds_sock ds_listen(int port) {
    int s = sceNetSocket("drdebug_tcp", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (s < 0) return DS_BAD;
    int one = 1; sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR, &one, sizeof(one));
    SceNetSockaddrIn a; memset(&a, 0, sizeof(a));
    a.sin_len = sizeof(a); a.sin_family = SCE_NET_AF_INET;
    a.sin_port = sceNetHtons((unsigned short)port); a.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
    if (sceNetBind(s, (SceNetSockaddr *)&a, sizeof(a)) < 0 || sceNetListen(s, 1) < 0) {
        sceNetSocketClose(s); return DS_BAD;
    }
    sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &one, sizeof(one));
    return s;
}
static ds_sock ds_accept(ds_sock l) {
    SceNetSockaddrIn a; unsigned int len = sizeof(a);
    int c = sceNetAccept(l, (SceNetSockaddr *)&a, &len);
    if (c < 0) return DS_BAD;
    int zero = 0; sceNetSetsockopt(c, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &zero, sizeof(zero));
    int t = 15 * 1000 * 1000; sceNetSetsockopt(c, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &t, sizeof(t));
    return c;
}
static ds_sock ds_udp(void) {
    int s = sceNetSocket("drdebug_udp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    if (s < 0) return DS_BAD;
    int one = 1; sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &one, sizeof(one));
    return s;
}
static void ds_broadcast(ds_sock s, const char *msg) {
    SceNetSockaddrIn a; memset(&a, 0, sizeof(a));
    a.sin_len = sizeof(a); a.sin_family = SCE_NET_AF_INET;
    a.sin_port = sceNetHtons(DEBUG_SYNC_DISCOVERY_PORT); a.sin_addr.s_addr = 0xFFFFFFFFu;
    sceNetSendto(s, msg, (unsigned int)strlen(msg), 0, (SceNetSockaddr *)&a, sizeof(a));
}
static int ds_recv(ds_sock s, void *b, int n) { return sceNetRecv(s, b, (unsigned int)n, 0); }
static int ds_send(ds_sock s, const void *b, int n) { return sceNetSend(s, b, (unsigned int)n, 0); }
static void ds_close(ds_sock s) { if (s >= 0) sceNetSocketClose(s); }
#endif

static bool send_all(ds_sock s, const void *data, unsigned long n) {
    const char *p = (const char *)data;
    while (n > 0) {
        int chunk = n > 65536 ? 65536 : (int)n;
        int sent = ds_send(s, p, chunk);
        if (sent <= 0) return false;
        p += sent; n -= (unsigned long)sent;
    }
    return true;
}
static bool send_text(ds_sock s, const char *text) { return send_all(s, text, strlen(text)); }
static bool recv_line(ds_sock s, char *line, int max) {
    int n = 0;
    while (n < max - 1) {
        char c;
        if (ds_recv(s, &c, 1) != 1) return false;
        if (c == '\n') break;
        if (c != '\r') line[n++] = c;
    }
    line[n] = '\0';
    return true;
}

/* ---- recursive listing: "FILE <area> <size> <mtime> <relative path>" ---- */
struct list_ctx { ds_sock sock; bool ok; const char *area; };

static void list_tree(struct list_ctx *ctx, const char *root, const char *rel, int depth) {
    if (!ctx->ok || depth > DS_MAX_DEPTH) return;
    char dir[384]; snprintf(dir, sizeof(dir), "%s%s", root, rel);
#if defined(DEBUG_SYNC_DESKTOP_TEST)
    char pattern[400]; snprintf(pattern, sizeof(pattern), "%s*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char *name = fd.cFileName;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    SceUID d = sceIoDopen(dir);
    if (d < 0) return;
    SceIoDirent e;
    while (ctx->ok && sceIoDread(d, &e) > 0) {
        const char *name = e.d_name;
        bool isDir = SCE_S_ISDIR(e.d_stat.st_mode);
#endif
        if (name[0] == '.') continue;
        char child[256]; snprintf(child, sizeof(child), "%s%s", rel, name);
        if (isDir) {
            char sub[260]; snprintf(sub, sizeof(sub), "%s/", child);
            list_tree(ctx, root, sub, depth + 1);
            continue;
        }
        char path[400]; snprintf(path, sizeof(path), "%s%s", root, child);
        unsigned long long size = 0, mtime = 0;
        if (!ds_stat(path, &size, &mtime)) continue;
        char line[DS_LINE];
        snprintf(line, sizeof(line), "FILE %s %llu %llu %s\n", ctx->area, size, mtime, child);
        ctx->ok = send_text(ctx->sock, line);
#if defined(DEBUG_SYNC_DESKTOP_TEST)
    } while (ctx->ok && FindNextFileA(h, &fd));
    FindClose(h);
#else
    }
    sceIoDclose(d);
#endif
}

/* Streams a whole file: "DATA <size>\n" + bytes. */
static bool send_file(ds_sock c, const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return send_text(c, "ERR missing\n");
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return send_text(c, "ERR missing\n"); }
    char head[64]; snprintf(head, sizeof(head), "DATA %ld\n", n);
    bool ok = send_text(c, head);
    static unsigned char buf[32 * 1024];
    long left = n;
    while (ok && left > 0) {
        size_t want = left > (long)sizeof(buf) ? sizeof(buf) : (size_t)left;
        size_t got = fread(buf, 1, want, f);
        if (got == 0) {                     /* shrank while sending: pad so the PC stays in sync */
            memset(buf, 0, want); got = want;
        }
        ok = send_all(c, buf, (unsigned long)got);
        left -= (long)got;
    }
    fclose(f);
    return ok;
}

/* "TAIL <log> <offset>" -> "DATA <n> <total>[ RESET]\n" + up to 64 KiB. RESET
 * means the log was rotated or recreated (shorter than offset): read from 0. */
static bool send_tail(ds_sock c, const char *name, unsigned long long offset) {
    char path[256]; snprintf(path, sizeof(path), "%s%s", g_dataDir, name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) return send_text(c, "DATA 0 0 RESET\n");
    fseek(f, 0, SEEK_END); long total = ftell(f);
    bool reset = total >= 0 && offset > (unsigned long long)total;
    if (reset) offset = 0;
    unsigned long n = total > 0 ? (unsigned long)((unsigned long long)total - offset) : 0;
    if (n > DS_TAIL_MAX) n = DS_TAIL_MAX;
    static unsigned char buf[DS_TAIL_MAX];
    fseek(f, (long)offset, SEEK_SET);
    n = (unsigned long)fread(buf, 1, n, f);
    fclose(f);
    char head[96]; snprintf(head, sizeof(head), "DATA %lu %ld%s\n", n, total, reset ? " RESET" : "");
    return send_text(c, head) && send_all(c, buf, n);
}

static void serve_client(ds_sock c) {
    char line[DS_LINE];
    set_status("PC connected");
    while (g_enabled && recv_line(c, line, sizeof(line))) {
        char area[16], name[64]; unsigned long long offset = 0;
        int seconds = 0;
        if (strcmp(line, "STATUS") == 0) {
            char reply[420];
            ds_lock();
            snprintf(reply, sizeof(reply), "STATUS %s\n", g_statusLine);
            ds_unlock();
            if (!send_text(c, reply)) break;
            continue;
        }
        if (strcmp(line, "ROOMS") == 0) {
            bool ok = true;
            for (int i = 0; i < g_roomCount && ok && g_roomNameFn != NULL; ++i) {
                const char *n = g_roomNameFn(i);
                char out[160]; snprintf(out, sizeof(out), "ROOM %d %s\n", i, n ? n : "");
                ok = send_text(c, out);
            }
            if (!ok || !send_text(c, "END\n")) break;
            continue;
        }
        if (strncmp(line, "CMD ", 4) == 0) {
            unsigned seq = 0;
            if (sscanf(line, "CMD ROOM %63s", name) == 1) seq = queue_command(DEBUG_CMD_ROOM, name, 0);
            else if (sscanf(line, "CMD CAPTURE %d", &seconds) == 1) seq = queue_command(DEBUG_CMD_CAPTURE, "", seconds);
            else if (strcmp(line, "CMD CAPTURE_STOP") == 0) seq = queue_command(DEBUG_CMD_CAPTURE_STOP, "", 0);
            else if (sscanf(line, "CMD MARK %63s", name) == 1) seq = queue_command(DEBUG_CMD_MARK, name, 0);
            char reply[64];
            if (seq) snprintf(reply, sizeof(reply), "OK %u\n", seq);
            else snprintf(reply, sizeof(reply), "ERR busy\n");
            if (!send_text(c, reply)) break;
            continue;
        }
        ds_lock();   /* send_file/send_tail use static buffers */
        bool keep = true;
        if (strncmp(line, "HELLO", 5) == 0) {
            char reply[96];
            snprintf(reply, sizeof(reply), "OK DRDEBUG %d PSVita\n", DEBUG_SYNC_PROTOCOL);
            if (!send_text(c, reply)) keep = false;
        } else if (strcmp(line, "LIST") == 0) {
            struct list_ctx ctx = { c, true, "devlog" };
            list_tree(&ctx, g_devDir, "", 0);
            ctx.area = "shot";
            list_tree(&ctx, g_shotDir, "", 0);
            if (!ctx.ok || !send_text(c, "END\n")) keep = false;
        } else if (strcmp(line, "LOGS") == 0) {
            const char *logs[2] = { "butterscotch-probe.log", "native-gxm-probe.log" };
            bool ok = true;
            for (int i = 0; i < 2 && ok; ++i) {
                char path[256]; snprintf(path, sizeof(path), "%s%s", g_dataDir, logs[i]);
                unsigned long long size = 0, mtime = 0;
                if (!ds_stat(path, &size, &mtime)) continue;
                char out[160]; snprintf(out, sizeof(out), "LOG %s %llu %llu\n", logs[i], size, mtime);
                ok = send_text(c, out);
            }
            if (!ok || !send_text(c, "END\n")) keep = false;
        } else if (sscanf(line, "GET %15s", area) == 1) {
            const char *root = area_root(area);
            const char *rel = line + 4 + strlen(area);
            while (*rel == ' ') ++rel;
            if (root == NULL || !safe_rel(rel)) { if (!send_text(c, "ERR refused\n")) keep = false; }
            else { char path[400]; snprintf(path, sizeof(path), "%s%s", root, rel);
                   if (!send_file(c, path)) keep = false; }
        } else if (sscanf(line, "TAIL %63s %llu", name, &offset) == 2) {
            if (!is_log_name(name)) { if (!send_text(c, "ERR refused\n")) keep = false; }
            else if (!send_tail(c, name, offset)) keep = false;
        } else if (strcmp(line, "BYE") == 0) {
            keep = false;
        } else {
            if (!send_text(c, "ERR unknown\n")) keep = false;
        }
        ds_unlock();
        if (!keep) break;
    }
    ds_close(c);
    set_status("waiting for PC");
}

/* One thread per PC client: the tray app and an auto-test run can be
 * connected at the same time. */
#define DS_MAX_CLIENTS 3
#if defined(DEBUG_SYNC_DESKTOP_TEST)
static DWORD WINAPI client_thread(LPVOID p) {
    serve_client((ds_sock)(uintptr_t)p);
    DS_DEC(&g_clientCount);
    return 0;
}
static void start_client(ds_sock c) {
    DS_INC(&g_clientCount);
    HANDLE h = CreateThread(NULL, 0, client_thread, (LPVOID)(uintptr_t)c, 0, NULL);
    if (h == NULL) { DS_DEC(&g_clientCount); ds_close(c); } else CloseHandle(h);
}
#else
static int client_thread(SceSize args, void *argp) {
    (void)args;
    ds_sock c = *(ds_sock *)argp;
    serve_client(c);
    DS_DEC(&g_clientCount);
    return sceKernelExitDeleteThread(0);
}
static void start_client(ds_sock c) {
    DS_INC(&g_clientCount);
    SceUID t = sceKernelCreateThread("drdebug_client", client_thread, 0x10000100 + 20, 48 * 1024, 0, 0, NULL);
    if (t < 0 || sceKernelStartThread(t, sizeof(c), &c) < 0) {
        DS_DEC(&g_clientCount); ds_close(c);
    }
}
#endif

#if !defined(DEBUG_SYNC_DESKTOP_TEST)
static bool net_ready(void) {
    static bool inited = false;
    if (!inited) {
        sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
        static void *pool = NULL;
        if (pool == NULL) pool = malloc(128 * 1024);
        if (pool == NULL) return false;
        SceNetInitParam p; memset(&p, 0, sizeof(p));
        p.memory = pool; p.size = 128 * 1024;
        int rc = sceNetInit(&p);
        /* Already initialised by the save sync or the log upload: fine. */
        if (rc < 0 && rc != (int)SCE_NET_ERROR_EBUSY) return false;
        sceNetCtlInit();
        inited = true;
    }
    int state = SCE_NETCTL_STATE_DISCONNECTED;
    return sceNetCtlInetGetState(&state) >= 0 && state == SCE_NETCTL_STATE_CONNECTED;
}
#endif

static void server_loop(void) {
    ds_sock listener = DS_BAD, udp = DS_BAD;
    unsigned tick = 0;
    while (g_enabled) {
#if !defined(DEBUG_SYNC_DESKTOP_TEST)
        if (!net_ready()) { set_status("no Wi-Fi"); ds_close(listener); ds_close(udp);
                            listener = udp = DS_BAD; ds_sleep_ms(3000); continue; }
#endif
        if (listener == DS_BAD) listener = ds_listen(DEBUG_SYNC_TCP_PORT);
        if (udp == DS_BAD) udp = ds_udp();
        if (listener == DS_BAD || udp == DS_BAD) { set_status("network error"); ds_sleep_ms(3000); continue; }
        set_status("waiting for PC");
        if ((tick++ % 10U) == 0U) {   /* every ~2 s */
            char msg[64];
            snprintf(msg, sizeof(msg), "DRDEBUG %d %d PSVita\n", DEBUG_SYNC_PROTOCOL, DEBUG_SYNC_TCP_PORT);
            ds_broadcast(udp, msg);
        }
        ds_sock c = g_clientCount < DS_MAX_CLIENTS ? ds_accept(listener) : DS_BAD;
        if (c != DS_BAD) start_client(c);
        else ds_sleep_ms(200);
    }
    ds_close(listener);
    ds_close(udp);
    set_status("off");
}

#if defined(DEBUG_SYNC_DESKTOP_TEST)
/* Desktop stand-in for the game's main thread: applies auto-test commands,
 * publishes STATUS and writes devlog captures like playable_main does. */
static const char *fake_rooms[] = { "room_dw_cliff_kawkawdash", "room_dw_fcastle_yellowblue",
                                    "room_town_south", "room_dw_garden_meetflowery" };
static const char *fake_room_name(int i) { return fake_rooms[i]; }
static DWORD WINAPI fake_game_thread(LPVOID p) {
    (void)p;
    DebugSync_setRoomNames(fake_room_name, 4);
    char room[96] = "room_town_south";
    unsigned frame = 0, pendingUntil = 0, captureUntil = 0, seqNo = 0;
    char pendingRoom[96] = "", last[120] = "none";
    FILE *cap = NULL;
    for (;;) {
        Sleep(33); frame++;
        DebugSyncCommand c;
        while (DebugSync_takeCommand(&c)) {
            if (c.kind == DEBUG_CMD_ROOM) {
                int known = 0; for (int i = 0; i < 4; ++i) if (strcmp(fake_rooms[i], c.arg) == 0) known = 1;
                if (!known) snprintf(last, sizeof(last), "room_unavailable:%s", c.arg);
                else if (strcmp(room, c.arg) == 0) snprintf(last, sizeof(last), "room_already:%s", c.arg);
                else { snprintf(pendingRoom, sizeof(pendingRoom), "%s", c.arg); pendingUntil = frame + 20;
                       snprintf(last, sizeof(last), "room_jump:%s", c.arg); }
            }
            else if (c.kind == DEBUG_CMD_CAPTURE) {
                char dir[300], path[400];
                snprintf(dir, sizeof(dir), "%schapter 5", g_devDir); CreateDirectoryA(dir, NULL);
                snprintf(path, sizeof(path), "%s\\%u_%s_test.log", dir, ++seqNo, room);
                cap = fopen(path, "wb");
                if (cap) fprintf(cap, "DELTARUNE_VITA_DEV_CAPTURE=1\nBUILD_VERSION=desktop-test\nCHAPTER=5 START_ROOM=%s\n", room);
                captureUntil = frame + (unsigned)c.seconds * 30U;
            } else if (c.kind == DEBUG_CMD_CAPTURE_STOP) captureUntil = frame;
        }
        if (pendingRoom[0] && frame >= pendingUntil) {
            snprintf(room, sizeof(room), "%s", pendingRoom); pendingRoom[0] = '\0';
            char line[200]; snprintf(line, sizeof(line), "ROOM_CHANGE frame=%u name=%s\n", frame, room);
            FILE *f = NULL; char lp[300]; snprintf(lp, sizeof(lp), "%sbutterscotch-probe.log", g_dataDir);
            if ((f = fopen(lp, "ab")) != NULL) { fputs(line, f); fclose(f); }
        }
        if (cap && frame % 30U == 0U)
            fprintf(cap, "DEV_SAMPLE frame=%u room=%s fps=%.2f total_us=%u\n", frame, room,
                    strstr(room, "kawkaw") ? 24.5 : 30.0, strstr(room, "kawkaw") ? 40800U : 33000U);
        if (cap && frame >= captureUntil) { fputs("DEV_CAPTURE=end\n", cap); fclose(cap); cap = NULL; }
        char st[300];
        snprintf(st, sizeof(st), "room=%s index=0 chapter=5 frame=%u fps=30.0 pending=%d capture=%d menu=0 last=%s build=desktop-test",
                 room, frame, pendingRoom[0] ? 1 : 0, cap ? 1 : 0, last);
        DebugSync_publishStatus(st);
    }
    return 0;
}

/* Test harness: debug_sync_test <devlogs\> <screenshots\> <data root\> */
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: debug_sync_test <devlogs\\> <shots\\> <data\\>\n"); return 2; }
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    snprintf(g_devDir, sizeof(g_devDir), "%s", argv[1]);
    snprintf(g_shotDir, sizeof(g_shotDir), "%s", argv[2]);
    snprintf(g_dataDir, sizeof(g_dataDir), "%s", argv[3]);
    g_enabled = true;
    fprintf(stderr, "DEBUG_SYNC_TEST serving on %d\n", DEBUG_SYNC_TCP_PORT);
    CreateThread(NULL, 0, fake_game_thread, NULL, 0, NULL);
    server_loop();
    return 0;
}
void DebugSync_setEnabled(bool enabled) { (void)enabled; }
void DebugSync_startFromConfig(void) {}
#else
static int debug_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    server_loop();
    g_threadRunning = false;
    return sceKernelExitDeleteThread(0);
}

void DebugSync_setEnabled(bool enabled) {
    g_enabled = enabled;
    if (!enabled || g_threadRunning) return;
    /* Lowest practical user priority: never competes with the game thread. */
    SceUID t = sceKernelCreateThread("drdebug", debug_thread, 0x10000100 + 20, 64 * 1024, 0, 0, NULL);
    if (t >= 0 && sceKernelStartThread(t, 0, NULL) >= 0) g_threadRunning = true;
}

void DebugSync_startFromConfig(void) {
    FILE *f = fopen("ux0:data/deltarune/config.ini", "rb");
    if (f == NULL) return;
    char text[2048]; size_t n = fread(text, 1, sizeof(text) - 1, f); fclose(f);
    text[n] = '\0';
    if (strstr(text, "debug_report_sync=1") != NULL) DebugSync_setEnabled(true);
}
#endif
