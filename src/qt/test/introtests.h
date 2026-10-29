// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TEST_INTROTESTS_H
#define BITCOIN_QT_TEST_INTROTESTS_H

#include <QObject>

class IntroTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void unusedDirectories();
    void nonEmptyDirectory_data();
    void nonEmptyDirectory();
    void invalidPath();
    void officialPackageWarning();
};

#endif // BITCOIN_QT_TEST_INTROTESTS_H
