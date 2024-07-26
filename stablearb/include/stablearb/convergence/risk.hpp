#pragma once

#include <atomic>

namespace stablearb {

template<typename NodeBase>
struct Risk : NodeBase
{
    using NodeBase::NodeBase;

    using PriceType = NodeBase::Traits::PriceType;

    void handle(tag::Risk::Abort) { aborted.test_and_set(); }

private:
    inline void checkAbort()
    {
        if (aborted.test())
        {
            this->getHandler()->invoke(tag::Sender::MassCancel{});
            this->getHandler()->invoke(tag::Stream::Stop{});
            this->getHandler()->invoke(tag::Logger::Stop{});
            std::abort();
        }
    }

    // To be checked everytime a handler except Abort is called
    std::atomic_flag aborted = ATOMIC_FLAG_INIT;
};

} // namespace stablearb
