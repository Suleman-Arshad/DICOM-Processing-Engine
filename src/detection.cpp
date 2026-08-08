#include "dicom_processor/detection.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>

namespace dicom {

namespace {

// Running statistics for one connected component, accumulated voxel-by-
// voxel during flood fill and mergeable across slab boundaries without
// re-scanning any voxels.
struct ComponentStats {
    size_t voxelCount{0};
    double sumX{0}, sumY{0}, sumZ{0};
    double sumHU{0}, sumHU2{0};
    int bboxMinX{std::numeric_limits<int>::max()};
    int bboxMinY{std::numeric_limits<int>::max()};
    int bboxMinZ{std::numeric_limits<int>::max()};
    int bboxMaxX{std::numeric_limits<int>::min()};
    int bboxMaxY{std::numeric_limits<int>::min()};
    int bboxMaxZ{std::numeric_limits<int>::min()};

    void addVoxel(int x, int y, int z, double hu) {
        ++voxelCount;
        sumX += x;
        sumY += y;
        sumZ += z;
        sumHU += hu;
        sumHU2 += hu * hu;
        bboxMinX = std::min(bboxMinX, x);
        bboxMinY = std::min(bboxMinY, y);
        bboxMinZ = std::min(bboxMinZ, z);
        bboxMaxX = std::max(bboxMaxX, x);
        bboxMaxY = std::max(bboxMaxY, y);
        bboxMaxZ = std::max(bboxMaxZ, z);
    }

    void merge(const ComponentStats& other) {
        voxelCount += other.voxelCount;
        sumX += other.sumX;
        sumY += other.sumY;
        sumZ += other.sumZ;
        sumHU += other.sumHU;
        sumHU2 += other.sumHU2;
        bboxMinX = std::min(bboxMinX, other.bboxMinX);
        bboxMinY = std::min(bboxMinY, other.bboxMinY);
        bboxMinZ = std::min(bboxMinZ, other.bboxMinZ);
        bboxMaxX = std::max(bboxMaxX, other.bboxMaxX);
        bboxMaxY = std::max(bboxMaxY, other.bboxMaxY);
        bboxMaxZ = std::max(bboxMaxZ, other.bboxMaxZ);
    }

    Anomaly toAnomaly() const {
        Anomaly a;
        a.voxelCount = voxelCount;
        const double n = static_cast<double>(voxelCount);
        a.centroid = {static_cast<int>(std::lround(sumX / n)), static_cast<int>(std::lround(sumY / n)),
                      static_cast<int>(std::lround(sumZ / n))};
        a.bboxMin = {bboxMinX, bboxMinY, bboxMinZ};
        a.bboxMax = {bboxMaxX, bboxMaxY, bboxMaxZ};
        a.meanHU = sumHU / n;
        // Population variance = E[X^2] - E[X]^2; clamp at 0 since floating
        // point roundoff can otherwise push a near-zero variance slightly
        // negative for a near-uniform component.
        const double variance = std::max(0.0, sumHU2 / n - a.meanHU * a.meanHU);
        a.stddevHU = std::sqrt(variance);
        return a;
    }
};

// Local flood-fill labeling result for one contiguous Z-slab [zStart, zEnd)
// of the volume. `labels` is sized to just this slab (w*h*(zEnd-zStart)),
// not the whole volume, so parallel tasks never write outside their own
// disjoint allocation.
struct LocalLabeling {
    std::vector<int> labels;  // 0 = background/out-of-threshold; >0 = local component id
    std::vector<ComponentStats> components;  // components[label-1]
};

bool inMask(const VoxelVolume& volume, int x, int y, int z, double huMin, double huMax) {
    const float v = volume.at(x, y, z);
    return v >= static_cast<float>(huMin) && v <= static_cast<float>(huMax);
}

// Flood-fills every not-yet-visited in-range voxel in [zStart, zEnd),
// using 26-connectivity but never stepping outside the given Z range
// (that's what makes independent slabs safe to run concurrently with no
// locking -- each task only ever reads/writes within its own slab).
LocalLabeling floodFillLabelRegion(const VoxelVolume& volume, int zStart, int zEnd, double huMin,
                                    double huMax) {
    const int w = volume.width;
    const int h = volume.height;
    const int slabDepth = zEnd - zStart;

    LocalLabeling result;
    result.labels.assign(static_cast<size_t>(w) * h * slabDepth, 0);

    auto localIndex = [&](int x, int y, int localZ) -> size_t {
        return (static_cast<size_t>(localZ) * h + y) * w + x;
    };

    int nextLabel = 1;
    std::vector<std::array<int, 3>> stack;  // (x, y, localZ) -- explicit stack avoids recursion depth limits

    for (int z = zStart; z < zEnd; ++z) {
        const int localZ = z - zStart;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t idx = localIndex(x, y, localZ);
                if (result.labels[idx] != 0) continue;
                if (!inMask(volume, x, y, z, huMin, huMax)) continue;

                const int label = nextLabel++;
                ComponentStats stats;
                stack.clear();
                stack.push_back({x, y, localZ});
                result.labels[idx] = label;

                while (!stack.empty()) {
                    const auto [cx, cy, clz] = stack.back();
                    stack.pop_back();
                    const int cz = clz + zStart;
                    stats.addVoxel(cx, cy, cz, volume.at(cx, cy, cz));

                    for (int dz = -1; dz <= 1; ++dz) {
                        const int nlz = clz + dz;
                        if (nlz < 0 || nlz >= slabDepth) continue;  // stay within this slab
                        const int nz = nlz + zStart;
                        for (int dy = -1; dy <= 1; ++dy) {
                            const int ny = cy + dy;
                            if (ny < 0 || ny >= h) continue;
                            for (int dx = -1; dx <= 1; ++dx) {
                                if (dx == 0 && dy == 0 && dz == 0) continue;
                                const int nx = cx + dx;
                                if (nx < 0 || nx >= w) continue;

                                const size_t nidx = localIndex(nx, ny, nlz);
                                if (result.labels[nidx] != 0) continue;
                                if (!inMask(volume, nx, ny, nz, huMin, huMax)) continue;

                                result.labels[nidx] = label;
                                stack.push_back({nx, ny, nlz});
                            }
                        }
                    }
                }
                result.components.push_back(stats);
            }
        }
    }
    return result;
}

std::vector<Anomaly> applyThresholding(const std::vector<ComponentStats>& components,
                                        const AnomalyDetector::Thresholds& t) {
    std::vector<Anomaly> results;
    for (const auto& comp : components) {
        if (comp.voxelCount == 0) continue;
        if (comp.voxelCount < t.minVoxelCount || comp.voxelCount > t.maxVoxelCount) continue;
        results.push_back(comp.toAnomaly());
    }
    return results;
}

}  // namespace

std::vector<Anomaly> AnomalyDetector::detect(const VoxelVolume& volume, const Thresholds& thresholds) {
    if (volume.depth == 0) return {};
    const LocalLabeling labeling = floodFillLabelRegion(volume, 0, volume.depth, thresholds.huMin,
                                                          thresholds.huMax);
    return applyThresholding(labeling.components, thresholds);
}

std::vector<Anomaly> AnomalyDetector::detectParallel(const VoxelVolume& volume, ThreadPool& pool,
                                                       const Thresholds& thresholds) {
    if (volume.depth == 0) return {};

    const size_t numSlabs = std::max<size_t>(
        1, std::min<size_t>(pool.threadCount(), static_cast<size_t>(volume.depth)));

    // Partition [0, depth) into numSlabs contiguous, near-equal ranges.
    std::vector<std::pair<int, int>> ranges;
    ranges.reserve(numSlabs);
    const int depth = volume.depth;
    const int base = depth / static_cast<int>(numSlabs);
    const int remainder = depth % static_cast<int>(numSlabs);
    int z = 0;
    for (size_t i = 0; i < numSlabs; ++i) {
        const int len = base + (static_cast<int>(i) < remainder ? 1 : 0);
        if (len == 0) continue;
        ranges.push_back({z, z + len});
        z += len;
    }

    // --- Parallel phase: label each slab independently. No locking needed
    // -- every task writes only to its own LocalLabeling allocation. ---
    std::vector<std::future<LocalLabeling>> futures;
    futures.reserve(ranges.size());
    for (const auto& r : ranges) {
        const double huMin = thresholds.huMin, huMax = thresholds.huMax;
        futures.push_back(
            pool.submit([&volume, r, huMin, huMax] { return floodFillLabelRegion(volume, r.first, r.second, huMin, huMax); }));
    }
    std::vector<LocalLabeling> slabResults;
    slabResults.reserve(ranges.size());
    for (auto& f : futures) {
        slabResults.push_back(f.get());
    }

    // --- Serial phase: union-find merge across slab boundaries. Cheap --
    // only touches the two boundary planes between each pair of adjacent
    // slabs, not the full volume. ---
    std::vector<size_t> prefixCount(ranges.size() + 1, 0);
    for (size_t i = 0; i < ranges.size(); ++i) {
        prefixCount[i + 1] = prefixCount[i] + slabResults[i].components.size();
    }
    const size_t totalComponents = prefixCount.back();

    std::vector<size_t> parent(totalComponents);
    std::iota(parent.begin(), parent.end(), 0);
    std::function<size_t(size_t)> find = [&](size_t idx) {
        while (parent[idx] != idx) {
            parent[idx] = parent[parent[idx]];  // path halving
            idx = parent[idx];
        }
        return idx;
    };
    auto unite = [&](size_t a, size_t b) {
        a = find(a);
        b = find(b);
        if (a != b) parent[a] = b;
    };
    auto globalId = [&](size_t slabIdx, int localLabel) {
        return prefixCount[slabIdx] + static_cast<size_t>(localLabel - 1);
    };

    const int w = volume.width, h = volume.height;
    for (size_t i = 0; i + 1 < ranges.size(); ++i) {
        const int localZLast = ranges[i].second - 1 - ranges[i].first;
        const auto& labelsA = slabResults[i].labels;
        const auto& labelsB = slabResults[i + 1].labels;  // localZFirst == 0

        auto idxA = [&](int x, int y) -> size_t { return (static_cast<size_t>(localZLast) * h + y) * w + x; };
        auto idxB = [&](int x, int y) -> size_t { return (static_cast<size_t>(y)) * w + x; };

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int labelA = labelsA[idxA(x, y)];
                if (labelA == 0) continue;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int ny = y + dy;
                    if (ny < 0 || ny >= h) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx;
                        if (nx < 0 || nx >= w) continue;
                        const int labelB = labelsB[idxB(nx, ny)];
                        if (labelB == 0) continue;
                        unite(globalId(i, labelA), globalId(i + 1, labelB));
                    }
                }
            }
        }
    }

    // Aggregate each local component's stats into its union-find root.
    std::vector<ComponentStats> merged(totalComponents);
    for (size_t i = 0; i < ranges.size(); ++i) {
        for (size_t li = 0; li < slabResults[i].components.size(); ++li) {
            const size_t gid = globalId(i, static_cast<int>(li) + 1);
            const size_t root = find(gid);
            merged[root].merge(slabResults[i].components[li]);
        }
    }

    // Only root entries carry a merged, non-empty component.
    std::vector<ComponentStats> finalComponents;
    for (size_t i = 0; i < totalComponents; ++i) {
        if (find(i) == i && merged[i].voxelCount > 0) {
            finalComponents.push_back(merged[i]);
        }
    }

    return applyThresholding(finalComponents, thresholds);
}

}  // namespace dicom
