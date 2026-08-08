#pragma once
#include "dicom_processor/voxel_volume.hpp"

namespace dicom::filters {

bool cpuSupportsAVX2();

VoxelVolume gaussianBlurScalar(const VoxelVolume& input, float sigma);
VoxelVolume gaussianBlurAVX2(const VoxelVolume& input, float sigma);
VoxelVolume gaussianBlur(const VoxelVolume& input, float sigma);

VoxelVolume sobelEdgeScalar(const VoxelVolume& input);
VoxelVolume sobelEdgeAVX2(const VoxelVolume& input);
VoxelVolume sobelEdge(const VoxelVolume& input);

VoxelVolume histogramEqualizeScalar(const VoxelVolume& input, int numBins = 256);
VoxelVolume histogramEqualizeAVX2(const VoxelVolume& input, int numBins = 256);
VoxelVolume histogramEqualize(const VoxelVolume& input, int numBins = 256);

}  // namespace dicom::filters
