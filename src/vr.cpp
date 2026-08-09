#include "dicom_processor/vr.hpp"
#include <array>
#include <utility>

namespace dicom
{

    VR vrFromString(std::string_view code)
    {
        static constexpr std::array<std::pair<std::string_view, VR>, 31> table{{
            {"AE", VR::AE},
            {"AS", VR::AS},
            {"AT", VR::AT},
            {"CS", VR::CS},
            {"DA", VR::DA},
            {"DS", VR::DS},
            {"DT", VR::DT},
            {"FL", VR::FL},
            {"FD", VR::FD},
            {"IS", VR::IS},
            {"LO", VR::LO},
            {"LT", VR::LT},
            {"OB", VR::OB},
            {"OD", VR::OD},
            {"OF", VR::OF},
            {"OL", VR::OL},
            {"OW", VR::OW},
            {"PN", VR::PN},
            {"SH", VR::SH},
            {"SL", VR::SL},
            {"SQ", VR::SQ},
            {"SS", VR::SS},
            {"ST", VR::ST},
            {"TM", VR::TM},
            {"UC", VR::UC},
            {"UI", VR::UI},
            {"UL", VR::UL},
            {"UN", VR::UN},
            {"UR", VR::UR},
            {"US", VR::US},
            {"UT", VR::UT},
        }};
        for (const auto &[str, vr] : table)
        {
            if (str == code)
                return vr;
        }
        return VR::Unknown;
    }

    std::string_view vrToString(VR vr)
    {
        switch (vr)
        {
        case VR::AE:
            return "AE";
        case VR::AS:
            return "AS";
        case VR::AT:
            return "AT";
        case VR::CS:
            return "CS";
        case VR::DA:
            return "DA";
        case VR::DS:
            return "DS";
        case VR::DT:
            return "DT";
        case VR::FL:
            return "FL";
        case VR::FD:
            return "FD";
        case VR::IS:
            return "IS";
        case VR::LO:
            return "LO";
        case VR::LT:
            return "LT";
        case VR::OB:
            return "OB";
        case VR::OD:
            return "OD";
        case VR::OF:
            return "OF";
        case VR::OL:
            return "OL";
        case VR::OW:
            return "OW";
        case VR::PN:
            return "PN";
        case VR::SH:
            return "SH";
        case VR::SL:
            return "SL";
        case VR::SQ:
            return "SQ";
        case VR::SS:
            return "SS";
        case VR::ST:
            return "ST";
        case VR::TM:
            return "TM";
        case VR::UC:
            return "UC";
        case VR::UI:
            return "UI";
        case VR::UL:
            return "UL";
        case VR::UN:
            return "UN";
        case VR::UR:
            return "UR";
        case VR::US:
            return "US";
        case VR::UT:
            return "UT";
        default:
            return "??";
        }
    }

    bool usesLongLengthForm(VR vr)
    {
        switch (vr)
        {
        case VR::OB:
        case VR::OD:
        case VR::OF:
        case VR::OL:
        case VR::OW:
        case VR::SQ:
        case VR::UC:
        case VR::UN:
        case VR::UR:
        case VR::UT:
            return true;
        default:
            return false;
        }
    }

} // namespace dicom
