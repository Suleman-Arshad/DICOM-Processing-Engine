#include "dicom_processor/parser.hpp"
#include "dicom_processor/reconstruction.hpp"

#include <algorithm>
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <slice1.dcm> <slice2.dcm> ...\n";
        std::cerr << "  Stacks the given 2D DICOM slices into one 3D voxel volume.\n";
        return EXIT_FAILURE;
    }

    std::vector<dicom::Slice> slices;
    slices.reserve(static_cast<size_t>(argc - 1));

    for (int i = 1; i < argc; ++i) {
        try {
            slices.push_back(dicom::Parser::parseFile(argv[i]));
        } catch (const dicom::ParseError& e) {
            std::cerr << "Parse error in '" << argv[i] << "': " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    }

    try {
        const int inputSliceCount = static_cast<int>(slices.size());
        const dicom::VoxelVolume volume = dicom::VolumeReconstructor::reconstruct(std::move(slices));

        std::cout << "=== Reconstructed Volume ===\n";
        std::cout << "Input slices:       " << inputSliceCount << '\n';
        std::cout << "Modality:           " << dicom::modalityToString(volume.modality) << '\n';
        std::cout << "Dimensions (WxHxD): " << volume.width << " x " << volume.height << " x "
                  << volume.depth << '\n';
        std::cout << "Voxel spacing:      " << volume.voxelSpacingMM << " mm (isotropic)\n";
        std::cout << "Total voxels:       " << volume.data.size() << '\n';

        if (!volume.data.empty()) {
            const auto [minIt, maxIt] = std::minmax_element(volume.data.begin(), volume.data.end());
            std::cout << "Value range:        [" << *minIt << ", " << *maxIt << "]"
                      << (volume.modality == dicom::Modality::CT ? " HU" : "") << '\n';
        }
    } catch (const dicom::ReconstructionError& e) {
        std::cerr << "Reconstruction error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
