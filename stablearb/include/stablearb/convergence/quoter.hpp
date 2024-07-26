#pragma once

namespace stablearb {

template<typename NodeBase>
struct Quoter : NodeBase
{
    using NodeBase::NodeBase;

    using PriceType = NodeBase::Traits::PriceType;
};

} // namespace stablearb
