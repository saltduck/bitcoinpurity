// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_QRSCAN_H
#define BITCOIN_QT_QRSCAN_H

#include <QDialog>
#include <QString>

class QImage;
class PlatformStyle;

/** Decode the first QR code found in image. Returns empty string on failure. */
QString DecodeQRCode(const QImage& image);

/**
 * Dialog that scans a QR code from the camera (when available) or from an image file.
 * On accept, scannedData() contains the decoded payload (bitcoin: URI or address text).
 */
class QRScanDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QRScanDialog(const PlatformStyle* platformStyle, QWidget* parent = nullptr);
    ~QRScanDialog() override;

    QString scannedData() const { return m_scanned; }

private Q_SLOTS:
    void onOpenImage();
    void onCameraError(const QString& error);
    void processFrameImage(const QImage& image);

private:
    void startCamera();
    void stopCamera();
    bool tryAcceptDecoded(const QString& data);

    QString m_scanned;
    class QRScanDialogPrivate;
    QRScanDialogPrivate* d{nullptr};
};

#endif // BITCOIN_QT_QRSCAN_H
