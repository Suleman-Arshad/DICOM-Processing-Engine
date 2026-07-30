#include "dicom_processor/reconstruction.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace dicom
{

    namespace
    {

        int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); }

        float trilinearSample(const std::vector<float> &grid, int w, int h, int d, double fx, double fy,
                              double fz)
        {
            const int x0 = clampInt(static_cast<int>(std::floor(fx)), 0, w - 1);
            const int y0 = clampInt(static_cast<int>(std::floor(fy)), 0, h - 1);
            const int z0 = clampInt(static_cast<int>(std::floor(fz)), 0, d - 1);
            const int x1 = clampInt(x0 + 1, 0, w - 1);
            const int y1 = clampInt(y0 + 1, 0, h - 1);
            const int z1 = clampInt(z0 + 1, 0, d - 1);

            const double tx = (x1 == x0) ? 0.0 : (fx - x0);
            const double ty = (y1 == y0) ? 0.0 : (fy - y0);
            const double tz = (z1 == z0) ? 0.0 : (fz - z0);

            auto at = [&](int x, int y, int z) -> double
            {
                return grid[(static_cast<size_t>(z) * h + y) * w + x];
            };

            const double c00 = at(x0, y0, z0) * (1 - tx) + at(x1, y0, z0) * tx;
            const double c01 = at(x0, y0, z1) * (1 - tx) + at(x1, y0, z1) * tx;
            const double c10 = at(x0, y1, z0) * (1 - tx) + at(x1, y1, z0) * tx;
            const double c11 = at(x0, y1, z1) * (1 - tx) + at(x1, y1, z1) * tx;

            const double c0 = c00 * (1 - ty) + c10 * ty;
            const double c1 = c01 * (1 - ty) + c11 * ty;

            return static_cast<float>(c0 * (1 - tz) + c1 * tz);
        }

    } // namespace

    VoxelVolume VolumeReconstructor::reconstruct(std::vector<Slice> slices, double targetSpacingMM)
    {
        if (slices.empty())
        {
            throw ReconstructionError("Cannot reconstruct a volume from zero slices");
        }

        const int rows = slices.front().rows;
        const int cols = slices.front().columns;
        for (const auto &s : slices)
        {
            if (s.rows != rows || s.columns != cols)
            {
                std::ostringstream oss;
                oss << "Inconsistent slice dimensions: expected " << rows << "x" << cols
                    << " but found " << s.rows << "x" << s.columns
                    << " -- all slices in a series must share the same in-plane grid";
                throw ReconstructionError(oss.str());
            }
        }

        std::sort(slices.begin(), slices.end(),
                  [](const Slice &a, const Slice &b)
                  { return a.imagePositionZ < b.imagePositionZ; });

        const int depth = static_cast<int>(slices.size());

        // Native (anisotropic) spacing.
        const double dx = slices.front().pixelSpacingColMM > 0 ? slices.front().pixelSpacingColMM : 1.0;
        const double dy = slices.front().pixelSpacingRowMM > 0 ? slices.front().pixelSpacingRowMM : 1.0;
        double dz = slices.front().sliceThicknessMM > 0 ? slices.front().sliceThicknessMM : 1.0;
        if (depth > 1)
        {
            const double totalSpan = slices.back().imagePositionZ - slices.front().imagePositionZ;
            if (std::abs(totalSpan) > 1e-6)
            {
                dz = std::abs(totalSpan) / (depth - 1);
            }
        }

        // Build the native-resolution grid: convert every slice's pixels to HU/rescaled intensity using *that slice's own* rescale parameters.
        std::vector<float> nativeGrid(static_cast<size_t>(rows) * cols * depth);
        for (int z = 0; z < depth; ++z)
        {
            const Slice &s = slices[static_cast<size_t>(z)];
            for (int y = 0; y < rows; ++y)
            {
                for (int x = 0; x < cols; ++x)
                {
                    nativeGrid[(static_cast<size_t>(z) * rows + y) * cols + x] =
                        static_cast<float>(s.huAt(x, y));
                }
            }
        }

        const double spacing = targetSpacingMM > 0 ? targetSpacingMM : std::min({dx, dy, dz});

        const double widthMM = (cols - 1) * dx;
        const double heightMM = (rows - 1) * dy;
        const double depthMM = (depth - 1) * dz;

        const int outW = std::max(1, static_cast<int>(std::round(widthMM / spacing)) + 1);
        const int outH = std::max(1, static_cast<int>(std::round(heightMM / spacing)) + 1);
        const int outD = std::max(1, static_cast<int>(std::round(depthMM / spacing)) + 1);

        VoxelVolume volume;
        volume.width = outW;
        volume.height = outH;
        volume.depth = outD;
        volume.voxelSpacingMM = spacing;
        volume.modality = modalityFromString(slices.front().modality);
        volume.data.resize(static_cast<size_t>(outW) * outH * outD);

        for (int oz = 0; oz < outD; ++oz)
        {
            const double fz = (oz * spacing) / dz;
            for (int oy = 0; oy < outH; ++oy)
            {
                const double fy = (oy * spacing) / dy;
                for (int ox = 0; ox < outW; ++ox)
                {
                    const double fx = (ox * spacing) / dx;
                    volume.at(ox, oy, oz) = trilinearSample(nativeGrid, cols, rows, depth, fx, fy, fz);
                }
            }
        }

        return volume;
    }

} // namespace dicom