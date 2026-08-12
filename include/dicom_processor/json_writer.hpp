#pragma once
#include <sstream>
#include <string>
#include <vector>

namespace dicom::json
{

    std::string escape(const std::string &s);

    // Small builder for flat/nested JSON objects. Not a general-purpose JSON
    // library -- just enough to emit the findings report and FHIR bundle.
    class Writer
    {
    public:
        Writer &beginObject();
        Writer &endObject();
        Writer &beginArray();
        Writer &endArray();
        Writer &key(const std::string &k);
        Writer &value(const std::string &v);
        Writer &value(double v);
        Writer &value(long long v);
        Writer &value(bool v);
        Writer &rawValue(const std::string &jsonLiteral); // for nested pre-built JSON

        std::string str() const { return out_.str(); }

    private:
        void beforeItem();
        std::ostringstream out_;
        std::vector<bool> hasItem_; // one entry per open container
        bool pendingValue_{false};  // true between key() and its value
    };

} // namespace dicom::json
