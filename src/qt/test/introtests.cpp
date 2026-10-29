// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/introtests.h>

#include <common/args.h>
#include <qt/intro.h>
#include <test/util/setup_common.h>

#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {
enum class Response { CANCEL, CONTINUE, ESCAPE, ENTER };

struct ValidationResult {
    bool accepted{false};
    bool prompted{false};
    bool default_cancel{false};
    bool escape_cancel{false};
    Qt::TextFormat format{Qt::AutoText};
    QMessageBox::Icon icon{QMessageBox::NoIcon};
    QString text;
};

ValidationResult ValidateStart(Intro& intro, Response response = Response::CANCEL)
{
    ValidationResult result;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &intro, [&] {
        auto* box = intro.findChild<QMessageBox*>();
        if (!box) return;
        timer.stop();
        result.prompted = true;
        result.default_cancel = box->defaultButton() == box->button(QMessageBox::Cancel);
        result.escape_cancel = box->escapeButton() == box->button(QMessageBox::Cancel);
        result.format = box->textFormat();
        result.icon = box->icon();
        result.text = box->text() + box->informativeText();
        if (response == Response::ESCAPE || response == Response::ENTER) {
            QTest::keyClick(box, response == Response::ESCAPE ? Qt::Key_Escape : Qt::Key_Return);
            return;
        }
        for (auto* button : box->buttons()) {
            if ((response == Response::CONTINUE && box->buttonRole(button) == QMessageBox::AcceptRole) ||
                (response == Response::CANCEL && box->standardButton(button) == QMessageBox::Cancel)) {
                button->click();
                return;
            }
        }
        box->reject(); // Also dismiss unexpected error dialogs, so failures cannot hang.
    });
    timer.start(0);
    // The asynchronous free-space checker may have updated this during the last dialog.
    intro.setStatus(/*ST_OK=*/0, QString{}, 1ULL << 50);
    result.accepted = static_cast<QWizard&>(intro).validateCurrentPage();
    return result;
}

void OpenStoragePage(Intro& intro)
{
    // Starting on page 2 avoids fetching a remote package manifest.
    intro.setStartId(2);
    intro.restart();
    intro.setStatus(/*ST_OK=*/0, QString{}, 1ULL << 50);
}
} // namespace

void IntroTests::unusedDirectories()
{
    BasicTestingSetup setup{ChainType::REGTEST};
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Intro intro;
    for (const QString& path : {dir.filePath("new-directory"), dir.path()}) {
        intro.setDataDirectory(path);
        OpenStoragePage(intro);
        const auto result = ValidateStart(intro);
        QVERIFY(result.accepted);
        QVERIFY(!result.prompted);
    }
    QVERIFY(QDir(dir.path()).isEmpty());
}

void IntroTests::nonEmptyDirectory_data()
{
    QTest::addColumn<QString>("entry");
    QTest::addColumn<bool>("skip_directory_page");
    QTest::newRow("configuration") << QStringLiteral("bitcoin.conf") << false;
    QTest::newRow("hidden-file") << QStringLiteral(".DS_Store") << false;
    QTest::newRow("empty-subdirectory") << QStringLiteral("wallets/") << false;
    QTest::newRow("cli-directory") << QStringLiteral("bitcoin.conf") << true;
}

void IntroTests::nonEmptyDirectory()
{
    QFETCH(QString, entry);
    QFETCH(bool, skip_directory_page);
    BasicTestingSetup setup{ChainType::REGTEST};
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const bool directory = entry.endsWith('/');
    if (directory) {
        QVERIFY(QDir(dir.path()).mkdir(entry));
    } else {
        QFile file(dir.filePath(entry));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("keep me"), qint64{7});
    }
    Intro intro;
    intro.setDataDirectory(dir.path());
    intro.setSkipDataDirPage(skip_directory_page);
    OpenStoragePage(intro);

    for (const auto response : {Response::CANCEL, Response::ESCAPE, Response::ENTER, Response::CONTINUE, Response::CANCEL}) {
        // A previous Continue must not suppress confirmation on a subsequent Start.
        const auto result = ValidateStart(intro, response);
        QCOMPARE(result.accepted, response == Response::CONTINUE);
        QVERIFY(result.prompted);
        QVERIFY(result.default_cancel);
        QVERIFY(result.escape_cancel);
        QCOMPARE(result.format, Qt::PlainText);
        QCOMPARE(result.icon, QMessageBox::Warning);
        QVERIFY(result.text.contains(QDir::toNativeSeparators(dir.path())));
        QCOMPARE(intro.currentId(), 2);
    }
    if (!directory) {
        QFile file(dir.filePath(entry));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("keep me"));
    }
    QCOMPARE(QDir(dir.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).size(), 1);

    QTemporaryDir other;
    QVERIFY(other.isValid());
    QVERIFY(QDir(other.path()).mkdir("existing"));
    intro.setDataDirectory(other.path());
    const auto changed_path = ValidateStart(intro);
    QVERIFY(changed_path.prompted);
    QVERIFY(!changed_path.accepted);
    QVERIFY(changed_path.text.contains(QDir::toNativeSeparators(other.path())));
}

void IntroTests::invalidPath()
{
    BasicTestingSetup setup{ChainType::REGTEST};
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath("file"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    Intro intro;
    intro.setDataDirectory(file.fileName());
    OpenStoragePage(intro);
    const auto result = ValidateStart(intro);
    QVERIFY(!result.accepted);
    QVERIFY(result.prompted);
    QCOMPARE(result.icon, QMessageBox::Critical);
}

void IntroTests::officialPackageWarning()
{
    BasicTestingSetup setup{ChainType::MAIN};
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile manifest(dir.filePath("packages.json"));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    const QByteArray json{R"({"packages":[{"id":"test","snapshot_height":961814,
        "base_blockhash":"00000000000000040440db37ab428b029ee5dda57d192088c13046d124eeb80b",
        "prune_mib":0,"download_uri":"file:///unused.zip",
        "archive_sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "archive_size_bytes":1,"extracted_size_bytes":1}]})"};
    QCOMPARE(manifest.write(json), qint64{json.size()});
    manifest.close();
    setup.m_node.args->ForceSetArg("-officialpackages", manifest.fileName().toStdString());
    Intro intro;
    intro.setDataDirectory(dir.path());
    intro.setSkipDataDirPage(true);
    intro.restart(); // Page 1 reads only the local manifest above.
    QCOMPARE(intro.officialPackages().size(), size_t{1});
    const auto buttons = intro.page(1)->findChildren<QRadioButton*>();
    QCOMPARE(buttons.size(), 2);
    buttons.at(1)->setChecked(true);
    intro.next();
    QCOMPARE(intro.currentId(), 2);
    intro.setStatus(/*ST_OK=*/0, QString{}, 1ULL << 50);
    QVERIFY(intro.selectedPackage());
    for (const auto response : {Response::CANCEL, Response::CONTINUE}) {
        const auto result = ValidateStart(intro, response);
        QCOMPARE(result.accepted, response == Response::CONTINUE);
        QVERIFY(result.prompted);
        QVERIFY(result.text.contains("blocks/"));
        QVERIFY(result.text.contains("chainstate/"));
        QVERIFY(result.text.contains("bitcoin.conf"));
    }
    QVERIFY(!QDir(dir.filePath(".package-download")).exists());
}
