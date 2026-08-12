#include "dicom_processor/detection.hpp"
#include "dicom_processor/dicom_writer.hpp"
#include "dicom_processor/findings_report.hpp"
#include "dicom_processor/parser.hpp"
#include "dicom_processor/png_writer.hpp"
#include "dicom_processor/reconstruction.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace
{

    void printUsage(const char *argv0)
    {
        std::cerr << "Usage: " << argv0
                  << " [--hu-min N] [--hu-max N] [--min-voxels N] [--fhir]"
                     " --out <dir> <slice1.dcm> [slice2.dcm ...]\n";
    }

} // namespace

int main(int argc, char *argv[])
{
    dicom::AnomalyDetector::Thresholds thresholds;
    thresholds.huMin = 100.0;
    thresholds.huMax = 1500.0;
    thresholds.minVoxelCount = 10;
    bool writeFHIR = false;
    std::string outDir;
    std::vector<std::string> filePaths;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--hu-min" && i + 1 < argc)
            thresholds.huMin = std::stod(argv[++i]);
        else if (arg == "--hu-max" && i + 1 < argc)
            thresholds.huMax = std::stod(argv[++i]);
        else if (arg == "--min-voxels" && i + 1 < argc)
            thresholds.minVoxelCount = static_cast<size_t>(std::stoul(argv[++i]));
        else if (arg == "--fhir")
            writeFHIR = true;
        else if (arg == "--out" && i + 1 < argc)
            outDir = argv[++i];
        else
            filePaths.push_back(arg);
    }

    if (outDir.empty() || filePaths.empty())
    {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    fs::create_directories(outDir);
    fs::create_directories(outDir + "/slices");

    std::vector<dicom::Slice> slices;
    std::string firstStudyDate, firstPatientId;
    for (const auto &path : filePaths)
    {
        try
        {
            auto s = dicom::Parser::parseFile(path);
            if (firstPatientId.empty())
            {
                firstPatientId = s.metadata.getString(dicom::tags::PatientID).value_or("ANONYMOUS");
                firstStudyDate = s.metadata.getString(dicom::tags::StudyDate).value_or("");
            }
            slices.push_back(std::move(s));
        }
        catch (const dicom::ParseError &e)
        {
            std::cerr << "Parse error in '" << path << "': " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    }
    const int sliceCount = static_cast<int>(slices.size());
    const dicom::Slice firstSliceCopy = slices.front(); // kept for DICOM export before slices is moved

    dicom::VoxelVolume volume;
    try
    {
        volume = dicom::VolumeReconstructor::reconstruct(std::move(slices));
    }
    catch (const dicom::ReconstructionError &e)
    {
        std::cerr << "Reconstruction error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Reconstructed volume: " << volume.width << "x" << volume.height << "x" << volume.depth << '\n';

    const auto findings = dicom::AnomalyDetector::detect(volume, thresholds);
    std::cout << "Detected " << findings.size() << " finding(s)\n";

    for (int z = 0; z < volume.depth; ++z)
    {
        char name[64];
        std::snprintf(name, sizeof(name), "/slice_%03d.png", z);
        dicom::PngWriter::writeSliceAnnotated(volume, z, findings, outDir + "/slices" + name);
    }
    std::cout << "Wrote " << volume.depth << " annotated PNG slice(s) to " << outDir << "/slices\n";

    dicom::SeriesInfo series;
    series.patientId = firstPatientId;
    series.modality = dicom::modalityToString(volume.modality);
    series.studyDate = firstStudyDate;
    series.sliceCount = sliceCount;

    dicom::FindingsReport::writeJSON(findings, volume, series, outDir + "/findings.json");
    std::cout << "Wrote " << outDir << "/findings.json\n";

    if (writeFHIR)
    {
        dicom::FindingsReport::writeFHIR(findings, volume, series, outDir + "/findings_fhir.json");
        std::cout << "Wrote " << outDir << "/findings_fhir.json\n";
    }

    dicom::DicomWriter::writeAnnotated(firstSliceCopy, findings, outDir + "/annotated.dcm");
    std::cout << "Wrote " << outDir << "/annotated.dcm\n";

    return EXIT_SUCCESS;
}
