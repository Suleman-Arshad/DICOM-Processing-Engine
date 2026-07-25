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
        ImplicitVRLittleEndian, // 1.2.840.10008.1.2
        ExplicitVRLittleEndian, // 1.2.840.10008.1.2.1
        ExplicitVRBigEndian,    // 1.2.840.10008.1.2.2
    };

    struct ParseError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct Slice
    {
        Dataset metadata;
        std::vector<int32_t> pixels; // row-major, size == rows * columns

        std::string modality;
        int rows{0};
        int columns{0};
        int bitsAllocated{0};
        bool pixelRepresentationSigned{false}; // from (0028,0103): false=unsigned, true=signed
        double rescaleSlope{1.0};
        double rescaleIntercept{0.0};

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