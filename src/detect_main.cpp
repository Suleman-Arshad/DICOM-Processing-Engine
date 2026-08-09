#include "dicom_processor/detection.hpp"
#include "dicom_processor/parser.hpp"
#include "dicom_processor/reconstruction.hpp"

#include <iostream>
#include <vector>

namespace
{

    void printUsage(const char *argv0)
    {
        std::cerr << "Usage: " << argv0 << " [--hu-min N] [--hu-max N] [--min-voxels N] <slice1.dcm> [slice2.dcm ...]\n";
        std::cerr << "  Defaults: --hu-min 100 --hu-max 1500 --min-voxels 10\n";
        std::cerr << "  (100-1500 HU is a broad calcification/dense-nodule range; narrow it for your data.)\n";
    }

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    dicom::AnomalyDetector::Thresholds thresholds;
    thresholds.huMin = 100.0;
    thresholds.huMax = 1500.0;
    thresholds.minVoxelCount = 10;

    std::vector<std::string> filePaths;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--hu-min" && i + 1 < argc)
        {
            thresholds.huMin = std::stod(argv[++i]);
        }
        else if (arg == "--hu-max" && i + 1 < argc)
        {
            thresholds.huMax = std::stod(argv[++i]);
        }
        else if (arg == "--min-voxels" && i + 1 < argc)
        {
            thresholds.minVoxelCount = static_cast<size_t>(std::stoul(argv[++i]));
        }
        else
        {
            filePaths.push_back(arg);
        }
    }

    if (filePaths.empty())
    {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    std::vector<dicom::Slice> slices;
    slices.reserve(filePaths.size());
    for (const auto &path : filePaths)
    {
        try
        {
            slices.push_back(dicom::Parser::parseFile(path));
        }
        catch (const dicom::ParseError &e)
        {
            std::cerr << "Parse error in '" << path << "': " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    }

    try
    {
        const int sliceCount = static_cast<int>(slices.size());
        const dicom::VoxelVolume volume = dicom::VolumeReconstructor::reconstruct(std::move(slices));

        std::cout << "=== Volume ===\n";
        std::cout << "Input slices:       " << sliceCount << '\n';
        std::cout << "Dimensions (WxHxD): " << volume.width << " x " << volume.height << " x "
                  << volume.depth << '\n';
        std::cout << "Voxel spacing:      " << volume.voxelSpacingMM << " mm\n\n";

        std::cout << "=== Detection ===\n";
        std::cout << "HU threshold:       [" << thresholds.huMin << ", " << thresholds.huMax << "]\n";
        std::cout << "Min voxel count:     " << thresholds.minVoxelCount << "\n\n";

        const auto anomalies = dicom::AnomalyDetector::detect(volume, thresholds);

        std::cout << "Findings: " << anomalies.size() << " anomal" << (anomalies.size() == 1 ? "y" : "ies")
                  << '\n';
        for (size_t i = 0; i < anomalies.size(); ++i)
        {
            const auto &a = anomalies[i];
            std::cout << "\n  [" << (i + 1) << "] Centroid: (" << a.centroid[0] << ", " << a.centroid[1]
                      << ", " << a.centroid[2] << ")\n";
            std::cout << "      BoundingBox: (" << a.bboxMin[0] << "," << a.bboxMin[1] << "," << a.bboxMin[2]
                      << ") - (" << a.bboxMax[0] << "," << a.bboxMax[1] << "," << a.bboxMax[2] << ")\n";
            std::cout << "      Voxel count: " << a.voxelCount << '\n';
            std::cout << "      Mean HU:     " << a.meanHU << '\n';
            std::cout << "      Stddev HU:   " << a.stddevHU << '\n';
        }
    }
    catch (const dicom::ReconstructionError &e)
    {
        std::cerr << "Reconstruction error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
