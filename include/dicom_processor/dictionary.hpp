#pragma once
#include "dicom_processor/tag.hpp"
#include "dicom_processor/vr.hpp"

namespace dicom {
VR lookupImplicitVR(Tag tag);
}  // namespace dicom
