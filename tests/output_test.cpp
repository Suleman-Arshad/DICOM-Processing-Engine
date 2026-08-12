#include "dicom_processor/dicom_writer.hpp"
#include "dicom_processor/findings_report.hpp"
#include "dicom_processor/png_writer.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace
{

    int testsRun = 0, testsFailed = 0;

    void check(bool cond, const std::string &what)
    {
        ++testsRun;
        if (!cond)
        {
            ++testsFailed;
            std::cout << "[FAIL] " << what << "\n";
        }
        else
            std::cout << "[PASS] " << what << "\n";
    }

    std::string readFile(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }

    // Not a real parser -- just enough to catch obviously broken output
    // (unbalanced braces/brackets, unterminated strings).
    bool bracesBalanced(const std::string &s)
    {
        int depth = 0;
        bool inString = false;
        for (size_t i = 0; i < s.size(); ++i)
        {
            char c = s[i];
            if (c == '"' && (i == 0 || s[i - 1] != '\\'))
                inString = !inString;
            if (inString)
                continue;
            if (c == '{' || c == '[')
                ++depth;
            if (c == '}' || c == ']')
                --depth;
            if (depth < 0)
                return false;
        }
        return depth == 0 && !inString;
    }

    dicom::VoxelVolume makeTestVolume()
    {
        dicom::VoxelVolume v;
        v.width = v.height = v.depth = 10;
        v.modality = dicom::Modality::CT;
        v.data.assign(1000, -800.f);
        for (int z = 4; z <= 6; ++z)
            for (int y = 4; y <= 6; ++y)
                for (int x = 4; x <= 6; ++x)
                    v.at(x, y, z) = 300.f;
        return v;
    }

    dicom::Anomaly makeTestAnomaly()
    {
        dicom::Anomaly a;
        a.centroid = {5, 5, 5};
        a.bboxMin = {4, 4, 4};
        a.bboxMax = {6, 6, 6};
        a.voxelCount = 27;
        a.meanHU = 300.0;
        a.stddevHU = 0.0;
        return a;
    }

} // namespace

int main()
{
    const auto volume = makeTestVolume();
    const auto anomaly = makeTestAnomaly();
    dicom::SeriesInfo series{"TEST001", "CT", "20260101", 10};

    dicom::FindingsReport::writeJSON({anomaly}, volume, series, "/tmp/out_test_findings.json");
    const auto jsonText = readFile("/tmp/out_test_findings.json");
    check(!jsonText.empty(), "JSON findings report is non-empty");
    check(bracesBalanced(jsonText), "JSON findings report has balanced braces/brackets");
    check(jsonText.find("\"voxelCount\":27") != std::string::npos, "JSON contains expected finding data");

    dicom::FindingsReport::writeFHIR({anomaly}, volume, series, "/tmp/out_test_fhir.json");
    const auto fhirText = readFile("/tmp/out_test_fhir.json");
    check(bracesBalanced(fhirText), "FHIR bundle has balanced braces/brackets");
    check(fhirText.find("\"resourceType\":\"Bundle\"") != std::string::npos, "FHIR bundle has correct resourceType");

    dicom::PngWriter::writeSlice(volume, 5, "/tmp/out_test_plain.png");
    const auto pngBytes = readFile("/tmp/out_test_plain.png");
    const unsigned char pngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    check(pngBytes.size() >= 8 && std::memcmp(pngBytes.data(), pngSig, 8) == 0, "PNG file has correct signature");

    dicom::PngWriter::writeSliceAnnotated(volume, 5, {anomaly}, "/tmp/out_test_annotated.png");
    const auto annotatedBytes = readFile("/tmp/out_test_annotated.png");
    check(annotatedBytes.size() >= 8 && std::memcmp(annotatedBytes.data(), pngSig, 8) == 0,
          "Annotated PNG file has correct signature");

    dicom::Slice slice;
    slice.rows = slice.columns = 10;
    slice.bitsAllocated = 16;
    slice.modality = "CT";
    slice.rescaleSlope = 1.0;
    slice.rescaleIntercept = -1024.0;
    slice.pixels.assign(100, 0);
    dicom::DicomWriter::writeAnnotated(slice, {anomaly}, "/tmp/out_test.dcm");
    const auto dcmBytes = readFile("/tmp/out_test.dcm");
    check(dcmBytes.size() > 132 && dcmBytes.substr(128, 4) == "DICM", "Annotated DICOM has valid Part 10 magic");

    std::cout << "\n"
              << (testsRun - testsFailed) << "/" << testsRun << " tests passed\n";
    return testsFailed == 0 ? 0 : 1;
}
