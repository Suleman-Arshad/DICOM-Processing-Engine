#include "dicom_processor/filters.hpp"
#include <immintrin.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace dicom::filters
{

    namespace
    {

        int clampCoord(int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); }

        std::vector<float> makeGaussianKernel(float sigma, int &radius)
        {
            radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0f)));
            std::vector<float> kernel(static_cast<size_t>(2 * radius + 1));
            float sum = 0.0f;
            for (int i = -radius; i <= radius; ++i)
            {
                const float v = std::exp(-(i * i) / (2.0f * sigma * sigma));
                kernel[static_cast<size_t>(i + radius)] = v;
                sum += v;
            }
            for (float &v : kernel)
                v /= sum;
            return kernel;
        }

    } // namespace

    VoxelVolume gaussianBlurAVX2(const VoxelVolume &input, float sigma)
    {
        int radius = 0;
        const std::vector<float> kernel = makeGaussianKernel(sigma, radius);

        VoxelVolume output = input;
        std::vector<float> temp(static_cast<size_t>(input.width) * static_cast<size_t>(input.height));
        const int width = input.width;
        const int height = input.height;

        for (int z = 0; z < input.depth; ++z)
        {
            const float *src = input.slicePtr(z);
            float *dst = output.slicePtr(z);

            // --- Horizontal pass -> temp ---
            for (int y = 0; y < height; ++y)
            {
                const float *row = src + static_cast<size_t>(y) * width;
                float *trow = temp.data() + static_cast<size_t>(y) * width;

                int x = 0;
                // Left border: neighbors need per-lane clamping, do scalar.
                for (; x < std::min(radius, width); ++x)
                {
                    float acc = 0.0f;
                    for (int k = -radius; k <= radius; ++k)
                    {
                        acc += row[clampCoord(x + k, 0, width - 1)] * kernel[static_cast<size_t>(k + radius)];
                    }
                    trow[x] = acc;
                }
                // Interior: x+k always in range, safe for unclamped vector loads.
                const int interiorEnd = std::max(x, width - radius);
                for (; x + 8 <= interiorEnd; x += 8)
                {
                    __m256 acc = _mm256_setzero_ps();
                    for (int k = -radius; k <= radius; ++k)
                    {
                        const __m256 v = _mm256_loadu_ps(row + x + k);
                        const __m256 w = _mm256_set1_ps(kernel[static_cast<size_t>(k + radius)]);
                        acc = _mm256_fmadd_ps(v, w, acc);
                    }
                    _mm256_storeu_ps(trow + x, acc);
                }
                // Remainder (interior tail + right border): scalar.
                for (; x < width; ++x)
                {
                    float acc = 0.0f;
                    for (int k = -radius; k <= radius; ++k)
                    {
                        acc += row[clampCoord(x + k, 0, width - 1)] * kernel[static_cast<size_t>(k + radius)];
                    }
                    trow[x] = acc;
                }
            }

            // --- Vertical pass -> dst ---
            // Y-clamping picks one row per (y,k) pair (not per-lane), so the
            // entire width can be vectorized with no border special-casing.
            for (int y = 0; y < height; ++y)
            {
                float *drow = dst + static_cast<size_t>(y) * width;
                int x = 0;
                for (; x + 8 <= width; x += 8)
                {
                    __m256 acc = _mm256_setzero_ps();
                    for (int k = -radius; k <= radius; ++k)
                    {
                        const int sy = clampCoord(y + k, 0, height - 1);
                        const float *srow = temp.data() + static_cast<size_t>(sy) * width;
                        const __m256 v = _mm256_loadu_ps(srow + x);
                        const __m256 w = _mm256_set1_ps(kernel[static_cast<size_t>(k + radius)]);
                        acc = _mm256_fmadd_ps(v, w, acc);
                    }
                    _mm256_storeu_ps(drow + x, acc);
                }
                for (; x < width; ++x)
                {
                    float acc = 0.0f;
                    for (int k = -radius; k <= radius; ++k)
                    {
                        const int sy = clampCoord(y + k, 0, height - 1);
                        acc += temp[static_cast<size_t>(sy) * width + x] * kernel[static_cast<size_t>(k + radius)];
                    }
                    drow[x] = acc;
                }
            }
        }
        return output;
    }

    VoxelVolume sobelEdgeAVX2(const VoxelVolume &input)
    {
        VoxelVolume output = input;
        const int width = input.width;
        const int height = input.height;

        static constexpr int gxk[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
        static constexpr int gyk[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};

        for (int z = 0; z < input.depth; ++z)
        {
            const float *src = input.slicePtr(z);
            float *dst = output.slicePtr(z);

            for (int y = 0; y < height; ++y)
            {
                const int yPrev = clampCoord(y - 1, 0, height - 1);
                const int yNext = clampCoord(y + 1, 0, height - 1);
                const float *rowPrev = src + static_cast<size_t>(yPrev) * width;
                const float *rowCur = src + static_cast<size_t>(y) * width;
                const float *rowNext = src + static_cast<size_t>(yNext) * width;
                float *drow = dst + static_cast<size_t>(y) * width;

                auto scalarSobel = [&](int x)
                {
                    float sx = 0.0f, sy = 0.0f;
                    for (int ky = -1; ky <= 1; ++ky)
                    {
                        for (int kx = -1; kx <= 1; ++kx)
                        {
                            const int sxc = clampCoord(x + kx, 0, width - 1);
                            const int syc = clampCoord(y + ky, 0, height - 1);
                            const float v = src[static_cast<size_t>(syc) * width + sxc];
                            sx += v * static_cast<float>(gxk[ky + 1][kx + 1]);
                            sy += v * static_cast<float>(gyk[ky + 1][kx + 1]);
                        }
                    }
                    drow[x] = std::sqrt(sx * sx + sy * sy);
                };

                if (width == 1)
                {
                    scalarSobel(0);
                    continue;
                }

                scalarSobel(0); // left border (needs x-1, out of range)

                int x = 1;
                // Interior: x-1 and x+1 both in range for the whole 8-wide group.
                for (; x + 8 <= width - 1; x += 8)
                {
                    const __m256 tl = _mm256_loadu_ps(rowPrev + x - 1);
                    const __m256 tc = _mm256_loadu_ps(rowPrev + x);
                    const __m256 tr = _mm256_loadu_ps(rowPrev + x + 1);
                    const __m256 ml = _mm256_loadu_ps(rowCur + x - 1);
                    const __m256 mr = _mm256_loadu_ps(rowCur + x + 1);
                    const __m256 bl = _mm256_loadu_ps(rowNext + x - 1);
                    const __m256 bc = _mm256_loadu_ps(rowNext + x);
                    const __m256 br = _mm256_loadu_ps(rowNext + x + 1);

                    const __m256 two = _mm256_set1_ps(2.0f);
                    // Gx = (tr + 2*mr + br) - (tl + 2*ml + bl)
                    const __m256 gx = _mm256_sub_ps(
                        _mm256_add_ps(_mm256_add_ps(tr, _mm256_mul_ps(two, mr)), br),
                        _mm256_add_ps(_mm256_add_ps(tl, _mm256_mul_ps(two, ml)), bl));
                    // Gy = (bl + 2*bc + br) - (tl + 2*tc + tr)
                    const __m256 gy = _mm256_sub_ps(
                        _mm256_add_ps(_mm256_add_ps(bl, _mm256_mul_ps(two, bc)), br),
                        _mm256_add_ps(_mm256_add_ps(tl, _mm256_mul_ps(two, tc)), tr));

                    const __m256 mag = _mm256_sqrt_ps(_mm256_fmadd_ps(gx, gx, _mm256_mul_ps(gy, gy)));
                    _mm256_storeu_ps(drow + x, mag);
                }
                for (; x <= width - 2; ++x)
                {
                    scalarSobel(x);
                }
                scalarSobel(width - 1); // right border (needs x+1, out of range)
            }
        }
        return output;
    }

    VoxelVolume histogramEqualizeAVX2(const VoxelVolume &input, int numBins)
    {
        VoxelVolume output = input;
        const size_t sliceSize = static_cast<size_t>(input.width) * static_cast<size_t>(input.height);

        for (int z = 0; z < input.depth; ++z)
        {
            const float *src = input.slicePtr(z);
            float *dst = output.slicePtr(z);

            // --- Scalar: histogram + CDF (data-dependent bucket accumulation
            // doesn't parallelize safely across lanes without cross-lane
            // conflicts, so this stays scalar per the architecture design). ---
            float minV = src[0], maxV = src[0];
            for (size_t i = 0; i < sliceSize; ++i)
            {
                minV = std::min(minV, src[i]);
                maxV = std::max(maxV, src[i]);
            }
            const float range = (maxV > minV) ? (maxV - minV) : 1.0f;
            const float invRange = 1.0f / range;
            const float binScale = static_cast<float>(numBins - 1);

            std::vector<int> histogram(static_cast<size_t>(numBins), 0);
            for (size_t i = 0; i < sliceSize; ++i)
            {
                int bin = static_cast<int>((src[i] - minV) * invRange * binScale);
                bin = std::clamp(bin, 0, numBins - 1);
                histogram[static_cast<size_t>(bin)]++;
            }
            std::vector<float> cdf(static_cast<size_t>(numBins), 0.0f);
            int cumulative = 0;
            for (int b = 0; b < numBins; ++b)
            {
                cumulative += histogram[static_cast<size_t>(b)];
                cdf[static_cast<size_t>(b)] = static_cast<float>(cumulative) / static_cast<float>(sliceSize);
            }

            // --- AVX2: vectorized remapping via gather from the CDF table. ---
            const __m256 vMin = _mm256_set1_ps(minV);
            const __m256 vInvRange = _mm256_set1_ps(invRange);
            const __m256 vBinScale = _mm256_set1_ps(binScale);
            const __m256i vMaxBin = _mm256_set1_epi32(numBins - 1);
            const __m256i vZero = _mm256_setzero_si256();
            const __m256 vRange = _mm256_set1_ps(range);

            size_t i = 0;
            for (; i + 8 <= sliceSize; i += 8)
            {
                const __m256 v = _mm256_loadu_ps(src + i);
                const __m256 normalized = _mm256_mul_ps(_mm256_sub_ps(v, vMin), vInvRange);
                const __m256 binF = _mm256_mul_ps(normalized, vBinScale);
                __m256i binI = _mm256_cvttps_epi32(binF);
                binI = _mm256_max_epi32(vZero, _mm256_min_epi32(binI, vMaxBin));

                const __m256 cdfVals = _mm256_i32gather_ps(cdf.data(), binI, 4);
                const __m256 result = _mm256_fmadd_ps(cdfVals, vRange, vMin);
                _mm256_storeu_ps(dst + i, result);
            }
            for (; i < sliceSize; ++i)
            {
                int bin = static_cast<int>((src[i] - minV) * invRange * binScale);
                bin = std::clamp(bin, 0, numBins - 1);
                dst[i] = minV + cdf[static_cast<size_t>(bin)] * range;
            }
        }
        return output;
    }

} // namespace dicom::filters