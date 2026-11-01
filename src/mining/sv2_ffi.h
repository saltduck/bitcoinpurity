// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#ifndef BITCOIN_MINING_SV2_FFI_H
#define BITCOIN_MINING_SV2_FFI_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    void* context;
    int32_t (*request)(void*, const uint8_t*, size_t, uint8_t**, size_t*);
    void (*release)(uint8_t*, size_t);
} Sv2NodeCallbacks;
#ifdef __cplusplus
}
#endif
#endif
