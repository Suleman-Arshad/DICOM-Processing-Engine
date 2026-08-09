#include "dicom_processor/filters.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <utility>

namespace
{

    dicom::VoxelVolume makeSyntheticVolume(int width, int height, int depth)
    {
        dicom::VoxelVolume volume;
        volume.width = width;
        volume.height = height;
        volume.depth = depth;
        volume.voxelSpacingMM = 1.0;
        volume.modality = dicom::Modality::CT;
        volume.data.resize(static_cast<size_t>(width) * height * depth);

        std::mt19937 rng(42);
        std::uniform_real_distribution<float> noise(-50.0f, 50.0f);

        for (int z = 0; z < depth; ++z)
        {
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const float gradient = -1000.0f + 2000.0f * (static_cast<float>(x) / width);
                    volume.at(x, y, z) = gradient + noise(rng);
                }
            }
        }
        return volume;
    }

    double maxAbsDiff(const dicom::VoxelVolume &a, const dicom::VoxelVolume &b)
    {
        double maxDiff = 0.0;
        for (size_t i = 0; i < a.data.size(); ++i)
        {
            maxDiff = std::max(maxDiff, static_cast<double>(std::abs(a.data[i] - b.data[i])));
        }
        return maxDiff;
    }

    template <typename Func>
    double timeMsAvg(Func &&f, int iterations)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i)
            f();
        const auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count() / iterations;
    }

    void printRow(const std::string &name, double scalarMs, double avx2Ms)
    {
        std::cout << std::left << std::setw(22) << name << std::right << std::fixed << std::setprecision(3)
                  << std::setw(12) << scalarMs << " ms" << std::setw(12) << avx2Ms << " ms"
                  << std::setprecision(2) << std::setw(10) << (scalarMs / avx2Ms) << "x\n";
    }

    void printScalarRow(const std::string &name, double scalarMs)
    {
        std::cout << std::left << std::setw(22) << name << std::right << std::fixed << std::setprecision(3)
                  << std::setw(12) << scalarMs << " ms\n";
    }

} // namespace

int main(int argc, char *argv[])
{
    const bool forceScalarOnly = (argc > 1 && std::string(argv[1]) == "--scalar-only");
    const bool hasAVX2 = dicom::filters::cpuSupportsAVX2();

    constexpr int width = 256, height = 256, depth = 32;
    constexpr int iterations = 5;
    std::cout << "Synthetic volume: " << width << "x" << height << "x" << depth << " ("
              << (static_cast<size_t>(width) * height * depth) << " voxels)\n\n";

    const dicom::VoxelVolume volume = makeSyntheticVolume(width, height, depth);

    if (!hasAVX2 || forceScalarOnly)
    {
        if (!hasAVX2)
        {
            std::cout << "This CPU does not support AVX2 -- there is no SIMD path to compare "
                         "against, so this reports scalar throughput only (still real,\n"
                         "useful single-threaded performance data -- just not a speedup ratio).\n\n";
        }
        else
        {
            std::cout << "--scalar-only requested -- reporting scalar throughput only.\n\n";
        }
        std::cout << std::left << std::setw(22) << "Filter" << std::right << std::setw(15) << "Scalar" << '\n';
        std::cout << std::string(37, '-') << '\n';
        printScalarRow("Gaussian Blur", timeMsAvg([&]
                                                  { dicom::filters::gaussianBlurScalar(volume, 2.0f); }, iterations));
        printScalarRow("Sobel Edge", timeMsAvg([&]
                                               { dicom::filters::sobelEdgeScalar(volume); }, iterations));
        printScalarRow("Histogram Equalize", timeMsAvg([&]
                                                       { dicom::filters::histogramEqualizeScalar(volume); }, iterations));
        return EXIT_SUCCESS;
    }

    const auto blurScalar = dicom::filters::gaussianBlurScalar(volume, 2.0f);
    const auto blurAVX2 = dicom::filters::gaussianBlurAVX2(volume, 2.0f);
    const auto edgeScalar = dicom::filters::sobelEdgeScalar(volume);
    const auto edgeAVX2 = dicom::filters::sobelEdgeAVX2(volume);
    const auto histScalar = dicom::filters::histogramEqualizeScalar(volume);
    const auto histAVX2 = dicom::filters::histogramEqualizeAVX2(volume);

    constexpr double epsilon = 1e-1;
    bool allCorrect = true;
    const std::pair<const char *, double> diffs[] = {
        {"Gaussian Blur", maxAbsDiff(blurScalar, blurAVX2)},
        {"Sobel Edge", maxAbsDiff(edgeScalar, edgeAVX2)},
        {"Histogram Equalize", maxAbsDiff(histScalar, histAVX2)},
    };
    for (const auto &[name, diff] : diffs)
    {
        const bool ok = diff < epsilon;
        allCorrect = allCorrect && ok;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << " max abs diff: " << diff << '\n';
    }
    if (!allCorrect)
    {
        std::cerr << "\nAVX2 output diverges from scalar reference -- refusing to report benchmark numbers.\n";
        return EXIT_FAILURE;
    }
    std::cout << "\nAll AVX2 implementations match scalar reference. Benchmarking...\n\n";

    std::cout << std::left << std::setw(22) << "Filter" << std::right << std::setw(15) << "Scalar"
              << std::setw(15) << "AVX2" << std::setw(10) << "Speedup" << '\n';
    std::cout << std::string(59, '-') << '\n';

    printRow("Gaussian Blur", timeMsAvg([&]
                                        { dicom::filters::gaussianBlurScalar(volume, 2.0f); }, iterations),
             timeMsAvg([&]
                       { dicom::filters::gaussianBlurAVX2(volume, 2.0f); }, iterations));
    printRow("Sobel Edge", timeMsAvg([&]
                                     { dicom::filters::sobelEdgeScalar(volume); }, iterations),
             timeMsAvg([&]
                       { dicom::filters::sobelEdgeAVX2(volume); }, iterations));
    printRow("Histogram Equalize", timeMsAvg([&]
                                             { dicom::filters::histogramEqualizeScalar(volume); }, iterations),
             timeMsAvg([&]
                       { dicom::filters::histogramEqualizeAVX2(volume); }, iterations));

    return EXIT_SUCCESS;
}
