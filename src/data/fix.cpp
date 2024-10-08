#include "phoenix/data/fix.hpp"

namespace phoenix {

// TODO: add checksum verification in the same loop
FIXReader::FIXReader(std::string_view data)
{
    std::string_view::size_type pos = 0;
    while (pos < data.size())
    {
        auto tagEnd = data.find('=', pos);
        if (tagEnd == std::string_view::npos)
            break;

        auto valueEnd = data.find('\x01', tagEnd + 1);
        if (valueEnd == std::string_view::npos)
            break;

        std::string tag{data.substr(pos, tagEnd - pos)};
        std::string value{data.substr(tagEnd + 1, valueEnd - tagEnd - 1)};

        fields[tag].emplace_back(std::move(value));
        pos = valueEnd + 1;
    }

    msgType = getStringView("35");
}

} // namespace phoenix
