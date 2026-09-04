// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_OFFICIALBROADCASTS_H
#define BITCOIN_QT_OFFICIALBROADCASTS_H

#include <kernel/official_broadcasts.h>

#include <QObject>

class QWidget;

/** Background checker that fetches the signed official broadcasts manifest. */
class OfficialBroadcastChecker : public QObject
{
    Q_OBJECT

public:
    explicit OfficialBroadcastChecker(QObject* parent = nullptr);

    void scheduleStartupCheck();
    void checkNow(bool manual);

    /** Persist that the user has read a notice so it is not shown again. */
    static void MarkNoticeRead(const QString& notice_id);

Q_SIGNALS:
    void noticesAvailable(const std::vector<OfficialBroadcastNotice>& notices);
    void noNoticesAvailable(bool manual);
    void checkFailed(const QString& error, bool manual);

private:
    void performCheck(bool manual);
    bool shouldCheckNow() const;
    void recordCheckAttempt();

    bool m_check_in_progress{false};
};

/** Prompt the user with unread official notices (plain text). */
class OfficialBroadcastPresenter
{
public:
    OfficialBroadcastPresenter() = delete;

    static void showNotices(QWidget* parent, const std::vector<OfficialBroadcastNotice>& notices);
    static void showNoNotices(QWidget* parent);
    static void showCheckFailed(QWidget* parent, const QString& error);
};

#endif // BITCOIN_QT_OFFICIALBROADCASTS_H
