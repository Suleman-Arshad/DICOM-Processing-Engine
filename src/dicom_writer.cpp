#include "dicom_processor/dicom_writer.hpp"
#include "dicom_processor/json_writer.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace dicom
{

    namespace
    {

        void putU16(std::vector<uint8_t> &b, uint16_t v)
        {
            b.push_back(v & 0xFF);
            b.push_back((v >> 8) & 0xFF);
        }
        void putU32(std::vector<uint8_t> &b, uint32_t v)
        {
            b.push_back(v & 0xFF);
            b.push_back((v >> 8) & 0xFF);
            b.push_back((v >> 16) & 0xFF);
            b.push_back((v >> 24) & 0xFF);
        }
        void putBytes(std::vector<uint8_t> &b, const void *data, size_t n)
        {
            const auto *p = static_cast<const uint8_t *>(data);
            b.insert(b.end(), p, p + n);
        }
        void putTag(std::vector<uint8_t> &b, uint16_t group, uint16_t element)
        {
            putU16(b, group);
            putU16(b, element);
        }

        std::string padEven(std::string s, char padChar = ' ')
        {
            if (s.size() % 2 == 1)
                s += padChar;
            return s;
        }

        // Short-form Explicit VR: tag + 2-char VR + 2-byte length + value.
        void writeShort(std::vector<uint8_t> &b, uint16_t g, uint16_t e, const char *vr, const std::string &value)
        {
            putTag(b, g, e);
            putBytes(b, vr, 2);
            putU16(b, static_cast<uint16_t>(value.size()));
            putBytes(b, value.data(), value.size());
        }

        // Long-form Explicit VR: tag + 2-char VR + 2 reserved + 4-byte length + value.
        void writeLong(std::vector<uint8_t> &b, uint16_t g, uint16_t e, const char *vr, const std::vector<uint8_t> &value)
        {
            putTag(b, g, e);
            putBytes(b, vr, 2);
            putU16(b, 0);
            putU32(b, static_cast<uint32_t>(value.size()));
            putBytes(b, value.data(), value.size());
        }

        void writeLongStr(std::vector<uint8_t> &b, uint16_t g, uint16_t e, const char *vr, const std::string &value)
        {
            std::vector<uint8_t> v(value.begin(), value.end());
            writeLong(b, g, e, vr, v);
        }

        std::string ds(double v)
        {
            std::ostringstream oss;
            oss << v;
            return oss.str();
        }

    } // namespace

    void DicomWriter::writeAnnotated(const Slice &slice, const std::vector<Anomaly> &findings,
                                     const std::string &outputPath)
    {
        // --- Findings JSON for the private tag block ---
        json::Writer jw;
        jw.beginArray();
        for (const auto &a : findings)
        {
            jw.beginObject()
                .key("centroid")
                .beginArray()
                .value(static_cast<long long>(a.centroid[0]))
                .value(static_cast<long long>(a.centroid[1]))
                .value(static_cast<long long>(a.centroid[2]))
                .endArray()
                .key("voxelCount")
                .value(static_cast<long long>(a.voxelCount))
                .key("meanHU")
                .value(a.meanHU)
                .endObject();
        }
        jw.endArray();
        const std::string findingsJson = jw.str();

        // --- Main dataset, tags in strict ascending order ---
        std::vector<uint8_t> ds_;
        writeShort(ds_, 0x0008, 0x0020, "DA", padEven(slice.metadata.getString(tags::StudyDate).value_or("")));
        writeShort(ds_, 0x0008, 0x0060, "CS", padEven(slice.modality));

        writeShort(ds_, 0x0009, 0x0010, "LO", padEven("PULSE_IMAGING_ENGINE"));
        writeLongStr(ds_, 0x0009, 0x1001, "UT", findingsJson);

        writeShort(ds_, 0x0010, 0x0020, "LO", padEven(slice.metadata.getString(tags::PatientID).value_or("")));

        writeShort(ds_, 0x0018, 0x0050, "DS", padEven(ds(slice.sliceThicknessMM)));

        std::ostringstream posStream;
        posStream << "0\\0\\" << slice.imagePositionZ;
        writeShort(ds_, 0x0020, 0x0032, "DS", padEven(posStream.str()));

        auto writeUS = [&](uint16_t g, uint16_t e, uint16_t val)
        {
            putTag(ds_, g, e);
            putBytes(ds_, "US", 2);
            putU16(ds_, 2);
            putU16(ds_, val);
        };

        writeUS(0x0028, 0x0002, 1); // SamplesPerPixel
        writeShort(ds_, 0x0028, 0x0004, "CS", padEven(std::string("MONOCHROME2")));
        writeUS(0x0028, 0x0010, static_cast<uint16_t>(slice.rows));
        writeUS(0x0028, 0x0011, static_cast<uint16_t>(slice.columns));
        writeShort(ds_, 0x0028, 0x0030, "DS",
                   padEven(ds(slice.pixelSpacingRowMM) + "\\" + ds(slice.pixelSpacingColMM)));
        writeUS(0x0028, 0x0100, static_cast<uint16_t>(slice.bitsAllocated));
        writeUS(0x0028, 0x0101, static_cast<uint16_t>(slice.bitsAllocated));
        writeUS(0x0028, 0x0103, slice.pixelRepresentationSigned ? 1 : 0);
        writeShort(ds_, 0x0028, 0x1052, "DS", padEven(ds(slice.rescaleIntercept)));
        writeShort(ds_, 0x0028, 0x1053, "DS", padEven(ds(slice.rescaleSlope)));

        // PixelData, re-encoded fresh from already-parsed (endian/sign-correct) values.
        std::vector<uint8_t> pixelBytes;
        pixelBytes.reserve(slice.pixels.size() * 2);
        for (int32_t p : slice.pixels)
        {
            const uint16_t raw = static_cast<uint16_t>(static_cast<int16_t>(p));
            pixelBytes.push_back(raw & 0xFF);
            pixelBytes.push_back((raw >> 8) & 0xFF);
        }
        writeLong(ds_, 0x7FE0, 0x0010, "OW", pixelBytes);

        // --- File Meta Info group ---
        std::vector<uint8_t> meta;
        writeShort(meta, 0x0002, 0x0010, "UI", padEven(std::string("1.2.840.10008.1.2.1"), '\0'));

        std::vector<uint8_t> metaWithLength;
        {
            std::vector<uint8_t> lenVal(4);
            const uint32_t metaLen = static_cast<uint32_t>(meta.size());
            lenVal[0] = metaLen & 0xFF;
            lenVal[1] = (metaLen >> 8) & 0xFF;
            lenVal[2] = (metaLen >> 16) & 0xFF;
            lenVal[3] = (metaLen >> 24) & 0xFF;
            putTag(metaWithLength, 0x0002, 0x0000);
            putBytes(metaWithLength, "UL", 2);
            putU16(metaWithLength, 4);
            putBytes(metaWithLength, lenVal.data(), 4);
        }

        // --- Assemble full file ---
        std::ofstream out(outputPath, std::ios::binary);
        if (!out)
            throw std::runtime_error("Could not open DICOM output file: " + outputPath);

        std::vector<uint8_t> preamble(128, 0);
        out.write(reinterpret_cast<const char *>(preamble.data()), static_cast<std::streamsize>(preamble.size()));
        out.write("DICM", 4);
        out.write(reinterpret_cast<const char *>(metaWithLength.data()), static_cast<std::streamsize>(metaWithLength.size()));
        out.write(reinterpret_cast<const char *>(meta.data()), static_cast<std::streamsize>(meta.size()));
        out.write(reinterpret_cast<const char *>(ds_.data()), static_cast<std::streamsize>(ds_.size()));
    }

} // namespace dicom
