#pragma once
#include "dicom_processor/detection.hpp"
#include "dicom_processor/voxel_volume.hpp"
#include <string>
#include <vector>

namespace dicom
{

    struct SeriesInfo
    {
        std::string patientId;
        std::string modality;
        std::string studyDate;
        int sliceCount{0};
    };

    class FindingsReport
    {
    public:
        // Structured JSON: series info + one object per finding.
        static void writeJSON(const std::vector<Anomaly> &findings, const VoxelVolume &volume,
                              const SeriesInfo &series, const std::string &outputPath);

        static void writeFHIR(const std::vector<Anomaly> &findings, const VoxelVolume &volume,
                              const SeriesInfo &series, const std::string &outputPath);
    };

} // namespace dicom
