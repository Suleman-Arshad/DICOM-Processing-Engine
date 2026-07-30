#include "dicom_processor/filters.hpp"

namespace dicom::filters
{

    bool cpuSupportsAVX2()
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_cpu_supports("avx2");
#else
        return false;
#endif
    }

    VoxelVolume gaussianBlur(const VoxelVolume &input, float sigma)
    {
        return cpuSupportsAVX2() ? gaussianBlurAVX2(input, sigma) : gaussianBlurScalar(input, sigma);
    }

    VoxelVolume sobelEdge(const VoxelVolume &input)
    {
        return cpuSupportsAVX2() ? sobelEdgeAVX2(input) : sobelEdgeScalar(input);
    }

    VoxelVolume histogramEqualize(const VoxelVolume &input, int numBins)
    {
        return cpuSupportsAVX2() ? histogramEqualizeAVX2(input, numBins) : histogramEqualizeScalar(input, numBins);
    }

} // namespace dicom::filters