// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#include <qt/miningpage.h>

#include <mining/datum_bridge.h>

#include <QAbstractItemView>
#include <QDateTime>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QScrollArea>
#include <QShowEvent>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace {
constexpr uint64_t HOUR_MS{60 * 60 * 1000};
constexpr uint64_t DAY_MS{24 * HOUR_MS};
constexpr uint64_t SAMPLE_INTERVAL_MS{60 * 1000};
constexpr int MAX_SAMPLES{1440};
constexpr double HASHES_PER_DIFFICULTY{4294967296.0};

QLabel* ValueLabel()
{
    auto* label = new QLabel(QStringLiteral("—"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

QString TimeText(uint64_t milliseconds)
{
    if (!milliseconds) return QStringLiteral("—");
    return QDateTime::fromMSecsSinceEpoch(milliseconds).toString(Qt::ISODate);
}

QString DurationText(uint64_t milliseconds)
{
    if (!milliseconds) return QStringLiteral("—");
    const uint64_t seconds = milliseconds / 1000;
    const uint64_t days = seconds / 86400;
    const uint64_t hours = (seconds / 3600) % 24;
    const uint64_t minutes = (seconds / 60) % 60;
    const uint64_t remaining = seconds % 60;
    if (days) return QStringLiteral("%1d %2h %3m").arg(days).arg(hours).arg(minutes);
    if (hours) return QStringLiteral("%1h %2m %3s").arg(hours).arg(minutes).arg(remaining);
    return QStringLiteral("%1m %2s").arg(minutes).arg(remaining);
}

QString AgeText(uint64_t milliseconds, uint64_t now)
{
    return milliseconds && now >= milliseconds ? DurationText(now - milliseconds) + QObject::tr(" ago") : QStringLiteral("—");
}

QString HashrateText(double ths)
{
    if (!std::isfinite(ths) || ths < 0) return QStringLiteral("—");
    if (ths >= 1000000) return QStringLiteral("%1 EH/s").arg(ths / 1000000, 0, 'f', 2);
    if (ths >= 1000) return QStringLiteral("%1 PH/s").arg(ths / 1000, 0, 'f', 2);
    if (ths >= 1) return QStringLiteral("%1 TH/s").arg(ths, 0, 'f', 2);
    return QStringLiteral("%1 GH/s").arg(ths * 1000, 0, 'f', 1);
}

QString ChanceText(double miner_ths, double network_difficulty)
{
    if (!std::isfinite(miner_ths) || !std::isfinite(network_difficulty) || miner_ths <= 0 || network_difficulty <= 0) {
        return QStringLiteral("—");
    }
    const double network_ths = network_difficulty * HASHES_PER_DIFFICULTY / 600.0 / 1.0e12;
    if (!std::isfinite(network_ths) || network_ths <= 0) return QStringLiteral("—");
    const double percent = std::min(1.0, miner_ths / network_ths) * 100.0;
    return percent >= 0.01 ? QStringLiteral("%1%").arg(percent, 0, 'f', 4)
                           : QStringLiteral("%1%").arg(percent, 0, 'g', 3);
}

void AddRow(QFormLayout* form, const QString& name, QLabel*& value)
{
    value = ValueLabel();
    form->addRow(name, value);
}

QFrame* MetricCard(const QString& title, QLabel*& value, const QString& detail, QLabel** detail_label = nullptr)
{
    auto* card = new QFrame;
    card->setFrameShape(QFrame::StyledPanel);
    card->setMinimumHeight(62);
    card->setMaximumHeight(72);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 5, 10, 5);
    layout->setSpacing(0);
    auto* heading = new QLabel(title);
    QFont heading_font = heading->font();
    heading_font.setPointSize(std::max(8, heading_font.pointSize() - 1));
    heading->setFont(heading_font);
    layout->addWidget(heading);
    value = ValueLabel();
    QFont value_font = value->font();
    value_font.setPointSize(value_font.pointSize() + 5);
    value->setFont(value_font);
    layout->addWidget(value);
    auto* description = new QLabel(detail);
    QFont description_font = description->font();
    description_font.setPointSize(std::max(8, description_font.pointSize() - 1));
    description->setFont(description_font);
    description->setWordWrap(true);
    description->setProperty("secondary", true);
    layout->addWidget(description);
    if (detail_label) *detail_label = description;
    return card;
}

void SetSummaryRow(QTableWidget* table, int row, const QString& metric, const QString& value, const QString& description)
{
    table->setItem(row, 0, new QTableWidgetItem(metric));
    table->setItem(row, 1, new QTableWidgetItem(value));
    table->setItem(row, 2, new QTableWidgetItem(description));
}
} // namespace

class HashrateGraphWidget final : public QWidget
{
public:
    explicit HashrateGraphWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(104);
        setMaximumHeight(126);
        setObjectName(QStringLiteral("miningHashrateGraph"));
    }

    void appendSample(uint64_t timestamp, double hashrate)
    {
        if (!m_samples.empty() && timestamp < m_samples.back().first + SAMPLE_INTERVAL_MS) return;
        m_samples.push_back({timestamp, std::max(0.0, hashrate)});
        const uint64_t cutoff = timestamp > DAY_MS ? timestamp - DAY_MS : 0;
        while (!m_samples.empty() && (m_samples.front().first < cutoff || static_cast<int>(m_samples.size()) > MAX_SAMPLES)) {
            m_samples.remove(0);
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());
        QColor grid_color = palette().text().color();
        grid_color.setAlpha(55);
        painter.setPen(grid_color);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        const QRectF plot = rect().adjusted(48, 10, -10, -23);
        for (int i = 1; i < 4; ++i) {
            const qreal x = plot.left() + plot.width() * i / 4.0;
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        for (int i = 1; i < 3; ++i) {
            const qreal y = plot.top() + plot.height() * i / 3.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
        painter.drawLine(plot.bottomLeft(), plot.topLeft());
        painter.setPen(palette().text().color());
        painter.drawText(QRectF(2, plot.top() - 2, 42, 18), Qt::AlignRight | Qt::AlignVCenter, tr("TH/s"));
        painter.drawText(QRectF(plot.left(), plot.bottom() + 2, 50, 18), Qt::AlignLeft, tr("-24h"));
        painter.drawText(QRectF(plot.right() - 50, plot.bottom() + 2, 50, 18), Qt::AlignRight, tr("Now"));

        if (m_samples.empty()) {
            painter.drawText(plot, Qt::AlignCenter, tr("Waiting for hashrate samples…"));
            return;
        }

        double maximum{0};
        for (const auto& sample : m_samples) maximum = std::max(maximum, sample.second);
        maximum = std::max(1.0, maximum);
        painter.drawText(QRectF(2, plot.top() + 16, 42, 18), Qt::AlignRight | Qt::AlignVCenter, QString::number(maximum, 'g', 3));

        const uint64_t now = QDateTime::currentMSecsSinceEpoch();
        const uint64_t start = now > DAY_MS ? now - DAY_MS : 0;
        QPainterPath path;
        bool first{true};
        for (const auto& sample : m_samples) {
            const double elapsed = sample.first > start ? static_cast<double>(sample.first - start) : 0.0;
            const double x = plot.left() + std::min(1.0, elapsed / DAY_MS) * plot.width();
            const double y = plot.bottom() - (sample.second / maximum) * plot.height();
            if (first) {
                path.moveTo(x, y);
                first = false;
            } else {
                path.lineTo(x, y);
            }
        }
        painter.setPen(QPen(palette().highlight().color(), 2));
        painter.drawPath(path);
        if (m_samples.size() == 1) painter.drawEllipse(path.currentPosition(), 2.5, 2.5);
    }

private:
    QVector<QPair<uint64_t, double>> m_samples;
};

MiningPage::MiningPage(QWidget* parent) : QWidget(parent), m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("miningPage"));
    auto* layout = new QVBoxLayout(this);
    m_warning = new QLabel;
    m_warning->setWordWrap(true);
    m_warning->setStyleSheet(QStringLiteral("QLabel { color: #b45309; font-weight: 600; padding: 6px; }"));
    m_warning->hide();
    layout->addWidget(m_warning);

    auto* tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("miningDetailTabs"));
    tabs->setDocumentMode(true);
    layout->addWidget(tabs);

    auto* dashboard_scroll = new QScrollArea;
    dashboard_scroll->setWidgetResizable(true);
    dashboard_scroll->setFrameShape(QFrame::NoFrame);
    dashboard_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* dashboard = new QWidget;
    auto* dashboard_layout = new QVBoxLayout(dashboard);
    dashboard_layout->setContentsMargins(8, 8, 8, 8);
    dashboard_layout->setSpacing(7);
    auto* title = new QLabel(tr("Bitcoin Purity Mining Console"));
    title->setAlignment(Qt::AlignCenter);
    QFont title_font = title->font();
    title_font.setPointSize(title_font.pointSize() + 7);
    title->setFont(title_font);
    dashboard_layout->addWidget(title);
    auto* status_line = new QHBoxLayout;
    status_line->addStretch();
    status_line->addWidget(new QLabel(tr("Runtime:")));
    m_dashboard_status = ValueLabel();
    m_dashboard_status->setWordWrap(false);
    QFont status_font = m_dashboard_status->font();
    status_font.setBold(true);
    m_dashboard_status->setFont(status_font);
    status_line->addWidget(m_dashboard_status);
    dashboard_layout->addLayout(status_line);

    auto* cards = new QGridLayout;
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setHorizontalSpacing(8);
    cards->setVerticalSpacing(6);
    cards->addWidget(MetricCard(tr("Estimated Miner Hashrate"), m_dashboard_hashrate, tr("Estimated from accepted shares")), 0, 0);
    cards->addWidget(MetricCard(tr("Chance per Block"), m_dashboard_chance, QString(), &m_dashboard_chance_detail), 0, 1);
    cards->addWidget(MetricCard(tr("Current Block Number"), m_dashboard_height, tr("Current DATUM job height")), 1, 0);
    cards->setColumnStretch(0, 1);
    cards->setColumnStretch(1, 1);
    dashboard_layout->addLayout(cards);

    auto* graph_group = new QGroupBox(tr("Hashrate Trend over Time (Last 24h)"));
    graph_group->setAlignment(Qt::AlignHCenter);
    auto* graph_layout = new QVBoxLayout(graph_group);
    graph_layout->setContentsMargins(8, 7, 8, 7);
    m_graph = new HashrateGraphWidget;
    graph_layout->addWidget(m_graph);
    dashboard_layout->addWidget(graph_group);

    auto* summary_group = new QGroupBox(tr("Share Performance Summary"));
    summary_group->setAlignment(Qt::AlignHCenter);
    auto* summary_layout = new QVBoxLayout(summary_group);
    summary_layout->setContentsMargins(8, 7, 8, 7);
    m_summary = new QTableWidget(5, 3);
    m_summary->setObjectName(QStringLiteral("miningShareSummary"));
    m_summary->setHorizontalHeaderLabels({tr("Metric"), tr("Value"), tr("Description")});
    m_summary->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_summary->setSelectionMode(QAbstractItemView::NoSelection);
    m_summary->setAlternatingRowColors(true);
    m_summary->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_summary->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_summary->verticalHeader()->hide();
    m_summary->verticalHeader()->setDefaultSectionSize(22);
    m_summary->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_summary->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_summary->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_summary->setFixedHeight(142);
    summary_layout->addWidget(m_summary);
    dashboard_layout->addWidget(summary_group);
    dashboard_layout->addStretch();
    dashboard_scroll->setWidget(dashboard);
    tabs->addTab(dashboard_scroll, tr("Mining Dashboard"));

    auto* overview = new QWidget;
    auto* overview_layout = new QVBoxLayout(overview);
    auto* runtime_group = new QGroupBox(tr("Runtime and Network"));
    auto* runtime = new QFormLayout(runtime_group);
    AddRow(runtime, tr("Status"), m_status);
    AddRow(runtime, tr("DATUM uptime"), m_uptime);
    AddRow(runtime, tr("Stratum listener"), m_listen);
    AddRow(runtime, tr("Authentication"), m_auth);
    AddRow(runtime, tr("Port mapping"), m_mapping);
    AddRow(runtime, tr("Payout address"), m_payout);
    overview_layout->addWidget(runtime_group);

    auto* mining_group = new QGroupBox(tr("Mining Overview"));
    auto* mining = new QFormLayout(mining_group);
    AddRow(mining, tr("Share difficulty"), m_difficulty);
    AddRow(mining, tr("Miners"), m_clients);
    AddRow(mining, tr("Estimated hashrate"), m_hashrate);
    AddRow(mining, tr("Session shares"), m_shares);
    AddRow(mining, tr("Last accepted share"), m_last_share);
    overview_layout->addWidget(mining_group);
    overview_layout->addStretch();
    tabs->addTab(overview, tr("Overview"));

    auto* miners_page = new QWidget;
    auto* miners_layout = new QVBoxLayout(miners_page);
    m_miners = new QTableWidget;
    m_miners->setColumnCount(11);
    m_miners->setHorizontalHeaderLabels({tr("Worker"), tr("Remote IP"), tr("Miner"), tr("Status"), tr("Connected"), tr("Difficulty"), tr("Est. hashrate"), tr("Accepted"), tr("Rejected"), tr("Reject rate"), tr("Last share")});
    m_miners->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_miners->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_miners->setAlternatingRowColors(true);
    m_miners->verticalHeader()->hide();
    m_miners->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_miners->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    miners_layout->addWidget(m_miners);
    tabs->addTab(miners_page, tr("Miners"));

    auto* job_page = new QWidget;
    auto* job_layout = new QVBoxLayout(job_page);
    auto* job_group = new QGroupBox(tr("Current Job"));
    auto* job = new QFormLayout(job_group);
    AddRow(job, tr("Height"), m_height);
    AddRow(job, tr("Job ID"), m_job_id);
    AddRow(job, tr("Job age"), m_job_age);
    AddRow(job, tr("Previous block hash"), m_prev_hash);
    AddRow(job, tr("nBits"), m_nbits);
    AddRow(job, tr("Network difficulty"), m_network_difficulty);
    AddRow(job, tr("Template"), m_template);
    AddRow(job, tr("Coinbase value"), m_coinbase);
    AddRow(job, tr("Last template result"), m_template_result);
    job_layout->addWidget(job_group);

    auto* diagnostics_group = new QGroupBox(tr("Block Submission and Diagnostics"));
    auto* diagnostics = new QFormLayout(diagnostics_group);
    AddRow(diagnostics, tr("Block candidates"), m_candidates);
    AddRow(diagnostics, tr("Last block result"), m_block_result);
    AddRow(diagnostics, tr("Last share rejection"), m_rejection);
    job_layout->addWidget(diagnostics_group);
    job_layout->addStretch();
    tabs->addTab(job_page, tr("Job and Diagnostics"));

    connect(m_timer, &QTimer::timeout, this, [this] { refresh(); });
    m_timer->setInterval(1000);
}

void MiningPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
    m_timer->start();
}

void MiningPage::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    QWidget::hideEvent(event);
}

void MiningPage::refresh()
{
    const mining::DatumStatusSnapshot status{mining::GetDatumStatusSnapshot()};
    const uint64_t now = QDateTime::currentMSecsSinceEpoch();
    m_dashboard_status->setText(QString::fromStdString(status.status));
    m_dashboard_hashrate->setText(status.running ? HashrateText(status.estimated_hashrate_ths) : QStringLiteral("—"));
    m_dashboard_height->setText(status.current_height ? QString::number(status.current_height) : QStringLiteral("—"));
    m_dashboard_chance->setText(ChanceText(status.estimated_hashrate_ths, status.network_difficulty));
    const double network_ths = status.network_difficulty > 0 ? status.network_difficulty * HASHES_PER_DIFFICULTY / 600.0 / 1.0e12 : 0;
    m_dashboard_chance_detail->setText(network_ths > 0 ? tr("Estimated network hashrate: %1").arg(HashrateText(network_ths)) : tr("Network difficulty unavailable"));
    if (!m_last_sample_ms || now >= m_last_sample_ms + SAMPLE_INTERVAL_MS) {
        m_graph->appendSample(now, status.running ? status.estimated_hashrate_ths : 0);
        m_last_sample_ms = now;
    }

    const uint64_t other_rejections = status.rejected_unknown_work + status.rejected_high_hash + status.rejected_duplicate + status.rejected_other;
    SetSummaryRow(m_summary, 0, tr("Accepted"), QString::number(status.session_accepted_shares), tr("Session accepted valid shares"));
    SetSummaryRow(m_summary, 1, tr("Rejected"), QString::number(status.session_rejected_shares), tr("Session rejected shares"));
    SetSummaryRow(m_summary, 2, tr("Stale / obsolete"), QString::number(status.rejected_stale), tr("Rejected stale work"));
    SetSummaryRow(m_summary, 3, tr("Other rejections"), QString::number(other_rejections), tr("Unknown work, high hash, duplicate, and other"));
    SetSummaryRow(m_summary, 4, tr("Block candidates"), tr("%1 accepted / %2 rejected").arg(status.block_submissions_accepted).arg(status.block_submissions_rejected), tr("%1 candidates submitted").arg(status.block_candidates));

    m_status->setText(QString::fromStdString(status.status));
    m_uptime->setText(status.running && status.session_started_ms ? DurationText(now - status.session_started_ms) : QStringLiteral("—"));
    m_listen->setText(QStringLiteral("%1:%2").arg(QString::fromStdString(status.listen)).arg(status.port));
    m_auth->setText(status.auth_required ? tr("Required") : tr("Disabled"));
    QString mapping = status.mapping_requested ? (status.mapping_active ? tr("Active") : tr("Not active")) : tr("Not requested");
    if (!status.mapping_protocol.empty()) mapping += QStringLiteral(" · ") + QString::fromStdString(status.mapping_protocol);
    if (!status.mapping_external.empty()) mapping += QStringLiteral(" · ") + QString::fromStdString(status.mapping_external);
    if (status.mapping_lifetime) mapping += tr(" · lifetime %1s").arg(status.mapping_lifetime);
    if (!status.mapping_error.empty()) mapping += QStringLiteral(" · ") + QString::fromStdString(status.mapping_error);
    m_mapping->setText(mapping);
    m_payout->setText(status.payout_address.empty() ? QStringLiteral("—") : QString::fromStdString(status.payout_address));

    const bool public_listener = status.listen == "0.0.0.0" || status.listen == "::";
    if (status.enabled && public_listener && !status.auth_required) {
        m_warning->setText(tr("Warning: Stratum is listening publicly without authentication."));
        m_warning->show();
    } else {
        m_warning->hide();
    }

    m_difficulty->setText(QString::number(status.share_difficulty));
    m_clients->setText(tr("%1 connected · %2 subscribed · %3 authorized").arg(status.clients).arg(status.subscribed_clients).arg(status.authorized_clients));
    m_hashrate->setText(HashrateText(status.estimated_hashrate_ths) + tr(" (estimated from accepted shares)"));
    const uint64_t total = status.session_accepted_shares + status.session_rejected_shares;
    const double reject_rate = total ? 100.0 * status.session_rejected_shares / total : 0.0;
    m_shares->setText(tr("%1 accepted · %2 rejected · %3% rejected").arg(status.session_accepted_shares).arg(status.session_rejected_shares).arg(reject_rate, 0, 'f', 2));
    m_last_share->setText(status.last_share_time_ms ? TimeText(status.last_share_time_ms) + QStringLiteral(" · ") + AgeText(status.last_share_time_ms, now) : QStringLiteral("—"));

    m_height->setText(status.current_height ? QString::number(status.current_height) : QStringLiteral("—"));
    m_job_id->setText(status.job_id.empty() ? QStringLiteral("—") : QString::fromStdString(status.job_id));
    m_job_age->setText(AgeText(status.job_created_ms, now));
    m_prev_hash->setText(status.previous_block_hash.empty() ? QStringLiteral("—") : QString::fromStdString(status.previous_block_hash));
    m_nbits->setText(status.nbits ? QStringLiteral("%1").arg(status.nbits, 8, 16, QLatin1Char('0')) : QStringLiteral("—"));
    m_network_difficulty->setText(status.network_difficulty ? QString::number(status.network_difficulty, 'g', 12) : QStringLiteral("—"));
    m_template->setText(status.job_id.empty() ? QStringLiteral("—") : tr("%1 transactions · %2 bytes · %3 weight").arg(status.transaction_count).arg(status.template_size).arg(status.template_weight));
    m_coinbase->setText(status.job_id.empty() ? QStringLiteral("—") : tr("%1 sat").arg(status.coinbase_value));
    QString template_result = status.last_template_update_ms ? TimeText(status.last_template_update_ms) + QStringLiteral(" · ") + (status.last_template_success ? tr("success") : tr("error")) : QStringLiteral("—");
    if (!status.last_template_error.empty()) template_result += QStringLiteral(" · ") + QString::fromStdString(status.last_template_error);
    m_template_result->setText(template_result);
    m_candidates->setText(tr("%1 candidates · %2 accepted · %3 rejected").arg(status.block_candidates).arg(status.block_submissions_accepted).arg(status.block_submissions_rejected));
    QString block_result = status.last_block_time_ms ? TimeText(status.last_block_time_ms) : QStringLiteral("—");
    if (!status.last_block_hash.empty()) block_result += QStringLiteral(" · ") + QString::fromStdString(status.last_block_hash);
    if (!status.last_block_result.empty()) block_result += QStringLiteral(" · ") + QString::fromStdString(status.last_block_result);
    m_block_result->setText(block_result);
    QString rejection = status.last_rejected_share_time_ms ? TimeText(status.last_rejected_share_time_ms) : QStringLiteral("—");
    if (!status.last_rejected_share_reason.empty()) rejection += QStringLiteral(" · ") + QString::fromStdString(status.last_rejected_share_reason);
    const uint64_t rejection_count = status.rejected_unknown_work + status.rejected_high_hash + status.rejected_stale + status.rejected_duplicate + status.rejected_other;
    if (rejection_count) rejection += tr(" · unknown %1, high hash %2, stale %3, duplicate %4, other %5")
        .arg(status.rejected_unknown_work).arg(status.rejected_high_hash).arg(status.rejected_stale).arg(status.rejected_duplicate).arg(status.rejected_other);
    m_rejection->setText(rejection);

    m_miners->setRowCount(status.miners.size());
    for (int row = 0; row < static_cast<int>(status.miners.size()); ++row) {
        const auto& miner = status.miners[row];
        const uint64_t miner_total = miner.accepted_shares + miner.rejected_shares;
        const double miner_reject_rate = miner_total ? 100.0 * miner.rejected_shares / miner_total : 0.0;
        const QString state = miner.authorized ? tr("Authorized") : miner.subscribed ? tr("Subscribed") : tr("Connected");
        const QStringList values{
            miner.worker.empty() ? QStringLiteral("—") : QString::fromStdString(miner.worker),
            QString::fromStdString(miner.remote_host), QString::fromStdString(miner.user_agent), state,
            miner.connected_since_ms ? DurationText(now - miner.connected_since_ms) : QStringLiteral("—"),
            QString::number(miner.current_difficulty), HashrateText(miner.estimated_hashrate_ths),
            QString::number(miner.accepted_shares), QString::number(miner.rejected_shares),
            QStringLiteral("%1%").arg(miner_reject_rate, 0, 'f', 2), AgeText(miner.last_share_time_ms, now)};
        for (int column = 0; column < values.size(); ++column) {
            m_miners->setItem(row, column, new QTableWidgetItem(values[column]));
        }
    }
}
