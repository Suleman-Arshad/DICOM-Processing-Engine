#pragma once
#include "dicom_processor/detection.hpp"
#include "dicom_processor/parser.hpp"
#include <string>
#include <vector>

namespace dicom {

class DicomWriter {
public:
    // Writes a new Explicit VR Little Endian DICOM file: core geometry/
    // identity tags re-encoded from the already-parsed Slice (safe
    // regardless of the source file's original endianness), original
    // pixel data, plus a private tag holding the findings as JSON text.
    // Arbitrary other elements from the source file are not carried
    // through -- only the tags this project parses.
    static void writeAnnotated(const Slice& slice, const std::vector<Anomaly>& findings,
                                const std::string& outputPath);
};

}  // namespace dicom
