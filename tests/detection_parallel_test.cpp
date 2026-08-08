#include "dicom_processor/detection.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

dicom::VoxelVolume makeVolumeWithSpheres(int size, const std::vector<std::array<int, 4>>& spheres,
                                          float backgroundHU, float sphereHU) {
    dicom::VoxelVolume volume;
    volume.width = volume.height = volume.depth = size;
    volume.modality = dicom::Modality::CT;
    volume.data.assign(static_cast<size_t>(size) * size * size, backgroundHU);

    for (const auto& sph : spheres) {
        const int cx = sph[0], cy = sph[1], cz = sph[2], radius = sph[3];
        for (int z = 0; z < size; ++z) {
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    const double d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz);
                    if (d2 <= static_cast<double>(radius) * radius) volume.at(x, y, z) = sphereHU;
                }
            }
        }
    }
    return volume;
}

// Sort anomalies deterministically (by voxel count then centroid) so two
// runs' outputs can be compared regardless of the order components were
// discovered/merged in.
void sortAnomalies(std::vector<dicom::Anomaly>& anomalies) {
    std::sort(anomalies.begin(), anomalies.end(), [](const dicom::Anomaly& a, const dicom::Anomaly& b) {
        if (a.voxelCount != b.voxelCount) return a.voxelCount < b.voxelCount;
        return a.centroid < b.centroid;
    });
}

bool anomaliesMatch(std::vector<dicom::Anomaly> a, std::vector<dicom::Anomaly> b) {
    if (a.size() != b.size()) return false;
    sortAnomalies(a);
    sortAnomalies(b);
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].voxelCount != b[i].voxelCount) return false;
        if (a[i].centroid != b[i].centroid) return false;
        if (a[i].bboxMin != b[i].bboxMin || a[i].bboxMax != b[i].bboxMax) return false;
        if (std::abs(a[i].meanHU - b[i].meanHU) > 1e-6) return false;
        if (std::abs(a[i].stddevHU - b[i].stddevHU) > 1e-6) return false;
    }
    return true;
}

}  // namespace

int main() {
    // Deliberately includes a sphere placed to straddle likely slab
    // boundaries at several thread counts, so the union-find merge logic
    // is actually exercised, not just the easy independent-slab case.
    const int size = 50;
    const auto volume = makeVolumeWithSpheres(
        size,
        {
            {10, 10, 10, 4},   // fully inside a small-Z region
            {25, 25, 25, 8},   // spans the volume's vertical midpoint -- likely crosses a boundary
            {40, 40, 45, 3},   // near the far edge
        },
        -1000.f, 300.f);

    dicom::AnomalyDetector::Thresholds t;
    t.huMin = 100;
    t.huMax = 500;
    t.minVoxelCount = 5;

    const auto serialResult = dicom::AnomalyDetector::detect(volume, t);
    std::cout << "Serial detection found " << serialResult.size() << " anomalies\n\n";

    bool allMatch = true;
    for (size_t threadCount : {1u, 2u, 3u, 4u, 7u, 8u, 16u}) {
        dicom::ThreadPool pool(threadCount);
        const auto parallelResult = dicom::AnomalyDetector::detectParallel(volume, pool, t);
        const bool match = anomaliesMatch(serialResult, parallelResult);
        allMatch &= match;
        std::cout << (match ? "[PASS] " : "[FAIL] ") << "threadCount=" << threadCount << " ("
                  << parallelResult.size() << " anomalies) matches serial result\n";
    }

    std::cout << (allMatch ? "\nALL MATCH\n" : "\nMISMATCH DETECTED\n");
    return allMatch ? 0 : 1;
}
