#include "stablearb/data/fix.hpp"

namespace stablearb {

// TODO: add checksum verification in the same loop
FIXReader::FIXReader(std::string_view data)
{
    std::string_view::size_type pos = 0;
    while (pos < data.size())
    {
        auto tagEnd = data.find('=', pos);
        if (tagEnd == std::string_view::npos)
            break;

        auto valueEnd = data.find('|', tagEnd + 1);
        if (valueEnd == std::string_view::npos)
            break;

        fields.emplace(data.substr(pos, tagEnd - pos), data.substr(tagEnd + 1, valueEnd - tagEnd - 1));
        pos = valueEnd + 1;
    }
}

} // namespace stablearb
