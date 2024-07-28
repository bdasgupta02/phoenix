#include "stablearb/data/fix.hpp"

namespace stablearb {

FIXBuilder::FIXBuilder() { buffer.reserve(8192u); }

FIXBuilder::FIXBuilder(std::size_t seqNum, char msgType)
{
    buffer.reserve(8192u);
    appendHeader(seqNum, msgType);
}

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

FIXReader::FIXReader(boost::asio::streambuf& buffer)
{
    char const* data = boost::asio::buffer_cast<char const*>(buffer.data());
    std::size_t size = buffer.size();
    *this = FIXReader{std::string_view{data, size}};
    buffer.consume(size);
}

} // namespace stablearb
