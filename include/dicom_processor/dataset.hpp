#pragma once
#include "dicom_processor/tag.hpp"
#include "dicom_processor/vr.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dicom
{

    struct Element
    {
        Tag tag;
        VR vr{VR::Unknown};
        std::vector<std::byte> value;
    };

    class Dataset
    {
    public:
        void insert(Element element);
        const Element *find(Tag tag) const;
        std::optional<std::string> getString(Tag tag) const;
        std::optional<uint16_t> getUInt16(Tag tag, bool bigEndian) const;
        std::optional<double> getDouble(Tag tag) const;
        std::vector<double> getDoubleList(Tag tag) const;
        const std::vector<std::byte> *getRawBytes(Tag tag) const;
        size_t size() const { return elements_.size(); }

    private:
        std::unordered_map<Tag, Element, TagHash> elements_;
    };

} // namespace dicom
