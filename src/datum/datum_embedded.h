/* Copyright (c) 2026 The Bitcoin Purity developers
 * Distributed under the MIT software license. */
#ifndef BITCOIN_DATUM_EMBEDDED_H
#define BITCOIN_DATUM_EMBEDDED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* listen_address;
    uint16_t listen_port;
    uint32_t max_clients;
    uint32_t max_clients_per_ip;
    bool auth_required;
    const char* auth_user;
    const char* auth_password;
    uint32_t auth_timeout_seconds;
    const char* rpc_url;
    const char* rpc_user;
    const char* rpc_password;
    const char* rpc_cookie_file;
    const unsigned char* payout_script;
    size_t payout_script_len;
    const char* payout_address;
    uint64_t share_difficulty;
    const char* coinbase_tag;
} datum_embedded_config;

typedef struct {
    bool running;
    uint32_t clients;
    uint32_t authorized_clients;
    uint64_t accepted_shares;
    uint64_t rejected_shares;
    uint64_t current_height;
} datum_embedded_stats;

int datum_embedded_start(const datum_embedded_config* config, char* error, size_t error_size);
void datum_embedded_interrupt(void);
void datum_embedded_stop(void);
void datum_request_template_refresh(void);
bool datum_embedded_should_stop(void);
void datum_embedded_request_stop(void);
void datum_embedded_get_stats(datum_embedded_stats* stats);
uint64_t datum_embedded_get_share_difficulty(void);
int datum_embedded_update_share_difficulty(uint64_t difficulty, char* error, size_t error_size);

/* Implemented by the C++ bridge. */
void datum_bridge_log(int level, const char* message);
void datum_bridge_fatal_error(const char* message);

#ifdef __cplusplus
}
#endif

#endif
