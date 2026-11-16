// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#ifndef BITCOIN_QT_SV2SETTINGS_H
#define BITCOIN_QT_SV2SETTINGS_H
#include <QWidget>
#include <map>
#include <string>
class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class Sv2SettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit Sv2SettingsWidget(QWidget* parent = nullptr);
    QComboBox* ModeControl() const { return m_mode; }
    int Mode() const;
    void SetMode(int);
    bool Validate(QString& error) const;
    bool Save();

private:
    std::map<std::string, std::string> Values() const;
    QComboBox* m_mode;
    QCheckBox* m_auth;
    QSpinBox* m_port;
    QSpinBox* m_job_interval;
    QSpinBox* m_clients;
    QSpinBox* m_per_ip;
    std::map<std::string, QLineEdit*> m_fields;
    std::map<std::string, std::string> m_initial;
};
#endif // BITCOIN_QT_SV2SETTINGS_H
