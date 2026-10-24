#ifndef DELTARUNE_VITA_NETWORK_H
#define DELTARUNE_VITA_NETWORK_H

#include <stddef.h>

int VitaNetwork_sendLogFile(const char* logPath,
                            const char* buildVersion,
                            int chapter,
                            const char* room,
                            char* status,
                            size_t statusSize);

#endif
