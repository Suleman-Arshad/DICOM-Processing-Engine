#include "dicom_processor/dataset.hpp"
#include <algorithm>
#include <cstring>

namespace dicom {

void Dataset::insert(Element element) {
    const Tag tag = element.tag;
    elements_.insert_or_assign(tag, std::move(element));
}

const Element* Dataset::find(Tag tag) const {
    const auto it = elements_.find(tag);
    return it != elements_.end() ? &it->second : nullptr;
}

std::optional<std::string> Dataset::getString(Tag tag) const {
    const Element* element = find(tag);
    if (element == nullptr || element->value.empty()) return std::nullopt;
    std::string text(reinterpret_cast<const char*>(element->value.data()), element->value.size());
    while (!text.empty() && (text.back() == ' ' || text.back() == '\0')) {
        text.pop_back();
    }
    return text;
}

std::optional<uint16_t> Dataset::getUInt16(Tag tag, bool bigEndian) const {
    const Element* element = find(tag);
    if (element == nullptr || element->value.size() < 2) return std::nullopt;
    const auto b0 = static_cast<uint8_t>(element->value[0]);
    const auto b1 = static_cast<uint8_t>(element->value[1]);
    return bigEndian ? static_cast<uint16_t>((b0 << 8) | b1) : static_cast<uint16_t>((b1 << 8) | b0);
}

std::optional<double> Dataset::getDouble(Tag tag) const {
    const auto text = getString(tag);
    if (!text.has_value() || text->empty()) return std::nullopt;
    const auto sep = text->find('\\');
    const std::string first = sep == std::string::npos ? *text : text->substr(0, sep);
    try {
        return std::stod(first);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

const std::vector<std::byte>* Dataset::getRawBytes(Tag tag) const {
    const Element* element = find(tag);
    return element != nullptr ? &element->value : nullptr;
}

std::vector<double> Dataset::getDoubleList(Tag tag) const {
    std::vector<double> values;
    const auto text = getString(tag);
    if (!text.has_value() || text->empty()) return values;
    size_t start = 0;
    while (start <= text->size()) {
        const size_t sep = text->find('\\', start);
        const std::string token = (sep == std::string::npos) ? text->substr(start) : text->substr(start, sep - start);
        try {
            values.push_back(std::stod(token));
        } catch (const std::exception&) {
            values.push_back(0.0);
        }
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return values;
}

}  // namespace dicom
