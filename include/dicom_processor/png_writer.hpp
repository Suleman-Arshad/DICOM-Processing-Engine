#pragma once
#include "dicom_processor/detection.hpp"
#include "dicom_processor/voxel_volume.hpp"
#include <string>
#include <vector>

namespace dicom
{

    class PngWriter
    {
    public:
        static void writeSlice(const VoxelVolume &volume, int z, const std::string &outputPath,
                               double windowMin = -1000.0, double windowMax = 1000.0);

        // Same as writeSlice, but RGB with detected findings whose bounding
        // box covers this Z drawn as a red rectangle outline.
        static void writeSliceAnnotated(const VoxelVolume &volume, int z,
                                        const std::vector<Anomaly> &findings, const std::string &outputPath,
                                        double windowMin = -1000.0, double windowMax = 1000.0);
    };

} // namespace dicom