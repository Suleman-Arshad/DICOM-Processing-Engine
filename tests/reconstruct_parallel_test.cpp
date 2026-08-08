#include "dicom_processor/reconstruction.hpp"
#include <iostream>
#include <random>

std::vector<dicom::Slice> makeSeries(int sliceCount, int rows, int cols) {
    std::vector<dicom::Slice> slices;
    std::mt19937 rng(123);
    std::uniform_real_distribution<float> noise(-100.f, 100.f);

    for (int z = 0; z < sliceCount; ++z) {
        dicom::Slice s;
        s.rows = rows;
        s.columns = cols;
        s.bitsAllocated = 16;
        s.modality = "CT";
        s.rescaleSlope = 1.0;
        s.rescaleIntercept = -1024.0;
        s.pixelSpacingRowMM = 0.8;
        s.pixelSpacingColMM = 0.8;
        s.sliceThicknessMM = 1.5;
        s.imagePositionZ = z * 1.5;
        s.pixels.resize(static_cast<size_t>(rows) * cols);
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                s.pixels[static_cast<size_t>(y) * cols + x] =
                    static_cast<int32_t>(x * 5 + y * 3 + z * 2 + noise(rng));
            }
        }
        slices.push_back(s);
    }
    return slices;
}

int main() {
    const auto series = makeSeries(20, 32, 32);
    const dicom::VoxelVolume serial = dicom::VolumeReconstructor::reconstruct(series);

    bool allMatch = true;
    for (size_t threadCount : {1u, 2u, 4u, 8u}) {
        dicom::ThreadPool pool(threadCount);
        const dicom::VoxelVolume parallel =
            dicom::VolumeReconstructor::reconstructParallel(series, pool);

        if (parallel.width != serial.width || parallel.height != serial.height ||
            parallel.depth != serial.depth || parallel.data.size() != serial.data.size()) {
            std::cout << "MISMATCH (dimensions) at threadCount=" << threadCount << "\n";
            allMatch = false;
            continue;
        }

        bool identical = true;
        for (size_t i = 0; i < serial.data.size(); ++i) {
            if (serial.data[i] != parallel.data[i]) {
                identical = false;
                break;
            }
        }
        allMatch &= identical;
        std::cout << (identical ? "[PASS] " : "[FAIL] ") << "threadCount=" << threadCount
                  << " output bit-identical to serial\n";
    }

    std::cout << (allMatch ? "\nALL MATCH\n" : "\nMISMATCH DETECTED\n");
    return allMatch ? 0 : 1;
}
