#include "dicom_processor/dictionary.hpp"
#include <unordered_map>

namespace dicom {

VR lookupImplicitVR(Tag tag) {
    static const std::unordered_map<Tag, VR, TagHash> table{
        {tags::TransferSyntaxUID, VR::UI},
        {tags::Modality, VR::CS},
        {tags::StudyDate, VR::DA},
        {tags::PatientID, VR::LO},
        {tags::SliceThickness, VR::DS},
        {tags::ImagePositionPatient, VR::DS},
        {tags::SamplesPerPixel, VR::US},
        {tags::PhotometricInterpretation, VR::CS},
        {tags::Rows, VR::US},
        {tags::Columns, VR::US},
        {tags::PixelSpacing, VR::DS},
        {tags::BitsAllocated, VR::US},
        {tags::BitsStored, VR::US},
        {tags::PixelRepresentation, VR::US},
        {tags::RescaleIntercept, VR::DS},
        {tags::RescaleSlope, VR::DS},
        {tags::PixelData, VR::OW},
    };
    const auto it = table.find(tag);
    return it != table.end() ? it->second : VR::Unknown;
}

}  // namespace dicom
