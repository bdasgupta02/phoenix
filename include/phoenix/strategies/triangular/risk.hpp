#pragma once

#include <cstdlib>

namespace phoenix::triangular {

template<typename NodeBase>
struct Risk : NodeBase
{
    using NodeBase::NodeBase;

    void handle(tag::Risk::Abort)
    {
        this->getHandler()->invoke(tag::Stream::Stop{});
        this->getHandler()->invoke(tag::Logger::Stop{});
        std::abort();
    }
};

} // namespace phoenix::triangular
