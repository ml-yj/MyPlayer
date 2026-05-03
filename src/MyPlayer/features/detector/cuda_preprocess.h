#pragma once

#include <cstdint>
#include <cuda_runtime.h>

extern "C" {
void launchP010ToNV12(
    const uint16_t* srcY, int srcYPitch,
    const uint16_t* srcUV, int srcUVPitch,
    uint8_t* dstY, int dstYPitch,
    uint8_t* dstUV, int dstUVPitch,
    int width, int height, cudaStream_t stream);

void launchLetterboxNormalizeCHW(
    const uint8_t* rgbResized, int resizedPitch,
    int resizedW, int resizedH,
    float* blob, int blobSize,
    int padX, int padY,
    float scale, cudaStream_t stream);
}
