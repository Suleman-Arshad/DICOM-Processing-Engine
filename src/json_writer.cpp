#include "dicom_processor/json_writer.hpp"
#include <cstdio>

namespace dicom::json
{

    std::string escape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            switch (c)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += c;
                }
            }
        }
        return out;
    }

    // Handles comma insertion for the enclosing container, unless this call
    // is the value half of a key()/value() pair (comma already handled by key()).
    void Writer::beforeItem()
    {
        if (pendingValue_)
        {
            pendingValue_ = false;
            return;
        }
        if (!hasItem_.empty())
        {
            if (hasItem_.back())
                out_ << ',';
            hasItem_.back() = true;
        }
    }

    Writer &Writer::beginObject()
    {
        beforeItem();
        out_ << '{';
        hasItem_.push_back(false);
        return *this;
    }

    Writer &Writer::endObject()
    {
        hasItem_.pop_back();
        out_ << '}';
        return *this;
    }

    Writer &Writer::beginArray()
    {
        beforeItem();
        out_ << '[';
        hasItem_.push_back(false);
        return *this;
    }

    Writer &Writer::endArray()
    {
        hasItem_.pop_back();
        out_ << ']';
        return *this;
    }

    Writer &Writer::key(const std::string &k)
    {
        if (!hasItem_.empty())
        {
            if (hasItem_.back())
                out_ << ',';
            hasItem_.back() = true;
        }
        out_ << '"' << escape(k) << "\":";
        pendingValue_ = true;
        return *this;
    }

    Writer &Writer::value(const std::string &v)
    {
        beforeItem();
        out_ << '"' << escape(v) << '"';
        return *this;
    }

    Writer &Writer::value(double v)
    {
        beforeItem();
        out_ << v;
        return *this;
    }

    Writer &Writer::value(long long v)
    {
        beforeItem();
        out_ << v;
        return *this;
    }

    Writer &Writer::value(bool v)
    {
        beforeItem();
        out_ << (v ? "true" : "false");
        return *this;
    }

    Writer &Writer::rawValue(const std::string &jsonLiteral)
    {
        beforeItem();
        out_ << jsonLiteral;
        return *this;
    }

} // namespace dicom::json
