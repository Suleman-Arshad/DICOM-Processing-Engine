#include "dicom_processor/detection.hpp"
#include <cmath>
#include <iostream>

namespace
{

    int testsRun = 0;
    int testsFailed = 0;

    void check(bool condition, const std::string &description)
    {
        ++testsRun;
        if (!condition)
        {
            ++testsFailed;
            std::cout << "[FAIL] " << description << '\n';
        }
        else
        {
            std::cout << "[PASS] " << description << '\n';
        }
    }

    // Builds a volume of background HU with a single sphere of a different HU
    // planted at a known center/radius -- the ground truth this test checks
    // the detector's output against.
    dicom::VoxelVolume makeVolumeWithSphere(int size, int cx, int cy, int cz, int radius,
                                            float backgroundHU, float sphereHU)
    {
        dicom::VoxelVolume volume;
        volume.width = volume.height = volume.depth = size;
        volume.modality = dicom::Modality::CT;
        volume.data.assign(static_cast<size_t>(size) * size * size, backgroundHU);

        size_t plantedVoxels = 0;
        for (int z = 0; z < size; ++z)
        {
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    const double dist2 = (x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz);
                    if (dist2 <= static_cast<double>(radius) * radius)
                    {
                        volume.at(x, y, z) = sphereHU;
                        ++plantedVoxels;
                    }
                }
            }
        }
        (void)plantedVoxels;
        return volume;
    }

    void testFindsPlantedSphere()
    {
        // Background at -1000 HU (near air), a calcification-like sphere at
        // +300 HU, radius 5 -> ~4/3*pi*5^3 ~= 524 voxels analytically.
        const int size = 40, cx = 20, cy = 20, cz = 20, radius = 5;
        const auto volume = makeVolumeWithSphere(size, cx, cy, cz, radius, -1000.f, 300.f);

        dicom::AnomalyDetector::Thresholds t;
        t.huMin = 100;
        t.huMax = 500;
        t.minVoxelCount = 5;

        const auto anomalies = dicom::AnomalyDetector::detect(volume, t);

        check(anomalies.size() == 1, "exactly one anomaly detected for one planted sphere");
        if (anomalies.size() == 1)
        {
            const auto &a = anomalies[0];
            check(std::abs(a.centroid[0] - cx) <= 1 && std::abs(a.centroid[1] - cy) <= 1 &&
                      std::abs(a.centroid[2] - cz) <= 1,
                  "detected centroid matches planted sphere center within 1 voxel");
            // Discretized sphere volume vs analytic 4/3*pi*r^3 should be close.
            const double analyticVolume = (4.0 / 3.0) * M_PI * radius * radius * radius;
            const double ratio = static_cast<double>(a.voxelCount) / analyticVolume;
            check(ratio > 0.7 && ratio < 1.3,
                  "detected voxel count within 30% of analytic sphere volume");
            check(std::abs(a.meanHU - 300.0) < 1.0, "detected mean HU matches planted sphere HU");
            check(a.stddevHU < 1.0, "detected stddev HU near zero for a uniform-density sphere");
        }
    }

    void testBackgroundAloneProducesNoAnomalies()
    {
        dicom::VoxelVolume volume;
        volume.width = volume.height = volume.depth = 20;
        volume.data.assign(20 * 20 * 20, -1000.f);

        dicom::AnomalyDetector::Thresholds t;
        t.huMin = 100;
        t.huMax = 500;

        const auto anomalies = dicom::AnomalyDetector::detect(volume, t);
        check(anomalies.empty(), "uniform background with no in-range voxels produces zero anomalies");
    }

    void testDensityThresholdingRejectsTinyComponent()
    {
        // A single-voxel "anomaly" should be rejected by density thresholding
        // (minVoxelCount), since it's more likely noise than a real finding.
        dicom::VoxelVolume volume;
        volume.width = volume.height = volume.depth = 10;
        volume.data.assign(10 * 10 * 10, -1000.f);
        volume.at(5, 5, 5) = 300.f; // single hot voxel

        dicom::AnomalyDetector::Thresholds t;
        t.huMin = 100;
        t.huMax = 500;
        t.minVoxelCount = 5; // the single voxel component has size 1 < 5

        const auto anomalies = dicom::AnomalyDetector::detect(volume, t);
        check(anomalies.empty(), "single-voxel component rejected by minVoxelCount density threshold");
    }

    void testTwoSeparateSpheresDetectedIndependently()
    {
        dicom::VoxelVolume volume;
        const int size = 40;
        volume.width = volume.height = volume.depth = size;
        volume.data.assign(static_cast<size_t>(size) * size * size, -1000.f);

        auto plantSphere = [&](int cx, int cy, int cz, int radius, float hu)
        {
            for (int z = 0; z < size; ++z)
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x)
                    {
                        const double d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz);
                        if (d2 <= static_cast<double>(radius) * radius)
                            volume.at(x, y, z) = hu;
                    }
        };
        plantSphere(10, 10, 10, 4, 300.f);
        plantSphere(30, 30, 30, 4, 300.f); // far apart -> definitely disconnected

        dicom::AnomalyDetector::Thresholds t;
        t.huMin = 100;
        t.huMax = 500;
        t.minVoxelCount = 5;

        const auto anomalies = dicom::AnomalyDetector::detect(volume, t);
        check(anomalies.size() == 2, "two well-separated spheres detected as two independent anomalies");
    }

} // namespace

int main()
{
    testFindsPlantedSphere();
    testBackgroundAloneProducesNoAnomalies();
    testDensityThresholdingRejectsTinyComponent();
    testTwoSeparateSpheresDetectedIndependently();

    std::cout << '\n'
              << (testsRun - testsFailed) << '/' << testsRun << " tests passed\n";
    return testsFailed == 0 ? 0 : 1;
}
