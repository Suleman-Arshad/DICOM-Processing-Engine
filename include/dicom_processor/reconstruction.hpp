#pragma once
#include "dicom_processor/parser.hpp"
#include "dicom_processor/thread_pool.hpp"
#include "dicom_processor/voxel_volume.hpp"
#include <stdexcept>
#include <vector>

namespace dicom
{

    struct ReconstructionError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    class VolumeReconstructor
    {
    public:
        static VoxelVolume reconstruct(std::vector<Slice> slices, double targetSpacingMM = -1.0);
        static VoxelVolume reconstructParallel(std::vector<Slice> slices, ThreadPool &pool,
                                               double targetSpacingMM = -1.0);
    };

} // namespace dicom
