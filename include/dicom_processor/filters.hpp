#pragma once
#include "dicom_processor/voxel_volume.hpp"

namespace dicom::filters
{

    bool cpuSupportsAVX2();

    // --- Gaussian blur (separable, per-slice) --------------------------------
    VoxelVolume gaussianBlurScalar(const VoxelVolume &input, float sigma);
    VoxelVolume gaussianBlurAVX2(const VoxelVolume &input, float sigma);
    VoxelVolume gaussianBlur(const VoxelVolume &input, float sigma);

    // --- Sobel edge detection (per-slice) ------------------------------------
    VoxelVolume sobelEdgeScalar(const VoxelVolume &input);
    VoxelVolume sobelEdgeAVX2(const VoxelVolume &input);
    VoxelVolume sobelEdge(const VoxelVolume &input);

    // --- Histogram equalization (per-slice) ----------------------------------
    VoxelVolume histogramEqualizeScalar(const VoxelVolume &input, int numBins = 256);
    VoxelVolume histogramEqualizeAVX2(const VoxelVolume &input, int numBins = 256);
    VoxelVolume histogramEqualize(const VoxelVolume &input, int numBins = 256);

} // namespace dicom::filters