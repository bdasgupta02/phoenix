#include "phoenix/tools/fix_circular_buffer.hpp"

#include <charconv>
#include <cstring>

namespace phoenix {

boost::asio::mutable_buffer FIXCircularBuffer::getAsioBuffer()
{
    return {buffer.data() + end, BUFFER_CAPACITY - end};
}

std::optional<std::string_view> FIXCircularBuffer::getMsg(std::size_t bytesRead)
{
    std::size_t const newEnd = end + bytesRead;
    /*assert((newEnd < BUFFER_CAPACITY) && "Circular buffer overflow");*/

    std::optional<std::string_view> result;

    if (newEnd - start < FIX_EXPECTED_MIN_LENGTH)
        return result;

    std::size_t i = start;
    while (i < newEnd && buffer[i] != '\x01')
        ++i;

    ++i;

    if (
        i < newEnd &&
        buffer[i] == '9' && 
        buffer[i + 1u] == '='
    )
    {
        i += 2u;
        char* lenStart = &buffer[i];

        while (i < newEnd)
        {
            if (buffer[i] == '\x01')
            {
                std::size_t length = 0u;
                std::from_chars(lenStart, lenStart + i, length);
                std::size_t msgEnd = i + 1u + length + FIX_CHECKSUM_LENGTH;

                result = {buffer.data() + start, msgEnd - start};
                advanceMoveOverflow(msgEnd, newEnd);
                return result;
            }

            ++i;
        }
    }

    advanceMoveOverflow(start, newEnd);
    return result;
}

void FIXCircularBuffer::advanceMoveOverflow(std::size_t newStart, std::size_t newEnd)
{
    if (newEnd < BUFFER_CAPACITY - BUFFER_WRAP_BOUNDARY) [[likely]]
    {
        start = newStart;
        end = newEnd;
    }
    else 
    {
        std::memcpy(buffer.data(), buffer.data() + newStart, newEnd - newStart);
        start = 0u;
        end = newEnd - newStart;
    }
}

}
