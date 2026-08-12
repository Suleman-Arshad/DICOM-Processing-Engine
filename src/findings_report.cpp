#include "dicom_processor/findings_report.hpp"
#include "dicom_processor/json_writer.hpp"
#include <fstream>
#include <stdexcept>

namespace dicom
{

    namespace
    {

        void writeFile(const std::string &path, const std::string &content)
        {
            std::ofstream out(path, std::ios::binary);
            if (!out)
                throw std::runtime_error("Could not open output file: " + path);
            out << content;
        }

    } // namespace

    void FindingsReport::writeJSON(const std::vector<Anomaly> &findings, const VoxelVolume &volume,
                                   const SeriesInfo &series, const std::string &outputPath)
    {
        json::Writer w;
        w.beginObject()
            .key("patientId")
            .value(series.patientId)
            .key("modality")
            .value(series.modality)
            .key("studyDate")
            .value(series.studyDate)
            .key("sliceCount")
            .value(static_cast<long long>(series.sliceCount))
            .key("volumeDimensions")
            .beginObject()
            .key("width")
            .value(static_cast<long long>(volume.width))
            .key("height")
            .value(static_cast<long long>(volume.height))
            .key("depth")
            .value(static_cast<long long>(volume.depth))
            .key("voxelSpacingMM")
            .value(volume.voxelSpacingMM)
            .endObject()
            .key("findingCount")
            .value(static_cast<long long>(findings.size()))
            .key("findings")
            .beginArray();

        for (size_t i = 0; i < findings.size(); ++i)
        {
            const auto &a = findings[i];
            w.beginObject()
                .key("id")
                .value(static_cast<long long>(i + 1))
                .key("centroid")
                .beginArray()
                .value(static_cast<long long>(a.centroid[0]))
                .value(static_cast<long long>(a.centroid[1]))
                .value(static_cast<long long>(a.centroid[2]))
                .endArray()
                .key("boundingBox")
                .beginObject()
                .key("min")
                .beginArray()
                .value(static_cast<long long>(a.bboxMin[0]))
                .value(static_cast<long long>(a.bboxMin[1]))
                .value(static_cast<long long>(a.bboxMin[2]))
                .endArray()
                .key("max")
                .beginArray()
                .value(static_cast<long long>(a.bboxMax[0]))
                .value(static_cast<long long>(a.bboxMax[1]))
                .value(static_cast<long long>(a.bboxMax[2]))
                .endArray()
                .endObject()
                .key("voxelCount")
                .value(static_cast<long long>(a.voxelCount))
                .key("meanHU")
                .value(a.meanHU)
                .key("stddevHU")
                .value(a.stddevHU)
                .endObject();
        }

        w.endArray().endObject();
        writeFile(outputPath, w.str());
    }

    void FindingsReport::writeFHIR(const std::vector<Anomaly> &findings, const VoxelVolume &volume,
                                   const SeriesInfo &series, const std::string &outputPath)
    {
        (void)volume;
        json::Writer w;
        w.beginObject()
            .key("resourceType")
            .value(std::string("Bundle"))
            .key("type")
            .value(std::string("collection"))
            .key("entry")
            .beginArray();

        // DiagnosticReport summarizing the study.
        w.beginObject().key("resource").beginObject().key("resourceType").value(std::string("DiagnosticReport")).key("status").value(std::string("preliminary")).key("code").beginObject().key("text").value(std::string("Automated density anomaly screening")).endObject().key("subject").beginObject().key("reference").value(std::string("Patient/" + series.patientId)).endObject().key("effectiveDateTime").value(series.studyDate).key("conclusion").value(std::string("Detected " + std::to_string(findings.size()) + " candidate density anomal" + (findings.size() == 1 ? "y" : "ies") + " on " + series.modality + " series.")).endObject().endObject();

        // One Observation per finding.
        for (size_t i = 0; i < findings.size(); ++i)
        {
            const auto &a = findings[i];
            w.beginObject().key("resource").beginObject().key("resourceType").value(std::string("Observation")).key("id").value(std::string("finding-" + std::to_string(i + 1))).key("status").value(std::string("preliminary")).key("code").beginObject().key("text").value(std::string("Density anomaly (region growing)")).endObject().key("subject").beginObject().key("reference").value(std::string("Patient/" + series.patientId)).endObject().key("valueQuantity").beginObject().key("value").value(a.meanHU).key("unit").value(std::string("HU")).endObject().key("component").beginArray().beginObject().key("code").beginObject().key("text").value(std::string("voxelCount")).endObject().key("valueInteger").value(static_cast<long long>(a.voxelCount)).endObject().beginObject().key("code").beginObject().key("text").value(std::string("stddevHU")).endObject().key("valueQuantity").beginObject().key("value").value(a.stddevHU).endObject().endObject().endArray().endObject().endObject();
        }

        w.endArray().endObject();
        writeFile(outputPath, w.str());
    }

} // namespace dicom
