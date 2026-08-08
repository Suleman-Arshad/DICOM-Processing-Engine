#pragma once
#include <cstdint>
#include <string_view>

namespace dicom {

enum class VR : uint8_t {
    AE, AS, AT, CS, DA, DS, DT, FL, FD, IS, LO, LT,
    OB, OD, OF, OL, OW, PN, SH, SL, SQ, SS, ST, TM,
    UC, UI, UL, UN, UR, US, UT,
    Unknown
};

VR vrFromString(std::string_view code);
std::string_view vrToString(VR vr);
bool usesLongLengthForm(VR vr);

}  // namespace dicom
