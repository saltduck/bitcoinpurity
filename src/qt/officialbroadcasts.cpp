// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qt/officialbroadcasts.h>

#include <clientversion.h>
#include <common/args.h>
#include <kernel/official_broadcasts.h>
#include <logging.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include <optional>
#include <string>
#include <vector>

namespace {

static constexpr qint64 STARTUP_CHECK_DELAY_MS{45'000};
static constexpr qint64 MANIFEST_FETCH_TIMEOUT_MS{30'000};
static constexpr qint64 CHECK_INTERVAL_SECONDS{24 * 60 * 60};

static constexpr const char* SETTINGS_GROUP{"official_broadcasts"};
static constexpr const char* SETTINGS_LAST_CHECK{"last_check_epoch"};
static constexpr const char* SETTINGS_DISMISSED_IDS{"dismissed_ids"};
static constexpr const char* SETTINGS_ENABLED{"fShowOfficialNotices"};

bool BroadcastsEnabled()
{
    if (!gArgs.GetBoolArg("-checkforbroadcasts", DEFAULT_CHECKFORBROADCASTS)) {
        return false;
    }
    QSettings settings;
    return settings.value(SETTINGS_ENABLED, true).toBool();
}

QString LocalVersionString()
{
    return QString::fromStdString(FormatFullVersion());
}

void ConfigureBroadcastRequest(QNetworkRequest& request)
{
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("%1/%2").arg(CLIENT_NAME, LocalVersionString()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setRawHeader("Cache-Control", "no-cache");
}

std::optional<int> HttpStatusCode(const QNetworkReply* reply)
{
    if (!reply) return std::nullopt;
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!status.isValid()) return std::nullopt;
    return status.toInt();
}

bool HttpStatusOk(const QNetworkReply* reply)
{
    const auto status = HttpStatusCode(reply);
    return status && *status >= 200 && *status < 300;
}

enum class ManifestFetchStatus {
    Ok,
    NotFound, //!< HTTP 404: treat as no published notices.
    Failed,
};

struct ManifestFetchResult {
    ManifestFetchStatus status{ManifestFetchStatus::Failed};
    std::string body;
    QString error;
};

ManifestFetchResult FetchManifestJson(const QUrl& url)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    ConfigureBroadcastRequest(request);

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(MANIFEST_FETCH_TIMEOUT_MS);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();

    auto finish = [&](ManifestFetchResult result) {
        reply->deleteLater();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        return result;
    };

    if (!reply->isFinished()) {
        reply->abort();
        return finish({ManifestFetchStatus::Failed, {}, QObject::tr("Timed out loading official notices.")});
    }

    const auto status = HttpStatusCode(reply);
    if ((status && *status == 404) || reply->error() == QNetworkReply::ContentNotFoundError) {
        // Missing broadcasts.json means there are currently no official notices.
        return finish({ManifestFetchStatus::NotFound, {}, {}});
    }

    if (reply->error() != QNetworkReply::NoError) {
        return finish({ManifestFetchStatus::Failed, {}, reply->errorString()});
    }

    if (!HttpStatusOk(reply)) {
        return finish({ManifestFetchStatus::Failed, {},
            QObject::tr("Failed to load official notices (HTTP %1).").arg(status.value_or(0))});
    }

    const QByteArray body = reply->readAll();
    return finish({ManifestFetchStatus::Ok, std::string(body.constData(), body.size()), {}});
}

std::vector<std::string> LoadDismissedIds()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    const QStringList ids = settings.value(SETTINGS_DISMISSED_IDS).toStringList();
    settings.endGroup();

    std::vector<std::string> out;
    out.reserve(ids.size());
    for (const QString& id : ids) {
        out.push_back(id.toStdString());
    }
    return out;
}

QString NowIso8601Utc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

} // namespace

OfficialBroadcastChecker::OfficialBroadcastChecker(QObject* parent)
    : QObject(parent)
{
}

void OfficialBroadcastChecker::MarkNoticeRead(const QString& notice_id)
{
    if (notice_id.isEmpty()) return;

    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    QStringList ids = settings.value(SETTINGS_DISMISSED_IDS).toStringList();
    if (!ids.contains(notice_id)) {
        ids.append(notice_id);
        settings.setValue(SETTINGS_DISMISSED_IDS, ids);
    }
    settings.endGroup();
}

void OfficialBroadcastChecker::scheduleStartupCheck()
{
    if (!BroadcastsEnabled()) return;

    QTimer::singleShot(STARTUP_CHECK_DELAY_MS, this, [this] {
        checkNow(false);
    });
}

void OfficialBroadcastChecker::checkNow(bool manual)
{
    if (!BroadcastsEnabled()) {
        if (manual) {
            Q_EMIT checkFailed(tr("Official notices are disabled."), manual);
        }
        return;
    }

    if (m_check_in_progress) return;
    if (!manual && !shouldCheckNow()) return;

    m_check_in_progress = true;
    performCheck(manual);
}

bool OfficialBroadcastChecker::shouldCheckNow() const
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    const qint64 last_check = settings.value(SETTINGS_LAST_CHECK, 0).toLongLong();
    settings.endGroup();

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return last_check <= 0 || now - last_check >= CHECK_INTERVAL_SECONDS;
}

void OfficialBroadcastChecker::recordCheckAttempt()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue(SETTINGS_LAST_CHECK, QDateTime::currentSecsSinceEpoch());
    settings.endGroup();
}

void OfficialBroadcastChecker::performCheck(bool manual)
{
    const auto manifest_url = GetOfficialBroadcastsManifestUrl(gArgs);
    if (!manifest_url || manifest_url->empty()) {
        m_check_in_progress = false;
        Q_EMIT checkFailed(tr("No official notices URL is configured."), manual);
        return;
    }

    recordCheckAttempt();

    const ManifestFetchResult fetch = FetchManifestJson(QUrl(QString::fromStdString(*manifest_url)));
    if (fetch.status == ManifestFetchStatus::NotFound) {
        m_check_in_progress = false;
        Q_EMIT noNoticesAvailable(manual);
        return;
    }
    if (fetch.status != ManifestFetchStatus::Ok) {
        m_check_in_progress = false;
        LogPrintf("Official broadcast check failed for %s: %s\n", *manifest_url, fetch.error.toStdString());
        Q_EMIT checkFailed(fetch.error.isEmpty()
                ? tr("Could not load official notices from downloads.bitcoinpurity.org.")
                : fetch.error,
            manual);
        return;
    }

    const auto notices = ParseOfficialBroadcastsManifest(
        fetch.body, *manifest_url, OfficialBroadcastTrustPolicy::REMOTE_SIGNED);
    if (!notices) {
        m_check_in_progress = false;
        Q_EMIT checkFailed(tr("Official notices from downloads.bitcoinpurity.org could not be verified."), manual);
        return;
    }

    const auto unread = FilterUnreadOfficialBroadcasts(
        *notices, LoadDismissedIds(), NowIso8601Utc().toStdString());
    m_check_in_progress = false;

    if (unread.empty()) {
        Q_EMIT noNoticesAvailable(manual);
        return;
    }

    Q_EMIT noticesAvailable(unread);
}

void OfficialBroadcastPresenter::showNotices(QWidget* parent, const std::vector<OfficialBroadcastNotice>& notices)
{
    for (const auto& notice : notices) {
        QMessageBox box(parent);
        box.setWindowTitle(notice.title.empty()
                ? QObject::tr("Official Notice")
                : QString::fromStdString(notice.title));
        box.setIcon(QMessageBox::Information);
        box.setTextFormat(Qt::PlainText);
        box.setText(QString::fromStdString(notice.body));
        box.setStandardButtons(QMessageBox::Ok);
        box.setDefaultButton(QMessageBox::Ok);
        box.exec();
        OfficialBroadcastChecker::MarkNoticeRead(QString::fromStdString(notice.id));
    }
}

void OfficialBroadcastPresenter::showNoNotices(QWidget* parent)
{
    QMessageBox::information(
        parent,
        QObject::tr("Official Notices"),
        QObject::tr("There are no new official notices."));
}

void OfficialBroadcastPresenter::showCheckFailed(QWidget* parent, const QString& error)
{
    QMessageBox::warning(parent, QObject::tr("Official Notices"), error);
}
