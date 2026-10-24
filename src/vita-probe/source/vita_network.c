#include "vita_network.h"
#include "vita_network_config.h"

#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/http.h>
#include <psp2/libssl.h>
#include <psp2/io/fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VITA_NET_POOL_SIZE  (1024U * 1024U)
#define VITA_HTTP_POOL_SIZE (1024U * 1024U)
#define VITA_SSL_POOL_SIZE  (1024U * 1024U)
#define VITA_LOG_MAX_SIZE   (2U * 1024U * 1024U)

static void set_status(char* out, size_t outSize, const char* text) {
    if (out == NULL || outSize == 0) return;
    snprintf(out, outSize, "%s", text != NULL ? text : "Unknown network error");
}

static int read_file(const char* path, unsigned char** dataOut, unsigned int* sizeOut) {
    *dataOut = NULL;
    *sizeOut = 0;
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return fd;
    SceOff end = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (end <= 0 || end > (SceOff)VITA_LOG_MAX_SIZE) {
        sceIoClose(fd);
        return -1;
    }
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    unsigned char* data = (unsigned char*)malloc((size_t)end);
    if (data == NULL) {
        sceIoClose(fd);
        return -2;
    }
    int total = 0;
    while (total < (int)end) {
        int got = sceIoRead(fd, data + total, (unsigned int)end - (unsigned int)total);
        if (got <= 0) break;
        total += got;
    }
    sceIoClose(fd);
    if (total != (int)end) {
        free(data);
        return -3;
    }
    *dataOut = data;
    *sizeOut = (unsigned int)end;
    return 0;
}

int VitaNetwork_sendLogFile(const char* logPath,
                            const char* buildVersion,
                            int chapter,
                            const char* room,
                            char* status,
                            size_t statusSize) {
    if (strstr(VITA_LOG_UPLOAD_URL, "YOUR-WORKER") != NULL ||
        strcmp(VITA_LOG_INGEST_KEY, "CHANGE-ME") == 0) {
        set_status(status, statusSize, "Worker URL/key not configured");
        return -100;
    }

    unsigned char* body = NULL;
    unsigned int bodySize = 0;
    if (read_file(logPath, &body, &bodySize) < 0) {
        set_status(status, statusSize, "Current log not found or too large");
        return -101;
    }

    void* netMemory = malloc(VITA_NET_POOL_SIZE);
    if (netMemory == NULL) {
        free(body);
        set_status(status, statusSize, "Not enough memory for network");
        return -102;
    }

    int netLoaded = 0, httpLoaded = 0, sslLoaded = 0, httpsLoaded = 0;
    int netInited = 0, ctlInited = 0, httpInited = 0, sslInited = 0;
    int tpl = -1, conn = -1, req = -1;
    int result = -103;

    int rc = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (rc >= 0 || rc == SCE_SYSMODULE_LOADED) netLoaded = 1; else goto cleanup;
    rc = sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
    if (rc >= 0 || rc == SCE_SYSMODULE_LOADED) httpLoaded = 1; else goto cleanup;
    rc = sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
    if (rc >= 0 || rc == SCE_SYSMODULE_LOADED) sslLoaded = 1; else goto cleanup;
    // HTTPS has its own sysmodule on Vita. The official VitaSDK HTTPS sample
    // loads this before issuing requests; NET+HTTP+SSL alone is insufficient
    // on some firmware/configurations and sceHttpSendRequest fails at TLS setup.
    rc = sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);
    if (rc >= 0 || rc == SCE_SYSMODULE_LOADED) httpsLoaded = 1; else goto cleanup;

    SceNetInitParam netParam;
    memset(&netParam, 0, sizeof(netParam));
    netParam.memory = netMemory;
    netParam.size = VITA_NET_POOL_SIZE;
    rc = sceNetInit(&netParam);
    if (rc >= 0) netInited = 1;

    rc = sceNetCtlInit();
    if (rc >= 0) ctlInited = 1;

    int state = SCE_NETCTL_STATE_DISCONNECTED;
    if (sceNetCtlInetGetState(&state) < 0 || state != SCE_NETCTL_STATE_CONNECTED) {
        set_status(status, statusSize, "Wi-Fi is not connected");
        result = -104;
        goto cleanup;
    }

    rc = sceSslInit(VITA_SSL_POOL_SIZE);
    if (rc >= 0) sslInited = 1; else goto cleanup;
    rc = sceHttpInit(VITA_HTTP_POOL_SIZE);
    if (rc >= 0) httpInited = 1; else goto cleanup;

    tpl = sceHttpCreateTemplate("DeltaruneVita/" PORT_BUILD_VERSION, SCE_HTTP_VERSION_1_1, 1);
    if (tpl < 0) goto cleanup;
    conn = sceHttpCreateConnectionWithURL(tpl, VITA_LOG_UPLOAD_URL, 0);
    if (conn < 0) goto cleanup;
    req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_POST, VITA_LOG_UPLOAD_URL, bodySize);
    if (req < 0) goto cleanup;

    char chapterText[16];
    snprintf(chapterText, sizeof(chapterText), "%d", chapter);
    sceHttpAddRequestHeader(req, "Content-Type", "text/plain; charset=utf-8", SCE_HTTP_HEADER_OVERWRITE);
    sceHttpAddRequestHeader(req, "X-Deltarune-Key", VITA_LOG_INGEST_KEY, SCE_HTTP_HEADER_OVERWRITE);
    sceHttpAddRequestHeader(req, "X-Deltarune-Build", buildVersion != NULL ? buildVersion : "unknown", SCE_HTTP_HEADER_OVERWRITE);
    sceHttpAddRequestHeader(req, "X-Deltarune-Chapter", chapterText, SCE_HTTP_HEADER_OVERWRITE);
    sceHttpAddRequestHeader(req, "X-Deltarune-Room", room != NULL ? room : "unknown_room", SCE_HTTP_HEADER_OVERWRITE);

    rc = sceHttpSendRequest(req, body, bodySize);
    if (rc < 0) {
        int sslError = 0;
        unsigned int sslDetail = 0;
        int sslRc = sceHttpsGetSslError(req, &sslError, &sslDetail);
        if (sslRc >= 0 && sslError != 0) {
            snprintf(status, statusSize, "Send 0x%08X SSL 0x%08X",
                     (unsigned int)rc, (unsigned int)sslError);
        } else {
            snprintf(status, statusSize, "Send failed: 0x%08X", (unsigned int)rc);
        }
        result = rc;
        goto cleanup;
    }

    int statusCode = 0;
    if (sceHttpGetStatusCode(req, &statusCode) < 0) {
        set_status(status, statusSize, "Upload response was invalid");
        result = -105;
        goto cleanup;
    }
    if (statusCode >= 200 && statusCode < 300) {
        set_status(status, statusSize, "Log sent successfully");
        result = 0;
    } else {
        snprintf(status, statusSize, "Upload failed: HTTP %d", statusCode);
        result = -statusCode;
    }

cleanup:
    if (req >= 0) sceHttpDeleteRequest(req);
    if (conn >= 0) sceHttpDeleteConnection(conn);
    if (tpl >= 0) sceHttpDeleteTemplate(tpl);
    if (httpInited) sceHttpTerm();
    if (sslInited) sceSslTerm();
    if (ctlInited) sceNetCtlTerm();
    if (netInited) sceNetTerm();
    if (httpsLoaded) sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTPS);
    if (sslLoaded) sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);
    if (httpLoaded) sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
    if (netLoaded) sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    free(netMemory);
    free(body);
    if (result == -103) set_status(status, statusSize, "Could not initialize network stack");
    return result;
}
