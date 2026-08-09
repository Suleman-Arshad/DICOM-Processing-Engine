#pragma once
#include "dicom_processor/dataset.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace dicom
{

    enum class TransferSyntax
    {
        ImplicitVRLittleEndian,
        ExplicitVRLittleEndian,
        ExplicitVRBigEndian,
    };

    struct ParseError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct Slice
    {
        Dataset metadata;
        std::vector<int32_t> pixels; // row-major, sign-corrected per PixelRepresentation

        std::string modality;
        int rows{0};
        int columns{0};
        int bitsAllocated{0};
        bool pixelRepresentationSigned{false};
        double rescaleSlope{1.0};
        double rescaleIntercept{0.0};

        double imagePositionZ{0.0};
        double pixelSpacingRowMM{1.0};
        double pixelSpacingColMM{1.0};
        double sliceThicknessMM{1.0};

        double huAt(int x, int y) const
        {
            const size_t index = static_cast<size_t>(y) * static_cast<size_t>(columns) + static_cast<size_t>(x);
            return static_cast<double>(pixels.at(index)) * rescaleSlope + rescaleIntercept;
        }
    };

    class Parser
    {
    public:
        static Slice parseFile(const std::string &path);
    };

} // namespace dicom
