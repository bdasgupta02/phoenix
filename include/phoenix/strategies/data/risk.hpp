#pragma once

#include <cstdlib>

namespace phoenix::data {

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

} // namespace phoenix::data
