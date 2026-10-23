#include "vita_io_profile.h"

#include <psp2/kernel/processmgr.h>
#include <stdio.h>

VitaIoProfileSample VitaIoProfile_begin(const char* operation,
                                        const char* pathClass,
                                        uint64_t bytes,
                                        uint64_t queueWaitUs,
                                        bool overlappedRendering) {
    VitaIoProfileSample sample;
    sample.operation = operation != NULL ? operation : "unknown";
    sample.pathClass = pathClass != NULL ? pathClass : "unknown";
    sample.bytes = bytes;
    sample.queueWaitUs = queueWaitUs;
    sample.beginUs = sceKernelGetProcessTimeWide();
    sample.readUs = 0;
    sample.decodeUs = 0;
    sample.overlappedRendering = overlappedRendering;
    sample.success = false;
    return sample;
}

void VitaIoProfile_end(VitaIoProfileSample* sample, bool success,
                       uint64_t readUs, uint64_t decodeUs) {
    if (sample == NULL) return;
    uint64_t elapsed = sceKernelGetProcessTimeWide() - sample->beginUs;
    sample->success = success;
    sample->readUs = readUs != 0 ? readUs : elapsed;
    sample->decodeUs = decodeUs;
}

void VitaIoProfile_format(const VitaIoProfileSample* sample,
                          char* output, size_t outputSize) {
    if (output == NULL || outputSize == 0) return;
    if (sample == NULL) {
        output[0] = '\0';
        return;
    }
    uint64_t throughput = sample->readUs != 0 ?
        (sample->bytes * 1000000ULL) / sample->readUs / 1024ULL : 0;
    snprintf(output, outputSize,
             "IO_PROFILE operation=%s bytes=%llu path=%s queue_us=%llu "
             "read_us=%llu decode_us=%llu overlap=%d success=%d "
             "throughput_kib_s=%llu",
             sample->operation, (unsigned long long)sample->bytes,
             sample->pathClass, (unsigned long long)sample->queueWaitUs,
             (unsigned long long)sample->readUs,
             (unsigned long long)sample->decodeUs,
             sample->overlappedRendering ? 1 : 0,
             sample->success ? 1 : 0,
             (unsigned long long)throughput);
}
