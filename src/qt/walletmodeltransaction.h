// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_WALLETMODELTRANSACTION_H
#define BITCOIN_QT_WALLETMODELTRANSACTION_H

#include <primitives/transaction.h>
#include <qt/sendcoinsrecipient.h>

#include <consensus/amount.h>

#include <QObject>
#include <QString>

#include <vector>

class SendCoinsRecipient;

namespace interfaces {
class Node;
}

/** Data model for a walletmodel transaction. */
class WalletModelTransaction
{
public:
    explicit WalletModelTransaction(const QList<SendCoinsRecipient> &recipients);

    QList<SendCoinsRecipient> getRecipients() const;

    void setOpReturnData(std::vector<unsigned char> data, const QString& display);
    const std::vector<unsigned char>& getOpReturnData() const;
    QString getOpReturnDisplay() const;

    CTransactionRef& getWtx();
    void setWtx(const CTransactionRef&);

    unsigned int getTransactionSize();

    void setTransactionFee(const CAmount& newFee);
    CAmount getTransactionFee() const;

    CAmount getTotalTransactionAmount() const;

    void reassignAmounts(int nChangePosRet); // needed for the subtract-fee-from-amount feature

private:
    QList<SendCoinsRecipient> recipients;
    std::vector<unsigned char> m_op_return_data;
    QString m_op_return_display;
    CTransactionRef wtx;
    CAmount fee{0};
};

#endif // BITCOIN_QT_WALLETMODELTRANSACTION_H
