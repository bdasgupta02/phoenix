#include "phoenix/tools/fix_circular_buffer.hpp"

#include <iostream>

namespace phoenix {

boost::asio::mutable_buffer FIXCircularBuffer::getAsioBuffer()
{
    return {buffer.data() + end, BUFFER_CAPACITY - end};
}

std::optional<std::string_view> FIXCircularBuffer::getMsg(std::size_t bytesRead)
{
    std::size_t const newEnd = end + bytesRead;
    assert((newEnd < BUFFER_CAPACITY) && "Circular buffer overflow");

    std::optional<std::string_view> result;
    std::size_t i = start;
    while (i + 3 < newEnd)
    {
        if (
            buffer[i] == '\x01' && 
            buffer[i + 1] == '1' && 
            buffer[i + 2] == '0' &&
            buffer[i + 3] == '=')
        {
            auto j = i + 6;
            if (j >= newEnd)
                break;

            result = {buffer.data() + start, j - start + 1u};
            advanceMoveOverflow(j + 1u, newEnd);
            return result;
        }

        ++i;
    }

    advanceMoveOverflow(start, newEnd);
    return result;
}

void FIXCircularBuffer::advanceMoveOverflow(std::size_t newStart, std::size_t newEnd)
{
    if (newEnd < BUFFER_CAPACITY - BUFFER_WRAP_BOUNDARY)
    {
        start = newStart;
        end = newEnd;
    }
    else 
    {
        std::memmove(buffer.data(), buffer.data() + newStart, newEnd - newStart);
        start = 0u;
        end = newEnd - newStart;
    }
}

}
