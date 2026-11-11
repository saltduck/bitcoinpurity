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
int32_t purity_sv2_validate(const uint8_t*, size_t);
void* purity_sv2_start(const uint8_t*, size_t, Sv2NodeCallbacks);
size_t purity_sv2_status(const void*, uint8_t*, size_t);
void purity_sv2_interrupt(const void*);
void purity_sv2_stop(void*);
#ifdef __cplusplus
}
#endif
#endif // BITCOIN_MINING_SV2_FFI_H
