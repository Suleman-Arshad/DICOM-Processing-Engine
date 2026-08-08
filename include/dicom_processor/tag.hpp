#pragma once
#include <cstdint>
#include <functional>

namespace dicom {

struct Tag {
    uint16_t group{};
    uint16_t element{};

    friend bool operator==(const Tag& lhs, const Tag& rhs) {
        return lhs.group == rhs.group && lhs.element == rhs.element;
    }
    friend bool operator!=(const Tag& lhs, const Tag& rhs) {
        return !(lhs == rhs);
    }
};

struct TagHash {
    size_t operator()(const Tag& t) const noexcept {
        return (static_cast<size_t>(t.group) << 16) | static_cast<size_t>(t.element);
    }
};

namespace structural_tags {
inline constexpr Tag Item{0xFFFE, 0xE000};
inline constexpr Tag ItemDelimitation{0xFFFE, 0xE00D};
inline constexpr Tag SequenceDelimitation{0xFFFE, 0xE0DD};
}  // namespace structural_tags

namespace tags {
inline constexpr Tag FileMetaGroupLength{0x0002, 0x0000};
inline constexpr Tag TransferSyntaxUID{0x0002, 0x0010};

inline constexpr Tag Modality{0x0008, 0x0060};
inline constexpr Tag StudyDate{0x0008, 0x0020};

inline constexpr Tag PatientID{0x0010, 0x0020};

inline constexpr Tag SliceThickness{0x0018, 0x0050};

inline constexpr Tag ImagePositionPatient{0x0020, 0x0032};

inline constexpr Tag SamplesPerPixel{0x0028, 0x0002};
inline constexpr Tag PhotometricInterpretation{0x0028, 0x0004};
inline constexpr Tag Rows{0x0028, 0x0010};
inline constexpr Tag Columns{0x0028, 0x0011};
inline constexpr Tag PixelSpacing{0x0028, 0x0030};
inline constexpr Tag BitsAllocated{0x0028, 0x0100};
inline constexpr Tag BitsStored{0x0028, 0x0101};
inline constexpr Tag PixelRepresentation{0x0028, 0x0103};
inline constexpr Tag RescaleIntercept{0x0028, 0x1052};
inline constexpr Tag RescaleSlope{0x0028, 0x1053};

inline constexpr Tag PixelData{0x7FE0, 0x0010};
}  // namespace tags

}  // namespace dicom
