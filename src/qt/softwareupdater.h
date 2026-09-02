// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_SOFTWAREUPDATER_H
#define BITCOIN_QT_SOFTWAREUPDATER_H

#include <kernel/software_updates.h>

#include <QObject>

class QWidget;

/** Background checker that fetches the signed releases manifest. */
class SoftwareUpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit SoftwareUpdateChecker(QObject* parent = nullptr);

    void scheduleStartupCheck();
    void checkNow(bool manual);

Q_SIGNALS:
    void updateAvailable(const SoftwareReleaseInfo& release, const SoftwareReleaseArtifact& artifact);
    void noUpdateAvailable(bool manual);
    void checkFailed(const QString& error, bool manual);

private:
    void performCheck(bool manual);
    bool shouldCheckNow() const;
    void recordCheckAttempt();

    bool m_check_in_progress{false};
};

/** Prompt the user to download and verify a newer release archive. */
class SoftwareUpdatePresenter
{
public:
  SoftwareUpdatePresenter() = delete;

  static void showUpdateAvailable(
      QWidget* parent,
      const SoftwareReleaseInfo& release,
      const SoftwareReleaseArtifact& artifact);

  static void showNoUpdate(QWidget* parent);
  static void showCheckFailed(QWidget* parent, const QString& error);
};

#endif // BITCOIN_QT_SOFTWAREUPDATER_H
