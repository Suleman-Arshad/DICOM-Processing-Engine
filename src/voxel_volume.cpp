#include "dicom_processor/voxel_volume.hpp"
#include <algorithm>
#include <cctype>

namespace dicom {

Modality modalityFromString(const std::string& s) {
    std::string upper = s;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper == "CT") return Modality::CT;
    if (upper == "MR") return Modality::MR;
    if (upper == "CR" || upper == "DX" || upper == "XR") return Modality::XR;
    return Modality::Unknown;
}

std::string modalityToString(Modality m) {
    switch (m) {
        case Modality::CT: return "CT";
        case Modality::MR: return "MR";
        case Modality::XR: return "XR";
        default: return "UNKNOWN";
    }
}

}  // namespace dicom
