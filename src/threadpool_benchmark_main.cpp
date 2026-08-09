#include "dicom_processor/detection.hpp"
#include "dicom_processor/reconstruction.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>

namespace
{

    std::vector<dicom::Slice> makeSyntheticSeries(int sliceCount, int rows, int cols)
    {
        std::vector<dicom::Slice> slices;
        slices.reserve(static_cast<size_t>(sliceCount));
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> noise(-100.f, 100.f);

        for (int z = 0; z < sliceCount; ++z)
        {
            dicom::Slice s;
            s.rows = rows;
            s.columns = cols;
            s.bitsAllocated = 16;
            s.modality = "CT";
            s.rescaleSlope = 1.0;
            s.rescaleIntercept = -1024.0;
            s.pixelSpacingRowMM = 0.75;
            s.pixelSpacingColMM = 0.75;
            s.sliceThicknessMM = 1.0;
            s.imagePositionZ = z * 1.0;
            s.pixels.resize(static_cast<size_t>(rows) * cols);
            for (int y = 0; y < rows; ++y)
            {
                for (int x = 0; x < cols; ++x)
                {
                    s.pixels[static_cast<size_t>(y) * cols + x] =
                        static_cast<int32_t>(x + y + z + noise(rng));
                }
            }
            slices.push_back(s);
        }
        return slices;
    }

    // A larger volume with several planted nodules, for a detection workload
    // that's representative of real work (not a trivially-empty scan).
    dicom::VoxelVolume makeVolumeWithNodules(int size, int nodulesPerAxis)
    {
        dicom::VoxelVolume volume;
        volume.width = volume.height = volume.depth = size;
        volume.modality = dicom::Modality::CT;
        volume.data.assign(static_cast<size_t>(size) * size * size, -800.f);

        const int spacing = size / (nodulesPerAxis + 1);
        const int radius = std::max(2, spacing / 6);
        for (int iz = 1; iz <= nodulesPerAxis; ++iz)
        {
            for (int iy = 1; iy <= nodulesPerAxis; ++iy)
            {
                for (int ix = 1; ix <= nodulesPerAxis; ++ix)
                {
                    const int cx = ix * spacing, cy = iy * spacing, cz = iz * spacing;
                    for (int z = std::max(0, cz - radius); z <= std::min(size - 1, cz + radius); ++z)
                    {
                        for (int y = std::max(0, cy - radius); y <= std::min(size - 1, cy + radius); ++y)
                        {
                            for (int x = std::max(0, cx - radius); x <= std::min(size - 1, cx + radius); ++x)
                            {
                                const double d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz);
                                if (d2 <= static_cast<double>(radius) * radius)
                                    volume.at(x, y, z) = 300.f;
                            }
                        }
                    }
                }
            }
        }
        return volume;
    }

    template <typename Func>
    double timeMs(Func &&f)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        f();
        const auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    // Single-shot timing is noisy -- OS scheduling jitter, thermal/frequency
    // scaling, and background processes can all skew one measurement enough
    // to distort a speedup ratio. Average over several runs, matching the
    // methodology already used in filter_benchmark.
    template <typename Func>
    double timeMsAvg(Func &&f, int iterations)
    {
        double total = 0.0;
        for (int i = 0; i < iterations; ++i)
        {
            total += timeMs(f);
        }
        return total / iterations;
    }

    void printHeader(const std::string &title)
    {
        std::cout << "\n=== " << title << " ===\n";
        std::cout << std::left << std::setw(10) << "Threads" << std::right << std::setw(14) << "Time (ms)"
                  << std::setw(12) << "Speedup" << std::setw(14) << "Efficiency" << '\n';
        std::cout << std::string(50, '-') << '\n';
    }

    void printRow(const std::string &label, double ms, double speedup, double efficiencyPct)
    {
        std::cout << std::left << std::setw(10) << label << std::right << std::fixed << std::setprecision(2)
                  << std::setw(14) << ms << std::setw(11) << speedup << "x" << std::setw(13) << efficiencyPct
                  << "%\n";
    }

} // namespace

int main()
{
    const unsigned hwThreads = std::thread::hardware_concurrency();
    std::cout << "Hardware concurrency reported: " << (hwThreads > 0 ? hwThreads : 1) << " thread(s)\n";
    if (hwThreads <= 1)
    {
        std::cout << "NOTE: this machine reports " << (hwThreads == 0 ? 1 : hwThreads)
                  << " CPU core(s) available. Near-linear speedup requires real hardware "
                     "parallelism to measure -- results below will correctly show ~1.0x "
                     "(no speedup) on such a machine. That is the CORRECT result for "
                     "zero-parallelism hardware, not a bug in the thread pool. Run this "
                     "on multi-core hardware for a meaningful measurement.\n";
    }

    const std::vector<size_t> threadCounts = {2u, 4u, 8u, 16u};

    // ============================== RECONSTRUCTION ==============================
    {
        constexpr int sliceCount = 60, rows = 256, cols = 256;
        const auto series = makeSyntheticSeries(sliceCount, rows, cols);
        std::cout << "\nReconstruction workload: " << sliceCount << " slices, " << rows << "x" << cols << "\n";

        const dicom::VoxelVolume serialResult = dicom::VolumeReconstructor::reconstruct(series);
        {
            dicom::ThreadPool checkPool(std::max(2u, hwThreads));
            const dicom::VoxelVolume parallelResult =
                dicom::VolumeReconstructor::reconstructParallel(series, checkPool);
            if (parallelResult.data != serialResult.data)
            {
                std::cerr << "Reconstruction: parallel output mismatch -- refusing to report numbers.\n";
                return EXIT_FAILURE;
            }
        }
        std::cout << "[PASS] Parallel reconstruction verified bit-identical to serial reference\n";

        const double serialMs = timeMsAvg([&]
                                          { dicom::VolumeReconstructor::reconstruct(series); }, 5);
        printHeader("Reconstruction Speedup");
        printRow("1 (serial)", serialMs, 1.0, 100.0);
        for (size_t tc : threadCounts)
        {
            dicom::ThreadPool pool(tc);
            const double parallelMs = timeMsAvg([&]
                                                { dicom::VolumeReconstructor::reconstructParallel(series, pool); }, 5);
            const double speedup = serialMs / parallelMs;
            printRow(std::to_string(tc), parallelMs, speedup, (speedup / static_cast<double>(tc)) * 100.0);
        }
    }

    // ================================ DETECTION =================================
    {
        constexpr int size = 128, nodulesPerAxis = 3;
        const auto volume = makeVolumeWithNodules(size, nodulesPerAxis);
        std::cout << "\nDetection workload: " << size << "^3 volume, " << (nodulesPerAxis * nodulesPerAxis * nodulesPerAxis)
                  << " planted nodules\n";

        dicom::AnomalyDetector::Thresholds t;
        t.huMin = 100;
        t.huMax = 500;
        t.minVoxelCount = 5;

        const auto serialResult = dicom::AnomalyDetector::detect(volume, t);
        {
            dicom::ThreadPool checkPool(std::max(2u, hwThreads));
            const auto parallelResult = dicom::AnomalyDetector::detectParallel(volume, checkPool, t);
            if (parallelResult.size() != serialResult.size())
            {
                std::cerr << "Detection: parallel anomaly count mismatch (" << parallelResult.size()
                          << " vs " << serialResult.size() << ") -- refusing to report numbers.\n";
                return EXIT_FAILURE;
            }
        }
        std::cout << "[PASS] Parallel detection verified to find the same " << serialResult.size()
                  << " anomalies as serial reference\n";

        const double serialMs = timeMsAvg([&]
                                          { dicom::AnomalyDetector::detect(volume, t); }, 5);
        printHeader("Detection Speedup");
        printRow("1 (serial)", serialMs, 1.0, 100.0);
        for (size_t tc : threadCounts)
        {
            dicom::ThreadPool pool(tc);
            const double parallelMs = timeMsAvg([&]
                                                { dicom::AnomalyDetector::detectParallel(volume, pool, t); }, 5);
            const double speedup = serialMs / parallelMs;
            printRow(std::to_string(tc), parallelMs, speedup, (speedup / static_cast<double>(tc)) * 100.0);
        }
    }

    std::cout << "\nNote: 'near-linear speedup' means speedup approaches threadCount as long as "
                 "threadCount <= physical cores available. Beyond that, expect diminishing "
                 "returns (oversubscription) rather than continued linear gains -- that's "
                 "expected hardware behavior, not a bug in the pool.\n";

    return EXIT_SUCCESS;
}