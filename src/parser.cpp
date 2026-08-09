#include "dicom_processor/parser.hpp"
#include "dicom_processor/dictionary.hpp"

#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace dicom
{

    namespace
    {

        class ByteCursor
        {
        public:
            explicit ByteCursor(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

            size_t position() const { return pos_; }
            size_t remaining() const { return bytes_.size() - pos_; }
            bool atEnd() const { return pos_ >= bytes_.size(); }

            void require(size_t n, const char *what) const
            {
                if (remaining() < n)
                {
                    std::ostringstream oss;
                    oss << "Truncated file while reading " << what << " at offset " << pos_
                        << " (need " << n << " bytes, have " << remaining() << ")";
                    throw ParseError(oss.str());
                }
            }

            void skip(size_t n)
            {
                require(n, "skip");
                pos_ += n;
            }

            std::vector<std::byte> readBytes(size_t n)
            {
                require(n, "value bytes");
                std::vector<std::byte> out(n);
                std::memcpy(out.data(), bytes_.data() + pos_, n);
                pos_ += n;
                return out;
            }

            uint16_t readUInt16(bool bigEndian)
            {
                require(2, "uint16");
                const uint8_t b0 = bytes_[pos_];
                const uint8_t b1 = bytes_[pos_ + 1];
                pos_ += 2;
                return bigEndian ? static_cast<uint16_t>((b0 << 8) | b1) : static_cast<uint16_t>((b1 << 8) | b0);
            }

            uint32_t readUInt32(bool bigEndian)
            {
                require(4, "uint32");
                const uint8_t b0 = bytes_[pos_], b1 = bytes_[pos_ + 1];
                const uint8_t b2 = bytes_[pos_ + 2], b3 = bytes_[pos_ + 3];
                pos_ += 4;
                if (bigEndian)
                {
                    return (static_cast<uint32_t>(b0) << 24) | (static_cast<uint32_t>(b1) << 16) |
                           (static_cast<uint32_t>(b2) << 8) | static_cast<uint32_t>(b3);
                }
                return (static_cast<uint32_t>(b3) << 24) | (static_cast<uint32_t>(b2) << 16) |
                       (static_cast<uint32_t>(b1) << 8) | static_cast<uint32_t>(b0);
            }

            Tag readTag(bool bigEndian)
            {
                const uint16_t group = readUInt16(bigEndian);
                const uint16_t element = readUInt16(bigEndian);
                return Tag{group, element};
            }

            std::string readAscii(size_t n)
            {
                require(n, "ascii code");
                std::string s(reinterpret_cast<const char *>(bytes_.data() + pos_), n);
                pos_ += n;
                return s;
            }

        private:
            std::vector<uint8_t> bytes_;
            size_t pos_{0};
        };

        constexpr uint32_t kUndefinedLength = 0xFFFFFFFFu;

        struct ElementHeader
        {
            Tag tag;
            VR vr;
            uint32_t length;
        };

        ElementHeader readElementHeader(ByteCursor &cursor, bool explicitVR, bool bigEndian)
        {
            const Tag tag = cursor.readTag(bigEndian);
            if (!explicitVR)
            {
                const uint32_t length = cursor.readUInt32(bigEndian);
                VR vr = lookupImplicitVR(tag);
                return ElementHeader{tag, vr, length};
            }
            const std::string vrCode = cursor.readAscii(2);
            const VR vr = vrFromString(vrCode);
            if (usesLongLengthForm(vr))
            {
                cursor.skip(2);
                const uint32_t length = cursor.readUInt32(bigEndian);
                return ElementHeader{tag, vr, length};
            }
            const uint16_t length = cursor.readUInt16(bigEndian);
            return ElementHeader{tag, vr, length};
        }

        void skipSequence(ByteCursor &cursor, uint32_t declaredLength, bool /*explicitVR*/, bool bigEndian)
        {
            if (declaredLength != kUndefinedLength)
            {
                cursor.skip(declaredLength);
                return;
            }
            while (true)
            {
                const Tag itemTag = cursor.readTag(bigEndian);
                const uint32_t itemLength = cursor.readUInt32(bigEndian);

                if (itemTag == structural_tags::SequenceDelimitation)
                {
                    return;
                }
                if (itemTag != structural_tags::Item)
                {
                    throw ParseError("Malformed sequence: expected Item or SequenceDelimitation tag");
                }
                if (itemLength != kUndefinedLength)
                {
                    cursor.skip(itemLength);
                    continue;
                }
                throw ParseError(
                    "Undefined-length sequence items are not yet supported by this "
                    "parser. None of the CT/MRI/X-Ray sample files used so far hit "
                    "this path; extend skipSequence() if a future sample does.");
            }
        }

        std::vector<uint8_t> readWholeFile(const std::string &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                throw ParseError("Could not open file: " + path);
            }
            file.seekg(0, std::ios::end);
            const std::streamoff size = file.tellg();
            if (size < 0)
                throw ParseError("Could not determine file size: " + path);
            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            if (!bytes.empty() && !file.read(reinterpret_cast<char *>(bytes.data()), size))
            {
                throw ParseError("Failed reading file contents: " + path);
            }
            return bytes;
        }

        TransferSyntax resolveTransferSyntax(const std::string &uid)
        {
            if (uid == "1.2.840.10008.1.2")
                return TransferSyntax::ImplicitVRLittleEndian;
            if (uid == "1.2.840.10008.1.2.1")
                return TransferSyntax::ExplicitVRLittleEndian;
            if (uid == "1.2.840.10008.1.2.2")
                return TransferSyntax::ExplicitVRBigEndian;
            throw ParseError(
                "Unsupported transfer syntax '" + uid +
                "' -- compressed pixel data (JPEG/JPEG2000/RLE) is out of scope for "
                "this custom parser (see ARCHITECTURE.md section 11).");
        }

    } // namespace

    Slice Parser::parseFile(const std::string &path)
    {
        ByteCursor cursor(readWholeFile(path));

        cursor.require(132, "preamble + DICM magic");
        cursor.skip(128);
        const std::string magic = cursor.readAscii(4);
        if (magic != "DICM")
        {
            throw ParseError("Not a Part 10 DICOM file (missing 'DICM' magic) in: " + path);
        }

        Dataset metaInfo;
        {
            const ElementHeader groupLengthHeader = readElementHeader(cursor, true, false);
            if (groupLengthHeader.tag != tags::FileMetaGroupLength)
            {
                throw ParseError("Expected FileMetaGroupLength (0002,0000) as first element");
            }
            const auto groupLengthBytes = cursor.readBytes(groupLengthHeader.length);
            uint32_t metaGroupLength = 0;
            if (groupLengthBytes.size() >= 4)
            {
                metaGroupLength = static_cast<uint32_t>(groupLengthBytes[0]) |
                                  (static_cast<uint32_t>(groupLengthBytes[1]) << 8) |
                                  (static_cast<uint32_t>(groupLengthBytes[2]) << 16) |
                                  (static_cast<uint32_t>(groupLengthBytes[3]) << 24);
            }
            const size_t metaEnd = cursor.position() + metaGroupLength;
            while (cursor.position() < metaEnd)
            {
                ElementHeader header = readElementHeader(cursor, true, false);
                if (header.vr == VR::SQ)
                {
                    skipSequence(cursor, header.length, true, false);
                    continue;
                }
                Element element{header.tag, header.vr, cursor.readBytes(header.length)};
                metaInfo.insert(std::move(element));
            }
        }

        const auto transferSyntaxUid = metaInfo.getString(tags::TransferSyntaxUID);
        if (!transferSyntaxUid.has_value())
        {
            throw ParseError("File Meta Info is missing TransferSyntaxUID (0002,0010)");
        }
        const TransferSyntax ts = resolveTransferSyntax(*transferSyntaxUid);
        const bool explicitVR = (ts != TransferSyntax::ImplicitVRLittleEndian);
        const bool bigEndian = (ts == TransferSyntax::ExplicitVRBigEndian);

        Dataset dataset;
        while (!cursor.atEnd())
        {
            ElementHeader header = readElementHeader(cursor, explicitVR, bigEndian);
            if (header.vr == VR::SQ)
            {
                skipSequence(cursor, header.length, explicitVR, bigEndian);
                continue;
            }
            if (header.length == kUndefinedLength)
            {
                throw ParseError("Unexpected undefined length on a non-sequence element");
            }
            Element element{header.tag, header.vr, cursor.readBytes(header.length)};
            dataset.insert(std::move(element));
        }

        Slice slice;
        slice.metadata = dataset;
        slice.modality = dataset.getString(tags::Modality).value_or("UNKNOWN");
        slice.rows = dataset.getUInt16(tags::Rows, bigEndian).value_or(0);
        slice.columns = dataset.getUInt16(tags::Columns, bigEndian).value_or(0);
        slice.bitsAllocated = dataset.getUInt16(tags::BitsAllocated, bigEndian).value_or(16);
        slice.pixelRepresentationSigned = dataset.getUInt16(tags::PixelRepresentation, bigEndian).value_or(0) != 0;
        slice.rescaleSlope = dataset.getDouble(tags::RescaleSlope).value_or(1.0);
        slice.rescaleIntercept = dataset.getDouble(tags::RescaleIntercept).value_or(0.0);

        if (const auto pos = dataset.getDoubleList(tags::ImagePositionPatient); pos.size() >= 3)
        {
            slice.imagePositionZ = pos[2];
        }
        if (const auto spacing = dataset.getDoubleList(tags::PixelSpacing); spacing.size() >= 2)
        {
            slice.pixelSpacingRowMM = spacing[0];
            slice.pixelSpacingColMM = spacing[1];
        }
        slice.sliceThicknessMM = dataset.getDouble(tags::SliceThickness).value_or(1.0);

        const auto *pixelBytes = dataset.getRawBytes(tags::PixelData);
        if (pixelBytes == nullptr)
        {
            throw ParseError("No PixelData (7FE0,0010) element found in: " + path);
        }

        const size_t expectedPixelCount = static_cast<size_t>(slice.rows) * static_cast<size_t>(slice.columns);
        slice.pixels.resize(expectedPixelCount);

        if (slice.bitsAllocated == 16)
        {
            if (pixelBytes->size() < expectedPixelCount * 2)
            {
                throw ParseError("PixelData shorter than Rows*Columns*2 bytes implies for 16-bit data");
            }
            for (size_t i = 0; i < expectedPixelCount; ++i)
            {
                const auto b0 = static_cast<uint8_t>((*pixelBytes)[i * 2]);
                const auto b1 = static_cast<uint8_t>((*pixelBytes)[i * 2 + 1]);
                const uint16_t raw16 = bigEndian ? static_cast<uint16_t>((b0 << 8) | b1)
                                                 : static_cast<uint16_t>((b1 << 8) | b0);
                slice.pixels[i] = slice.pixelRepresentationSigned
                                      ? static_cast<int32_t>(static_cast<int16_t>(raw16))
                                      : static_cast<int32_t>(raw16);
            }
        }
        else if (slice.bitsAllocated == 8)
        {
            if (pixelBytes->size() < expectedPixelCount)
            {
                throw ParseError("PixelData shorter than Rows*Columns bytes implies for 8-bit data");
            }
            for (size_t i = 0; i < expectedPixelCount; ++i)
            {
                const auto raw8 = static_cast<uint8_t>((*pixelBytes)[i]);
                slice.pixels[i] = slice.pixelRepresentationSigned
                                      ? static_cast<int32_t>(static_cast<int8_t>(raw8))
                                      : static_cast<int32_t>(raw8);
            }
        }
        else
        {
            throw ParseError("Unsupported BitsAllocated value (only 8 and 16 are handled)");
        }

        return slice;
    }

} // namespace dicom
