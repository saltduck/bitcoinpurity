// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#ifndef BITCOIN_MINING_SV2_TEMPLATE_H
#define BITCOIN_MINING_SV2_TEMPLATE_H
#include <mining/sv2_ffi.h>
#include <memory>
class UniValue;
namespace node { struct NodeContext; }
namespace mining {
class Sv2TemplateProvider {
public:
    explicit Sv2TemplateProvider(node::NodeContext&);
    ~Sv2TemplateProvider();
    UniValue Request(const UniValue&);
    void Interrupt();
    Sv2NodeCallbacks Callbacks();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
#endif // BITCOIN_MINING_SV2_TEMPLATE_H
