/* Copyright (c) 2026 The Bitcoin Purity developers
 * Distributed under the MIT software license. */
#include "datum_embedded.h"

#include "datum_blocktemplates.h"
#include "datum_conf.h"
#include "datum_jsonrpc.h"
#include "datum_stratum.h"
#include "datum_coinbaser.h"
#include "datum_submitblock.h"
#include "datum_utils.h"

#include <curl/curl.h>
#include <errno.h>
#include <limits.h>
#include "datum_thread.h"
#include "datum_net.h"
#include <sodium.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

global_config_t datum_config;
const char* datum_gateway_config_filename = NULL;
const char* const* datum_argv = NULL;

static atomic_bool g_stop = true;
static atomic_bool g_running = false;
static atomic_uint_fast64_t g_share_difficulty;
static pthread_t g_template_thread;
static pthread_t g_stratum_thread;
static bool g_template_started;
static bool g_stratum_started;
static bool g_coinbaser_started;
static bool g_curl_initialized;
static bool g_net_initialized;

static bool copy_string(char* dst, size_t dst_size, const char* src,
    const char* name, char* error, size_t error_size)
{
    if (!src) src = "";
    if (strlen(src) >= dst_size) {
        snprintf(error, error_size, "%s is too long", name);
        return false;
    }
    memcpy(dst, src, strlen(src) + 1);
    return true;
}

bool datum_embedded_should_stop(void)
{
    return atomic_load_explicit(&g_stop, memory_order_relaxed);
}

void datum_embedded_request_stop(void)
{
    atomic_store_explicit(&g_stop, true, memory_order_relaxed);
}

int datum_embedded_start(const datum_embedded_config* config, char* error, size_t error_size)
{
    if (!config || !error || error_size == 0) return -1;
    error[0] = '\0';
    if (atomic_load_explicit(&g_running, memory_order_acquire)) return 0;
    if (config->listen_port == 0 || config->max_clients == 0 ||
        config->max_clients > 4096 || config->max_clients_per_ip == 0 ||
        config->max_clients_per_ip > config->max_clients ||
        config->share_difficulty == 0 || config->share_difficulty > INT_MAX ||
        config->payout_script_len == 0 || config->payout_script_len > sizeof(datum_config.mining_pool_script)) {
        snprintf(error, error_size, "invalid embedded DATUM numeric or payout configuration");
        return -1;
    }
    if (config->auth_required &&
        ((!config->auth_user || !config->auth_user[0]) ||
         (!config->auth_password || !config->auth_password[0]))) {
        snprintf(error, error_size, "DATUM authentication requires a username and password");
        return -1;
    }

    memset(&datum_config, 0, sizeof(datum_config));
    if (!copy_string(datum_config.stratum_v1_listen_addr, sizeof(datum_config.stratum_v1_listen_addr), config->listen_address, "datumlisten", error, error_size) ||
        !copy_string(datum_config.stratum_v1_auth_user, sizeof(datum_config.stratum_v1_auth_user), config->auth_user, "datumuser", error, error_size) ||
        !copy_string(datum_config.stratum_v1_auth_password, sizeof(datum_config.stratum_v1_auth_password), config->auth_password, "datumpassword", error, error_size) ||
        !copy_string(datum_config.bitcoind_rpcurl, sizeof(datum_config.bitcoind_rpcurl), config->rpc_url, "datumrpcurl", error, error_size) ||
        !copy_string(datum_config.bitcoind_rpcuser, sizeof(datum_config.bitcoind_rpcuser), config->rpc_user, "datumrpcuser", error, error_size) ||
        !copy_string(datum_config.bitcoind_rpcpassword, sizeof(datum_config.bitcoind_rpcpassword), config->rpc_password, "datumrpcpassword", error, error_size) ||
        !copy_string(datum_config.bitcoind_rpccookiefile, sizeof(datum_config.bitcoind_rpccookiefile), config->rpc_cookie_file, "rpccookiefile", error, error_size) ||
        !copy_string(datum_config.mining_pool_address, sizeof(datum_config.mining_pool_address), config->payout_address, "datumaddress", error, error_size) ||
        !copy_string(datum_config.mining_coinbase_tag_primary, sizeof(datum_config.mining_coinbase_tag_primary), config->coinbase_tag, "datumcoinbasetag", error, error_size)) {
        return -1;
    }

    datum_config.stratum_v1_listen_port = config->listen_port;
    datum_config.stratum_v1_max_clients = (int)config->max_clients;
    datum_config.stratum_v1_max_clients_per_ip = (int)config->max_clients_per_ip;
    datum_config.stratum_v1_max_threads = config->max_clients < 4 ? (int)config->max_clients : 4;
    datum_config.stratum_v1_max_clients_per_thread =
        ((int)config->max_clients + datum_config.stratum_v1_max_threads - 1) /
        datum_config.stratum_v1_max_threads;
    datum_config.stratum_v1_trust_proxy = -1;
    datum_config.stratum_v1_auth_required = config->auth_required;
    datum_config.stratum_v1_auth_timeout_seconds = (int)config->auth_timeout_seconds;
    datum_config.stratum_v1_fixed_difficulty = true;
    datum_config.stratum_v1_vardiff_min = (int)config->share_difficulty;
    datum_config.stratum_v1_vardiff_target_shares_min = 8;
    datum_config.stratum_v1_vardiff_quickdiff_count = 4;
    datum_config.stratum_v1_vardiff_quickdiff_delta = 4;
    datum_config.stratum_v1_share_stale_seconds = 150;
    datum_config.stratum_v1_idle_timeout_no_subscribe = 15;
    datum_config.stratum_v1_idle_timeout_no_share = 900;
    datum_config.stratum_v1_idle_timeout_max_last_work = 7200;
    datum_config.stratum_v1_fingerprint_miners = true;
    datum_config.bitcoind_work_update_seconds = 30;
    datum_config.bitcoind_notify_fallback = true;
    datum_config.datum_pooled_mining_only = false;
    memcpy(datum_config.mining_pool_script, config->payout_script, config->payout_script_len);
    datum_config.mining_pool_script_len = (int)config->payout_script_len;
    update_rpc_auth(&datum_config);
    if (datum_net_init() != 0) {
        snprintf(error, error_size, "failed to initialize DATUM network support");
        return -1;
    }
    g_net_initialized = true;
    if (sodium_init() < 0 || curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        snprintf(error, error_size, "failed to initialize DATUM cryptographic or RPC support");
        datum_embedded_stop();
        return -1;
    }
    g_curl_initialized = true;
    datum_utils_init();
    atomic_store_explicit(&g_stop, false, memory_order_release);
    atomic_store_explicit(&g_share_difficulty, config->share_difficulty, memory_order_release);

    if (datum_coinbaser_init() != 0) {
        snprintf(error, error_size, "failed to start DATUM coinbase worker");
        datum_embedded_request_stop();
        datum_embedded_stop();
        return -1;
    }
    g_coinbaser_started = true;
    if (pthread_create(&g_template_thread, NULL, datum_gateway_template_thread, NULL) != 0) {
        snprintf(error, error_size, "failed to start DATUM template worker");
        datum_embedded_stop();
        return -1;
    }
    g_template_started = true;
    if (pthread_create(&g_stratum_thread, NULL, datum_stratum_v1_socket_server, NULL) != 0) {
        snprintf(error, error_size, "failed to start DATUM Stratum worker");
        datum_embedded_stop();
        return -1;
    }
    g_stratum_started = true;
    atomic_store_explicit(&g_running, true, memory_order_release);
    return 0;
}

void datum_embedded_interrupt(void)
{
    datum_embedded_request_stop();
    datum_stratum_v1_shutdown_all();
    datum_submitblock_interrupt();
}

void datum_embedded_stop(void)
{
    atomic_store_explicit(&g_running, false, memory_order_release);
    datum_embedded_interrupt();
    if (g_stratum_started) {
        pthread_join(g_stratum_thread, NULL);
        g_stratum_started = false;
    }
    if (g_template_started) {
        pthread_join(g_template_thread, NULL);
        g_template_started = false;
    }
    if (g_coinbaser_started) {
        datum_coinbaser_stop();
        g_coinbaser_started = false;
    }
    datum_submitblock_stop();
    if (g_curl_initialized) {
        curl_global_cleanup();
        g_curl_initialized = false;
    }
    if (g_net_initialized) {
        datum_net_cleanup();
        g_net_initialized = false;
    }
    sodium_memzero(&datum_config, sizeof(datum_config));
    atomic_store_explicit(&g_share_difficulty, 0, memory_order_release);
}

void datum_request_template_refresh(void)
{
    if (!datum_embedded_should_stop()) datum_blocktemplates_notifynew(NULL, 0);
}

void datum_embedded_get_stats(datum_embedded_stats* stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    stats->running = atomic_load_explicit(&g_running, memory_order_acquire);
    datum_stratum_v1_get_stats(&stats->clients, &stats->authorized_clients,
        &stats->accepted_shares, &stats->rejected_shares, &stats->current_height);
}

uint64_t datum_embedded_get_share_difficulty(void)
{
    return atomic_load_explicit(&g_share_difficulty, memory_order_acquire);
}

int datum_embedded_update_share_difficulty(uint64_t difficulty, char* error, size_t error_size)
{
    if (!error || error_size == 0) return -1;
    error[0] = '\0';
    if (difficulty == 0 || difficulty > INT_MAX) {
        snprintf(error, error_size, "share difficulty must be between 1 and %d", INT_MAX);
        return -1;
    }
    if (!atomic_load_explicit(&g_running, memory_order_acquire) || datum_embedded_should_stop()) {
        snprintf(error, error_size, "DATUM is not running");
        return -1;
    }
    atomic_store_explicit(&g_share_difficulty, difficulty, memory_order_release);
    return 0;
}

void datum_print_banner(void) {}

const T_DATUM_CONFIG_ITEM* datum_config_get_option_info(const char* category, size_t category_len, const char* name, size_t name_len)
{ (void)category; (void)category_len; (void)name; (void)name_len; return NULL; }
const T_DATUM_CONFIG_ITEM* datum_config_get_option_info2(const char* category, const char* name)
{ (void)category; (void)name; return NULL; }
int datum_config_parse_username_mods(struct datum_username_mod** mods, json_t* item, bool log_errors)
{ (void)mods; (void)item; (void)log_errors; return 0; }
struct datum_username_mod* datum_username_mods_next(struct datum_username_mod* prev)
{ (void)prev; return NULL; }
struct datum_username_mod* datum_username_mods_find(struct datum_username_mod* mods, const char* name, size_t name_len)
{ (void)mods; (void)name; (void)name_len; return NULL; }
