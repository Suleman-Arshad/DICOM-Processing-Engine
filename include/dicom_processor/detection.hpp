#pragma once
#include "dicom_processor/thread_pool.hpp"
#include "dicom_processor/voxel_volume.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <vector>

namespace dicom
{

    struct Anomaly
    {
        std::array<int, 3> centroid{}; // rounded to nearest voxel
        std::array<int, 3> bboxMin{};
        std::array<int, 3> bboxMax{};
        size_t voxelCount{0};
        double meanHU{0.0};
        double stddevHU{0.0};
    };

    class AnomalyDetector
    {
    public:
        struct Thresholds
        {
            double huMin;
            double huMax;
            // Density thresholding: components outside this voxel-count range
            // are discarded as noise (too small) or likely segmentation
            // errors (too large) rather than reported as findings.
            size_t minVoxelCount = 10;
            size_t maxVoxelCount = std::numeric_limits<size_t>::max();
        };

        // Serial: single flood-fill pass over the whole volume.
        static std::vector<Anomaly> detect(const VoxelVolume &volume, const Thresholds &thresholds);

        static std::vector<Anomaly> detectParallel(const VoxelVolume &volume, ThreadPool &pool,
                                                   const Thresholds &thresholds);
    };

} // namespace dicom
