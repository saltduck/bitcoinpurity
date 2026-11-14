// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>
#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <common/args.h>
#include <mining/sv2_bridge.h>
#include <qt/sv2settings.h>
#include <util/string.h>
#include <util/translation.h>
Sv2SettingsWidget::Sv2SettingsWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* notice = new QLabel(tr("Connect Stratum V1 miners to a Purity SV2 pool using this node's transactions. The pool must support Full-Template Job Declaration. Mode and configuration changes require restart. Command-line and configuration-file settings retain precedence."), this);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    m_mode = new QComboBox(this);
    m_mode->setObjectName("miningMode");
    m_mode->addItem(tr("Disabled"), 0);
#ifdef ENABLE_DATUM
    m_mode->addItem(tr("DATUM Solo"), 1);
#endif
    m_mode->addItem(tr("SV2 pool"), 2);
    form->addRow(tr("Mining mode:"), m_mode);
    m_auth = new QCheckBox(tr("Authenticate local miners"), this);
    m_auth->setChecked(gArgs.GetBoolArg("-sv2auth", false));
    form->addRow(m_auth);
    for (const auto& [name, label] : {std::pair{"sv2listen", QT_TR_NOOP("Miner listen address:")}, {"sv2pool", QT_TR_NOOP("Mining endpoint (host:port):")}, {"sv2jds", QT_TR_NOOP("Job Declaration endpoint (host:port):")}, {"sv2poolkey", QT_TR_NOOP("Mining authority public key:")}, {"sv2jdskey", QT_TR_NOOP("JDS authority public key:")}, {"sv2identity", QT_TR_NOOP("Pool account identity:")}, {"sv2pooltag", QT_TR_NOOP("Pool coinbase tag (optional):")}, {"sv2minertag", QT_TR_NOOP("Miner coinbase tag (optional):")}, {"sv2user", QT_TR_NOOP("Local miner username:")}, {"sv2password", QT_TR_NOOP("Local miner password:")}}) {
        auto* input = new QLineEdit(this);
        input->setObjectName(name);
        input->setText(QString::fromStdString(gArgs.GetArg("-" + std::string{name}, std::string{name} == "sv2listen" ? "127.0.0.1" : "")));
        if (std::string{name} == "sv2password") input->setEchoMode(QLineEdit::Password);
        m_fields.emplace(name, input);
        form->addRow(tr(label), input);
    }
    m_job_interval = new QSpinBox(this);
    m_job_interval->setObjectName("sv2jobinterval");
    m_job_interval->setRange(30, 60);
    m_job_interval->setValue(gArgs.GetIntArg("-sv2jobinterval", 30));
    form->addRow(tr("Job refresh interval (seconds):"), m_job_interval);
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(gArgs.GetIntArg("-sv2port", 23334));
    form->addRow(tr("Miner port:"), m_port);
    m_clients = new QSpinBox(this);
    m_clients->setRange(1, 4096);
    m_clients->setValue(gArgs.GetIntArg("-sv2maxclients", 32));
    form->addRow(tr("Maximum miners:"), m_clients);
    m_per_ip = new QSpinBox(this);
    m_per_ip->setRange(1, 4096);
    m_per_ip->setValue(gArgs.GetIntArg("-sv2maxperip", 4));
    form->addRow(tr("Maximum miners per IP:"), m_per_ip);
    SetMode(gArgs.GetBoolArg("-sv2", false) ? 2 : gArgs.GetBoolArg("-datum", false) ? 1 :
                                                                                      0);
    layout->addStretch();
    m_initial = Values();
}
int Sv2SettingsWidget::Mode() const { return m_mode->currentData().toInt(); }
void Sv2SettingsWidget::SetMode(int mode) { m_mode->setCurrentIndex(m_mode->findData(mode)); }
std::map<std::string, std::string> Sv2SettingsWidget::Values() const
{
    std::map<std::string, std::string> values;
    for (const auto& [name, input] : m_fields)
        values.emplace(name, input->text().toStdString());
    values.emplace("sv2", Mode() == 2 ? "1" : "0");
#ifdef ENABLE_DATUM
    values.emplace("datum", Mode() == 1 ? "1" : "0");
#endif
    values.emplace("sv2auth", m_auth->isChecked() ? "1" : "0");
    values.emplace("sv2jobinterval", util::ToString(m_job_interval->value()));
    values.emplace("sv2port", util::ToString(m_port->value()));
    values.emplace("sv2maxclients", util::ToString(m_clients->value()));
    values.emplace("sv2maxperip", util::ToString(m_per_ip->value()));
    return values;
}
bool Sv2SettingsWidget::Validate(QString& error) const
{
    error.clear();
    const auto values = Values();
    for (const auto& [name, value] : values) {
        const auto text = QString::fromStdString(value);
        if (value == m_initial.at(name)) continue;
        if (text.trimmed() != text || value.find_first_of("\r\n#") != std::string::npos) {
            error = tr("Values cannot contain leading or trailing whitespace, newlines or the configuration comment character (#).");
            return false;
        }
    }
    if (Mode() != 2) return true;
    ArgsManager staged;
    mining::SetupSv2Args(staged);
    for (const auto& [name, value] : values)
        if (name != "datum") staged.ForceSetArg("-" + name, value);
    bilingual_str configuration_error;
    if ((gArgs.GetChainType() != ChainType::MAIN && gArgs.GetChainType() != ChainType::REGTEST) ||
        !mining::ValidateSv2Options(staged, configuration_error)) {
        error = tr("SV2 requires mainnet or regtest, valid Mining/JDS endpoints and authority keys, a pool identity, and consistent authentication and connection limits.");
        return false;
    }
    return true;
}
bool Sv2SettingsWidget::Save()
{
    const auto values = Values();
    std::map<std::string, std::string> updates;
    for (const auto& [name, value] : values) {
        if (value != m_initial.at(name)) updates.emplace(name, value);
    }
    if (updates.empty()) return false;
    gArgs.ModifyRWConfigFile(updates, /*also_settings_json=*/false);
    m_initial = values;
    return true;
}
