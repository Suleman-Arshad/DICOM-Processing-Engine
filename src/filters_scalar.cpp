#include "dicom_processor/filters.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dicom::filters {

namespace {

int clampCoord(int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); }

std::vector<float> makeGaussianKernel(float sigma, int& radius) {
    radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0f)));
    std::vector<float> kernel(static_cast<size_t>(2 * radius + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float v = std::exp(-(i * i) / (2.0f * sigma * sigma));
        kernel[static_cast<size_t>(i + radius)] = v;
        sum += v;
    }
    for (float& v : kernel) v /= sum;
    return kernel;
}

}  // namespace

VoxelVolume gaussianBlurScalar(const VoxelVolume& input, float sigma) {
    int radius = 0;
    const std::vector<float> kernel = makeGaussianKernel(sigma, radius);

    VoxelVolume output = input;
    std::vector<float> temp(static_cast<size_t>(input.width) * static_cast<size_t>(input.height));
    const int width = input.width;
    const int height = input.height;

    for (int z = 0; z < input.depth; ++z) {
        const float* src = input.slicePtr(z);
        float* dst = output.slicePtr(z);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float acc = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    const int sx = clampCoord(x + k, 0, width - 1);
                    acc += src[static_cast<size_t>(y) * width + sx] * kernel[static_cast<size_t>(k + radius)];
                }
                temp[static_cast<size_t>(y) * width + x] = acc;
            }
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float acc = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    const int sy = clampCoord(y + k, 0, height - 1);
                    acc += temp[static_cast<size_t>(sy) * width + x] * kernel[static_cast<size_t>(k + radius)];
                }
                dst[static_cast<size_t>(y) * width + x] = acc;
            }
        }
    }
    return output;
}

VoxelVolume sobelEdgeScalar(const VoxelVolume& input) {
    VoxelVolume output = input;
    static constexpr int gx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    static constexpr int gy[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};
    const int width = input.width;
    const int height = input.height;

    for (int z = 0; z < input.depth; ++z) {
        const float* src = input.slicePtr(z);
        float* dst = output.slicePtr(z);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float sx = 0.0f, sy = 0.0f;
                for (int ky = -1; ky <= 1; ++ky) {
                    for (int kx = -1; kx <= 1; ++kx) {
                        const int sxc = clampCoord(x + kx, 0, width - 1);
                        const int syc = clampCoord(y + ky, 0, height - 1);
                        const float v = src[static_cast<size_t>(syc) * width + sxc];
                        sx += v * static_cast<float>(gx[ky + 1][kx + 1]);
                        sy += v * static_cast<float>(gy[ky + 1][kx + 1]);
                    }
                }
                dst[static_cast<size_t>(y) * width + x] = std::sqrt(sx * sx + sy * sy);
            }
        }
    }
    return output;
}

VoxelVolume histogramEqualizeScalar(const VoxelVolume& input, int numBins) {
    VoxelVolume output = input;
    const size_t sliceSize = static_cast<size_t>(input.width) * static_cast<size_t>(input.height);

    for (int z = 0; z < input.depth; ++z) {
        const float* src = input.slicePtr(z);
        float* dst = output.slicePtr(z);

        float minV = src[0], maxV = src[0];
        for (size_t i = 0; i < sliceSize; ++i) {
            minV = std::min(minV, src[i]);
            maxV = std::max(maxV, src[i]);
        }
        const float range = (maxV > minV) ? (maxV - minV) : 1.0f;
        const float invRange = 1.0f / range;
        const float binScale = static_cast<float>(numBins - 1);

        std::vector<int> histogram(static_cast<size_t>(numBins), 0);
        std::vector<int> binIndex(sliceSize);
        for (size_t i = 0; i < sliceSize; ++i) {
            int bin = static_cast<int>((src[i] - minV) * invRange * binScale);
            bin = std::clamp(bin, 0, numBins - 1);
            binIndex[i] = bin;
            histogram[static_cast<size_t>(bin)]++;
        }

        std::vector<float> cdf(static_cast<size_t>(numBins), 0.0f);
        int cumulative = 0;
        for (int b = 0; b < numBins; ++b) {
            cumulative += histogram[static_cast<size_t>(b)];
            cdf[static_cast<size_t>(b)] = static_cast<float>(cumulative) / static_cast<float>(sliceSize);
        }

        for (size_t i = 0; i < sliceSize; ++i) {
            dst[i] = minV + cdf[static_cast<size_t>(binIndex[i])] * range;
        }
    }
    return output;
}

}  // namespace dicom::filters
