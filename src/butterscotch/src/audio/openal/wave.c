// From https://gist.github.com/SteelPh0enix/e44d4a030dd8816309af84809ed75604

#include "wave.h"
#include <stdio.h>
#include <string.h>
#include "binary_utils.h"
#include "utils.h"

// Convert 32-bit unsigned little-endian value to big-endian from byte array
static inline uint32_t little2big_u32(uint8_t const* data) {
  return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

// Convert 16-bit unsigned little-endian value to big-endian from byte array
static inline uint16_t little2big_u16(uint8_t const* data) {
  return data[0] | (data[1] << 8);
}

// Copy n bytes from source to destination and terminate the destination with
// null character. Destination must be at least (amount + 1) bytes big to
// account for null character.
static inline void bytes_to_string(uint8_t const* source,
                                   char* destination,
                                   size_t amount) {
  memcpy(destination, source, amount);
  destination[amount] = '\0';
}

// Parse the header of WAV file and return WAVFile structure with header and
// pointer to data
WAVFile WAV_ParseFileData(uint8_t const* data, size_t size) {
  WAVFile file;
  memset(&file, 0, sizeof(file));
  if (data == NULL || size < 12 || memcmp(data, "RIFF", 4) != 0 ||
      memcmp(data + 8, "WAVE", 4) != 0) return file;

  bytes_to_string(data, file.header.file_id, 4);
  file.header.file_size = little2big_u32(data + 4);
  bytes_to_string(data + 8, file.header.format, 4);

  const uint8_t* fmt = NULL;
  uint32_t fmtSize = 0;
  const uint8_t* pcm = NULL;
  uint32_t pcmSize = 0;
  size_t offset = 12;
  while (offset + 8 <= size) {
    const uint8_t* chunk = data + offset;
    uint32_t chunkSize = little2big_u32(chunk + 4);
    size_t payload = offset + 8;
    if (payload > size || chunkSize > size - payload) return file;
    if (memcmp(chunk, "fmt ", 4) == 0) {
      fmt = data + payload;
      fmtSize = chunkSize;
    } else if (memcmp(chunk, "data", 4) == 0) {
      pcm = data + payload;
      pcmSize = chunkSize;
      break;
    }
    offset = payload + chunkSize + (chunkSize & 1U);
  }
  if (fmt == NULL || fmtSize < 16 || pcm == NULL || pcmSize == 0) return file;

  bytes_to_string((const uint8_t*)"fmt ", file.header.subchunk_id, 4);
  file.header.subchunk_size = fmtSize;
  file.header.audio_format = little2big_u16(fmt + 0);
  file.header.number_of_channels = little2big_u16(fmt + 2);
  file.header.sample_rate = little2big_u32(fmt + 4);
  file.header.byte_rate = little2big_u32(fmt + 8);
  file.header.block_align = little2big_u16(fmt + 12);
  file.header.bits_per_sample = little2big_u16(fmt + 14);
  bytes_to_string((const uint8_t*)"data", file.header.data_id, 4);
  file.header.data_size = pcmSize;

  // The chunk was bounded against the AUDO entry above, so this allocation
  // can never use a forged/truncated WAV size from adjacent game data.
  file.data = (uint8_t *)malloc(pcmSize);
  if (file.data == NULL) return file;
  memcpy(file.data, pcm, pcmSize);
  file.data_length = pcmSize;

  if (file.header.bits_per_sample == 16) {
    for (size_t i = 0; i < (file.header.data_size / 2); i++) {
        uint8_t* p = (uint8_t*)file.data + i * 2;

        uint16_t val = (uint16_t)(p[0] | (p[1] << 8));
        val = BinaryUtils_toLittle16(val);

        p[0] = (uint8_t)(val & 0xFF);
        p[1] = (uint8_t)(val >> 8);
      }
  }

  return file;
}
