#include "dicom_processor/parser.hpp"
#include <algorithm>
#include <iostream>
#include <limits>

namespace
{

    void printSlice(const std::string &path, const dicom::Slice &slice)
    {
        std::cout << "=== " << path << " ===\n";
        std::cout << "Modality:          " << slice.modality << '\n';
        std::cout << "Rows x Columns:    " << slice.rows << " x " << slice.columns << '\n';
        std::cout << "Bits Allocated:    " << slice.bitsAllocated << '\n';
        std::cout << "Pixel Repr.:       " << (slice.pixelRepresentationSigned ? "signed" : "unsigned") << '\n';
        std::cout << "Rescale Slope:     " << slice.rescaleSlope << '\n';
        std::cout << "Rescale Intercept: " << slice.rescaleIntercept << '\n';
        std::cout << "Pixel Count:       " << slice.pixels.size() << '\n';

        if (slice.pixels.empty())
        {
            std::cout << "(no pixel data decoded)\n\n";
            return;
        }

        const auto [minIt, maxIt] = std::minmax_element(slice.pixels.begin(), slice.pixels.end());
        std::cout << "Raw pixel range:   [" << *minIt << ", " << *maxIt << "]\n";

        if (slice.modality == "CT")
        {
            const double huMin = static_cast<double>(*minIt) * slice.rescaleSlope + slice.rescaleIntercept;
            const double huMax = static_cast<double>(*maxIt) * slice.rescaleSlope + slice.rescaleIntercept;
            std::cout << "HU range:          [" << huMin << ", " << huMax << "]\n";
        }

        const int patch = std::min({5, slice.rows, slice.columns});
        std::cout << "Top-left " << patch << "x" << patch << " raw pixel patch:\n";
        for (int y = 0; y < patch; ++y)
        {
            std::cout << "  ";
            for (int x = 0; x < patch; ++x)
            {
                const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(slice.columns) + static_cast<size_t>(x);
                std::cout << slice.pixels[idx] << ' ';
            }
            std::cout << '\n';
        }
        std::cout << '\n';
    }

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <dicom-file> [dicom-file ...]\n";
        return EXIT_FAILURE;
    }

    int failures = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string path = argv[i];
        try
        {
            const dicom::Slice slice = dicom::Parser::parseFile(path);
            printSlice(path, slice);
        }
        catch (const dicom::ParseError &e)
        {
            std::cerr << "Parse error in '" << path << "': " << e.what() << "\n\n";
            ++failures;
        }
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}