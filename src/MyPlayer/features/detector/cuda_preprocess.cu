#include "cuda_preprocess.h"
#include <cuda_runtime.h>

__global__ void P010ToNV12Kernel_Y(
    const uint16_t* __restrict__ srcY, int srcYPitch,
    uint8_t* __restrict__ dstY, int dstYPitch,
    int width, int height)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    uint16_t val = srcY[y * (srcYPitch / 2) + x];
    dstY[y * dstYPitch + x] = (uint8_t)(val >> 8);
}

__global__ void P010ToNV12Kernel_UV(
    const uint16_t* __restrict__ srcUV, int srcUVPitch,
    uint8_t* __restrict__ dstUV, int dstUVPitch,
    int width, int uvHeight)
{

    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= uvHeight) return;

    uint16_t val = srcUV[y * (srcUVPitch / 2) + x];
    dstUV[y * dstUVPitch + x] = (uint8_t)(val >> 8);
}

extern "C"
void launchP010ToNV12(
    const uint16_t* srcY, int srcYPitch,
    const uint16_t* srcUV, int srcUVPitch,
    uint8_t* dstY, int dstYPitch,
    uint8_t* dstUV, int dstUVPitch,
    int width, int height, cudaStream_t stream)
{
    dim3 block(32, 8);
    dim3 gridY((width + block.x - 1) / block.x,
               (height + block.y - 1) / block.y);
    P010ToNV12Kernel_Y<<<gridY, block, 0, stream>>>(
        srcY, srcYPitch, dstY, dstYPitch, width, height);

    int uvHeight = height / 2;
    dim3 gridUV((width + block.x - 1) / block.x,
                (uvHeight + block.y - 1) / block.y);
    P010ToNV12Kernel_UV<<<gridUV, block, 0, stream>>>(
        srcUV, srcUVPitch, dstUV, dstUVPitch, width, uvHeight);
}

__global__ void LetterboxNormalizeCHWKernel(
    const uint8_t* __restrict__ rgbResized, int resizedPitch,
    int resizedW, int resizedH,
    float* __restrict__ blob,
    int padX, int padY)
{
    const int OUTPUT_SIZE = 640;
    int ox = blockIdx.x * blockDim.x + threadIdx.x;
    int oy = blockIdx.y * blockDim.y + threadIdx.y;
    if (ox >= OUTPUT_SIZE || oy >= OUTPUT_SIZE) return;

    const int totalPixels = OUTPUT_SIZE * OUTPUT_SIZE;
    const float grayVal = 114.0f / 255.0f;

    float r, g, b;

    int ix = ox - padX;
    int iy = oy - padY;

    if (ix >= 0 && ix < resizedW && iy >= 0 && iy < resizedH)
    {
        const uint8_t* pixel = rgbResized + iy * resizedPitch + ix * 3;
        r = pixel[0] * (1.0f / 255.0f);
        g = pixel[1] * (1.0f / 255.0f);
        b = pixel[2] * (1.0f / 255.0f);
    }
    else
    {
        r = g = b = grayVal;
    }

    int idx = oy * OUTPUT_SIZE + ox;
    blob[idx]                    = r;
    blob[totalPixels + idx]      = g;
    blob[2 * totalPixels + idx]  = b;
}

extern "C"
void launchLetterboxNormalizeCHW(
    const uint8_t* rgbResized, int resizedPitch,
    int resizedW, int resizedH,
    float* blob, int blobSize,
    int padX, int padY,
    float scale, cudaStream_t stream)
{
    (void)blobSize;
    (void)scale;

    const int OUTPUT_SIZE = 640;
    dim3 block(32, 8);
    dim3 grid((OUTPUT_SIZE + block.x - 1) / block.x,
              (OUTPUT_SIZE + block.y - 1) / block.y);

    LetterboxNormalizeCHWKernel<<<grid, block, 0, stream>>>(
        rgbResized, resizedPitch, resizedW, resizedH,
        blob, padX, padY);
}
