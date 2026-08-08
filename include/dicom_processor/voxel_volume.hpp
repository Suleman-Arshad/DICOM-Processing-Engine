#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace dicom {

enum class Modality { CT, MR, XR, Unknown };
Modality modalityFromString(const std::string& s);
std::string modalityToString(Modality m);

struct VoxelVolume {
    int width{0};
    int height{0};
    int depth{0};
    double voxelSpacingMM{1.0};
    Modality modality{Modality::Unknown};
    std::vector<float> data;

    size_t index(int x, int y, int z) const {
        return (static_cast<size_t>(z) * static_cast<size_t>(height) + static_cast<size_t>(y)) *
                   static_cast<size_t>(width) + static_cast<size_t>(x);
    }
    float at(int x, int y, int z) const { return data[index(x, y, z)]; }
    float& at(int x, int y, int z) { return data[index(x, y, z)]; }

    const float* slicePtr(int z) const { return data.data() + static_cast<size_t>(z) * width * height; }
    float* slicePtr(int z) { return data.data() + static_cast<size_t>(z) * width * height; }
};

}  // namespace dicom
