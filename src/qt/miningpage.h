// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#ifndef BITCOIN_QT_MININGPAGE_H
#define BITCOIN_QT_MININGPAGE_H

#include <QWidget>

#include <cstdint>

class HashrateGraphWidget;
class QLabel;
class QHideEvent;
class QShowEvent;
class QTableWidget;
class QTimer;

class MiningPage final : public QWidget
{
public:
    explicit MiningPage(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refresh();

    QTimer* m_timer;
    uint64_t m_last_sample_ms{0};
    QLabel* m_warning;
    QLabel* m_dashboard_status;
    QLabel* m_dashboard_hashrate;
    QLabel* m_dashboard_height;
    QLabel* m_dashboard_chance;
    QLabel* m_dashboard_chance_detail;
    HashrateGraphWidget* m_graph;
    QTableWidget* m_summary;
    QLabel* m_status;
    QLabel* m_uptime;
    QLabel* m_listen;
    QLabel* m_auth;
    QLabel* m_mapping;
    QLabel* m_payout;
    QLabel* m_difficulty;
    QLabel* m_clients;
    QLabel* m_hashrate;
    QLabel* m_shares;
    QLabel* m_last_share;
    QLabel* m_height;
    QLabel* m_job_id;
    QLabel* m_job_age;
    QLabel* m_prev_hash;
    QLabel* m_nbits;
    QLabel* m_network_difficulty;
    QLabel* m_template;
    QLabel* m_coinbase;
    QLabel* m_template_result;
    QLabel* m_candidates;
    QLabel* m_block_result;
    QLabel* m_rejection;
    QTableWidget* m_miners;
};

#endif // BITCOIN_QT_MININGPAGE_H
