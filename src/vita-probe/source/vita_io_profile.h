#ifndef DELTARUNE_VITA_IO_PROFILE_H
#define DELTARUNE_VITA_IO_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct VitaIoProfileSample {
    const char* operation;
    const char* pathClass;
    uint64_t bytes;
    uint64_t queueWaitUs;
    uint64_t beginUs;
    uint64_t readUs;
    uint64_t decodeUs;
    bool overlappedRendering;
    bool success;
} VitaIoProfileSample;

VitaIoProfileSample VitaIoProfile_begin(const char* operation,
                                        const char* pathClass,
                                        uint64_t bytes,
                                        uint64_t queueWaitUs,
                                        bool overlappedRendering);
void VitaIoProfile_end(VitaIoProfileSample* sample, bool success,
                       uint64_t readUs, uint64_t decodeUs);
void VitaIoProfile_format(const VitaIoProfileSample* sample,
                          char* output, size_t outputSize);

#endif
