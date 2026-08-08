#pragma once
// detection.hpp — Week 7: 3D seed-expansion region growing within
// Hounsfield thresholds, connected component labeling, and density
// (voxel-count) thresholding.
//
// Design note: "region growing from a seed" and "connected component
// labeling of a thresholded volume" are the same underlying operation
// here. A voxel within [huMin, huMax] is a valid seed; growing a region
// from it via 26-connectivity flood fill and marking every visited voxel
// is exactly what a flood-fill connected-component labeler does. Scanning
// the whole volume and flood-filling from every not-yet-visited in-range
// voxel therefore performs region growing from every implicit seed AND
// produces full connected component labels in one pass — this project
// doesn't need two separate algorithms for what's the same graph
// traversal under two different names.

#include "dicom_processor/thread_pool.hpp"
#include "dicom_processor/voxel_volume.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <vector>

namespace dicom {

struct Anomaly {
    std::array<int, 3> centroid{};   // rounded to nearest voxel
    std::array<int, 3> bboxMin{};
    std::array<int, 3> bboxMax{};
    size_t voxelCount{0};
    double meanHU{0.0};
    double stddevHU{0.0};
};

class AnomalyDetector {
public:
    struct Thresholds {
        double huMin;
        double huMax;
        // Density thresholding: components outside this voxel-count range
        // are discarded as noise (too small) or likely segmentation
        // errors (too large) rather than reported as findings.
        size_t minVoxelCount = 10;
        size_t maxVoxelCount = std::numeric_limits<size_t>::max();
    };

    // Serial: single flood-fill pass over the whole volume.
    static std::vector<Anomaly> detect(const VoxelVolume& volume, const Thresholds& thresholds);

    // Parallel: partitions the volume into contiguous Z-slabs (one per
    // pool thread, up to volume.depth), flood-fills each slab
    // independently and concurrently, then merges components that touch
    // across slab boundaries via union-find. Produces the same set of
    // anomalies as detect() regardless of thread count (verified in
    // tests/detection_parallel_test.cpp) -- slab partitioning is an
    // implementation detail of how the work is scheduled, not a change in
    // what counts as connected.
    static std::vector<Anomaly> detectParallel(const VoxelVolume& volume, ThreadPool& pool,
                                                 const Thresholds& thresholds);
};

}  // namespace dicom
