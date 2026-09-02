// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qt/softwareupdater.h>

#include <qt/guiutil.h>

#include <clientversion.h>
#include <common/args.h>
#include <crypto/sha256.h>
#include <kernel/software_updates.h>
#include <logging.h>
#include <util/fs.h>
#include <util/strencodings.h>

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <array>
#include <fstream>

namespace {

static constexpr qint64 STARTUP_CHECK_DELAY_MS{30'000};
static constexpr qint64 MANIFEST_FETCH_TIMEOUT_MS{30'000};
static constexpr qint64 CHECK_INTERVAL_SECONDS{7 * 24 * 60 * 60};
static constexpr qint64 DOWNLOAD_STALL_TIMEOUT_MS{45'000};

static constexpr const char* SETTINGS_GROUP{"software_updates"};
static constexpr const char* SETTINGS_LAST_CHECK{"last_check_epoch"};
static constexpr const char* SETTINGS_SKIPPED_VERSION{"skipped_version"};

static constexpr bool DEFAULT_CHECKFOR_UPDATES{true};

bool UpdatesEnabled()
{
    return gArgs.GetBoolArg("-checkforupdates", DEFAULT_CHECKFOR_UPDATES);
}

QString LocalVersionString()
{
    return QString::fromStdString(FormatFullVersion());
}

QString UrgencyLabel(SoftwareUpdateUrgency urgency)
{
    switch (urgency) {
    case SoftwareUpdateUrgency::RECOMMENDED:
        return QObject::tr("Recommended");
    case SoftwareUpdateUrgency::REQUIRED:
        return QObject::tr("Required");
    case SoftwareUpdateUrgency::OPTIONAL:
    default:
        return QObject::tr("Optional");
    }
}

QString InstallInstructions()
{
#if defined(Q_OS_MACOS)
    return QObject::tr(
        "1. Quit Bitcoin Purity completely.\n"
        "2. Extract the downloaded archive.\n"
        "3. Replace the existing bitcoin-qt application with the new one from the archive.");
#elif defined(Q_OS_WIN)
    return QObject::tr(
        "1. Quit Bitcoin Purity completely.\n"
        "2. Extract the downloaded zip file.\n"
        "3. Replace the existing bitcoin-qt.exe with the new one from the archive.");
#else
    return QObject::tr(
        "1. Quit Bitcoin Purity completely.\n"
        "2. Extract the downloaded archive.\n"
        "3. Replace the binaries in the bin/ directory with the new ones from the archive.");
#endif
}

std::optional<std::string> FetchManifestJson(const QUrl& url, QString& error_out)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("%1/%2").arg(CLIENT_NAME, LocalVersionString()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(MANIFEST_FETCH_TIMEOUT_MS);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();

    if (!reply->isFinished()) {
        error_out = QObject::tr("Timed out loading the release manifest.");
        reply->abort();
        reply->deleteLater();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        return std::nullopt;
    }

    if (reply->error() != QNetworkReply::NoError) {
        error_out = reply->errorString();
        reply->deleteLater();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        return std::nullopt;
    }

    const QByteArray body = reply->readAll();
    reply->deleteLater();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    return std::string(body.constData(), body.size());
}

bool HashFileSha256(const fs::path& path, uint256& hash_out)
{
    CSHA256 hasher;
    std::ifstream file{path, std::ios::binary};
    if (!file) return false;
    std::array<unsigned char, 1 << 16> buffer{};
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const std::streamsize bytes = file.gcount();
        if (bytes > 0) {
            hasher.Write(buffer.data(), static_cast<size_t>(bytes));
        }
    }
    if (!file.eof() && file.fail()) return false;
    hasher.Finalize(hash_out.begin());
    return true;
}

class SoftwareUpdateDownloadDialog : public QDialog
{
    Q_OBJECT

public:
    SoftwareUpdateDownloadDialog(
        const SoftwareReleaseInfo& release,
        const SoftwareReleaseArtifact& artifact,
        QWidget* parent)
        : QDialog(parent, GUIUtil::dialog_flags),
          m_release(release),
          m_artifact(artifact)
    {
        setWindowTitle(tr("Download Update"));
        auto* layout = new QVBoxLayout(this);

        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);

        m_progress = new QProgressBar(this);
        m_progress->setRange(0, 100);
        layout->addWidget(m_progress);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::rejected, this, &SoftwareUpdateDownloadDialog::onCancel);
        layout->addWidget(buttons);

        const QString file_name = QUrl(QString::fromStdString(m_artifact.download_uri)).fileName();
        const QString downloads_dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        m_download_path = GUIUtil::QStringToPath(downloads_dir) / fs::PathFromString(file_name.toStdString());

        m_status->setText(tr("Downloading %1 to your Downloads folder…").arg(file_name));
        setMinimumWidth(480);

        m_manager = new QNetworkAccessManager(this);
        m_stall_timer = new QTimer(this);
        m_stall_timer->setInterval(1000);
        connect(m_stall_timer, &QTimer::timeout, this, &SoftwareUpdateDownloadDialog::checkStall);

        startDownload();
    }

private Q_SLOTS:
    void onCancel()
    {
        m_cancelled = true;
        if (m_reply) {
            m_reply->abort();
        }
        reject();
    }

    void onReadyRead()
    {
        if (!m_reply || !m_output.isOpen()) return;
        m_output.write(m_reply->readAll());
        m_last_progress_ms = QDateTime::currentMSecsSinceEpoch();
        updateProgress();
    }

    void onFinished()
    {
        if (!m_reply) return;
        m_output.close();

        if (m_cancelled) {
            m_reply->deleteLater();
            m_reply = nullptr;
            return;
        }

        if (m_reply->error() != QNetworkReply::NoError) {
            fail(m_reply->errorString());
            m_reply->deleteLater();
            m_reply = nullptr;
            return;
        }

        m_reply->deleteLater();
        m_reply = nullptr;
        m_stall_timer->stop();
        verifyDownload();
    }

    void checkStall()
    {
        if (!m_reply || m_total_bytes == 0) return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_last_progress_ms > DOWNLOAD_STALL_TIMEOUT_MS) {
            fail(tr("Download stalled. Please try again."));
            m_reply->abort();
        }
    }

private:
    void startDownload()
    {
        if (fs::exists(m_download_path)) {
            fs::remove(m_download_path);
        }

        QNetworkRequest request(QUrl(QString::fromStdString(m_artifact.download_uri)));
        request.setHeader(QNetworkRequest::UserAgentHeader,
            QStringLiteral("%1/%2").arg(CLIENT_NAME, LocalVersionString()));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        m_output.setFileName(GUIUtil::PathToQString(m_download_path));
        if (!m_output.open(QIODevice::WriteOnly)) {
            fail(tr("Cannot write to %1").arg(GUIUtil::PathToQString(m_download_path.parent_path())));
            return;
        }

        m_reply = m_manager->get(request);
        connect(m_reply, &QNetworkReply::readyRead, this, &SoftwareUpdateDownloadDialog::onReadyRead);
        connect(m_reply, &QNetworkReply::finished, this, &SoftwareUpdateDownloadDialog::onFinished);
        connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
            if (total > 0) m_total_bytes = static_cast<uint64_t>(total);
            m_last_progress_ms = QDateTime::currentMSecsSinceEpoch();
            updateProgress(static_cast<uint64_t>(received));
        });

        m_last_progress_ms = QDateTime::currentMSecsSinceEpoch();
        m_stall_timer->start();
    }

    void updateProgress(uint64_t received = 0)
    {
        const uint64_t total = m_artifact.archive_size_bytes > 0 ? m_artifact.archive_size_bytes : m_total_bytes;
        if (total == 0) {
            m_progress->setRange(0, 0);
            return;
        }
        if (received == 0) {
            received = static_cast<uint64_t>(m_output.size());
        }
        const int percent = static_cast<int>(received * 100 / total);
        m_progress->setRange(0, 100);
        m_progress->setValue(std::min(percent, 100));
    }

    void verifyDownload()
    {
        m_status->setText(tr("Verifying download…"));
        m_progress->setRange(0, 0);

        uint256 digest;
        if (!HashFileSha256(m_download_path, digest) || digest != m_artifact.archive_sha256) {
            fs::remove(m_download_path);
            fail(tr("Download verification failed. The file was removed."));
            return;
        }

        m_status->setText(tr("Download complete."));
        m_progress->setRange(0, 100);
        m_progress->setValue(100);

        QMessageBox result_box(this);
        result_box.setWindowTitle(tr("Update Ready"));
        result_box.setText(tr("Bitcoin Purity %1 has been downloaded and verified.\n\n%2\n\nSaved to:\n%3")
            .arg(QString::fromStdString(m_release.version))
            .arg(InstallInstructions())
            .arg(GUIUtil::PathToQString(m_download_path)));
        result_box.setIcon(QMessageBox::Information);
        QPushButton* open_button = result_box.addButton(tr("Open Folder"), QMessageBox::AcceptRole);
        result_box.addButton(QMessageBox::Close);
        result_box.setDefaultButton(open_button);
        result_box.exec();
        if (result_box.clickedButton() == open_button) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(GUIUtil::PathToQString(m_download_path.parent_path())));
        }
        accept();
    }

    void fail(const QString& message)
    {
        m_stall_timer->stop();
        m_status->setText(message);
        QMessageBox::warning(this, tr("Download Failed"), message);
        reject();
    }

    const SoftwareReleaseInfo m_release;
    const SoftwareReleaseArtifact m_artifact;
    fs::path m_download_path;
    QLabel* m_status{nullptr};
    QProgressBar* m_progress{nullptr};
    QNetworkAccessManager* m_manager{nullptr};
    QNetworkReply* m_reply{nullptr};
    QFile m_output;
    QTimer* m_stall_timer{nullptr};
    uint64_t m_total_bytes{0};
    qint64 m_last_progress_ms{0};
    bool m_cancelled{false};
};

} // namespace

SoftwareUpdateChecker::SoftwareUpdateChecker(QObject* parent)
    : QObject(parent)
{
}

void SoftwareUpdateChecker::scheduleStartupCheck()
{
    if (!UpdatesEnabled()) return;

    QTimer::singleShot(STARTUP_CHECK_DELAY_MS, this, [this] {
        checkNow(false);
    });
}

void SoftwareUpdateChecker::checkNow(bool manual)
{
    if (!UpdatesEnabled()) {
        if (manual) {
            Q_EMIT checkFailed(tr("Software update checks are disabled."), manual);
        }
        return;
    }

    if (m_check_in_progress) return;
    if (!manual && !shouldCheckNow()) return;

    m_check_in_progress = true;
    performCheck(manual);
}

bool SoftwareUpdateChecker::shouldCheckNow() const
{
    if (ShouldSkipAutomaticSoftwareUpdatePrompt(FormatFullVersion())) {
        return false;
    }

    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    const qint64 last_check = settings.value(SETTINGS_LAST_CHECK, 0).toLongLong();
    settings.endGroup();

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return last_check <= 0 || now - last_check >= CHECK_INTERVAL_SECONDS;
}

void SoftwareUpdateChecker::recordCheckAttempt()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue(SETTINGS_LAST_CHECK, QDateTime::currentSecsSinceEpoch());
    settings.endGroup();
}

void SoftwareUpdateChecker::performCheck(bool manual)
{
    const auto manifest_url = GetSoftwareUpdatesManifestUrl(gArgs);
    if (!manifest_url || manifest_url->empty()) {
        m_check_in_progress = false;
        Q_EMIT checkFailed(tr("No software update manifest URL is configured."), manual);
        return;
    }

    recordCheckAttempt();

    QString fetch_error;
    const auto contents = FetchManifestJson(QUrl(QString::fromStdString(*manifest_url)), fetch_error);
    if (!contents) {
        m_check_in_progress = false;
        LogPrintf("Software update check failed for %s: %s\n", *manifest_url, fetch_error.toStdString());
        Q_EMIT checkFailed(fetch_error.isEmpty()
            ? tr("Could not load the release manifest from downloads.bitcoinpurity.org.")
            : fetch_error,
            manual);
        return;
    }

    const auto release = ParseSoftwareReleaseManifest(
        *contents, *manifest_url, SoftwareUpdateTrustPolicy::REMOTE_SIGNED);
    if (!release) {
        m_check_in_progress = false;
        Q_EMIT checkFailed(tr("The release manifest from downloads.bitcoinpurity.org could not be verified."), manual);
        return;
    }

    const std::string local_version = FormatFullVersion();
    if (!IsNewerSoftwareVersion(release->version, local_version)) {
        m_check_in_progress = false;
        Q_EMIT noUpdateAvailable(manual);
        return;
    }

    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    const QString skipped_version = settings.value(SETTINGS_SKIPPED_VERSION).toString();
    settings.endGroup();
    if (!manual && skipped_version == QString::fromStdString(release->version)) {
        m_check_in_progress = false;
        return;
    }

    const std::string platform = DetectCurrentSoftwarePlatform();
    const auto artifact = FindSoftwareReleaseArtifact(*release, platform);
    if (!artifact) {
        m_check_in_progress = false;
        Q_EMIT checkFailed(tr("No download is available for this platform (%1).")
            .arg(QString::fromStdString(platform)), manual);
        return;
    }

    m_check_in_progress = false;
    Q_EMIT updateAvailable(*release, *artifact);
}

void SoftwareUpdatePresenter::showUpdateAvailable(
    QWidget* parent,
    const SoftwareReleaseInfo& release,
    const SoftwareReleaseArtifact& artifact)
{
    const QString current_version = LocalVersionString();
    const QString latest_version = QString::fromStdString(release.version);

    QMessageBox box(parent);
    box.setWindowTitle(QObject::tr("Update Available"));
    box.setIcon(QMessageBox::Information);
    box.setText(QObject::tr("A newer version of Bitcoin Purity is available.\n\n"
                            "Current version: %1\n"
                            "Latest version: %2\n"
                            "Priority: %3\n\n"
                            "Would you like to download the update now?")
                    .arg(current_version, latest_version, UrgencyLabel(release.urgency)));
    QPushButton* download_button = box.addButton(QObject::tr("Download"), QMessageBox::AcceptRole);
    QPushButton* skip_button = box.addButton(QObject::tr("Skip this version"), QMessageBox::DestructiveRole);
    box.addButton(QObject::tr("Remind me later"), QMessageBox::RejectRole);
    box.setDefaultButton(download_button);
    box.exec();

    if (box.clickedButton() == skip_button) {
        QSettings settings;
        settings.beginGroup(SETTINGS_GROUP);
        settings.setValue(SETTINGS_SKIPPED_VERSION, latest_version);
        settings.endGroup();
        return;
    }
    if (box.clickedButton() != download_button) {
        return;
    }

    SoftwareUpdateDownloadDialog dialog(release, artifact, parent);
    dialog.exec();
}

void SoftwareUpdatePresenter::showNoUpdate(QWidget* parent)
{
    QMessageBox::information(
        parent,
        QObject::tr("No Updates"),
        QObject::tr("Bitcoin Purity %1 is up to date.").arg(LocalVersionString()));
}

void SoftwareUpdatePresenter::showCheckFailed(QWidget* parent, const QString& error)
{
    QMessageBox::warning(parent, QObject::tr("Update Check Failed"), error);
}

#include <qt/softwareupdater.moc>
