// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qt/qrscan.h>

#include <qt/guiutil.h>
#include <qt/platformstyle.h>

#include <qt/quirc/quirc.h>

#include <cstring>

#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoFrame>

#ifdef ENABLE_QRSCAN_CAMERA
#include <QCamera>
#include <QCameraInfo>
#include <QCameraViewfinder>
#include <QVideoProbe>
#endif

QString DecodeQRCode(const QImage& image_in)
{
    if (image_in.isNull()) return {};

    const QImage image = image_in.convertToFormat(QImage::Format_Grayscale8);
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) return {};

    quirc* qr = quirc_new();
    if (!qr) return {};

    if (quirc_resize(qr, image.width(), image.height()) < 0) {
        quirc_destroy(qr);
        return {};
    }

    int w = 0, h = 0;
    uint8_t* buf = quirc_begin(qr, &w, &h);
    if (!buf || w != image.width() || h != image.height()) {
        quirc_destroy(qr);
        return {};
    }

    for (int y = 0; y < h; ++y) {
        memcpy(buf + y * w, image.constScanLine(y), static_cast<size_t>(w));
    }
    quirc_end(qr);

    QString result;
    const int count = quirc_count(qr);
    for (int i = 0; i < count; ++i) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(qr, i, &code);
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS) {
            continue;
        }
        result = QString::fromUtf8(reinterpret_cast<const char*>(data.payload), data.payload_len);
        if (!result.isEmpty()) break;
    }

    quirc_destroy(qr);
    return result;
}

class QRScanDialog::QRScanDialogPrivate
{
public:
    QLabel* status{nullptr};
    QLabel* preview_fallback{nullptr};
#ifdef ENABLE_QRSCAN_CAMERA
    QCamera* camera{nullptr};
    QCameraViewfinder* viewfinder{nullptr};
    QVideoProbe* probe{nullptr};
    QTimer* throttle{nullptr};
    bool decoding{false};
#endif
};

QRScanDialog::QRScanDialog(const PlatformStyle* platformStyle, QWidget* parent)
    : QDialog(parent, GUIUtil::dialog_flags),
      d(new QRScanDialogPrivate)
{
    Q_UNUSED(platformStyle);
    setWindowTitle(tr("Scan QR Code"));
    resize(480, 420);

    auto* layout = new QVBoxLayout(this);

#ifdef ENABLE_QRSCAN_CAMERA
    d->viewfinder = new QCameraViewfinder(this);
    d->viewfinder->setMinimumSize(320, 240);
    layout->addWidget(d->viewfinder, /*stretch=*/1);

    d->probe = new QVideoProbe(this);
    d->throttle = new QTimer(this);
    d->throttle->setSingleShot(true);
    d->throttle->setInterval(200);
#else
    d->preview_fallback = new QLabel(tr("Camera support is not available in this build.\nUse “Open Image…” to decode a QR code from a picture."), this);
    d->preview_fallback->setAlignment(Qt::AlignCenter);
    d->preview_fallback->setWordWrap(true);
    d->preview_fallback->setMinimumSize(320, 240);
    layout->addWidget(d->preview_fallback, /*stretch=*/1);
#endif

    d->status = new QLabel(tr("Point the camera at a Bitcoin QR code, or open an image file."), this);
    d->status->setWordWrap(true);
    layout->addWidget(d->status);

    auto* buttons = new QHBoxLayout();
    auto* open_btn = new QPushButton(tr("Open Image…"), this);
    auto* cancel_btn = new QPushButton(tr("Cancel"), this);
    buttons->addWidget(open_btn);
    buttons->addStretch();
    buttons->addWidget(cancel_btn);
    layout->addLayout(buttons);

    connect(open_btn, &QPushButton::clicked, this, &QRScanDialog::onOpenImage);
    connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);

#ifdef ENABLE_QRSCAN_CAMERA
    connect(d->probe, &QVideoProbe::videoFrameProbed, this, [this](const QVideoFrame& frame) {
        if (d->decoding || (d->throttle && d->throttle->isActive())) return;
        QVideoFrame clone(frame);
        if (!clone.map(QAbstractVideoBuffer::ReadOnly)) return;
        const QImage::Format fmt = QVideoFrame::imageFormatFromPixelFormat(clone.pixelFormat());
        QImage image;
        if (fmt != QImage::Format_Invalid) {
            image = QImage(clone.bits(), clone.width(), clone.height(), clone.bytesPerLine(), fmt).copy();
        }
        clone.unmap();
        if (image.isNull()) return;
        d->decoding = true;
        processFrameImage(image);
        d->decoding = false;
        if (d->throttle) d->throttle->start();
    });
    startCamera();
#endif
}

QRScanDialog::~QRScanDialog()
{
    stopCamera();
    delete d;
}

void QRScanDialog::startCamera()
{
#ifdef ENABLE_QRSCAN_CAMERA
    const QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
    if (cameras.isEmpty()) {
        d->status->setText(tr("No camera found. Use “Open Image…” instead."));
        return;
    }

    d->camera = new QCamera(cameras.first(), this);
    d->camera->setViewfinder(d->viewfinder);
    if (!d->probe->setSource(d->camera)) {
        d->status->setText(tr("Could not attach to camera frames. Use “Open Image…” instead."));
    }
    connect(d->camera, QOverload<QCamera::Error>::of(&QCamera::error), this, [this](QCamera::Error) {
        onCameraError(d->camera ? d->camera->errorString() : tr("Camera error"));
    });
    d->camera->start();
    d->status->setText(tr("Scanning…"));
#endif
}

void QRScanDialog::stopCamera()
{
#ifdef ENABLE_QRSCAN_CAMERA
    if (d->camera) {
        d->camera->stop();
    }
#endif
}

void QRScanDialog::onCameraError(const QString& error)
{
    d->status->setText(tr("Camera error: %1").arg(error));
}

void QRScanDialog::processFrameImage(const QImage& image)
{
    const QString decoded = DecodeQRCode(image);
    if (!decoded.isEmpty()) {
        tryAcceptDecoded(decoded);
    }
}

void QRScanDialog::onOpenImage()
{
    const QString filename = GUIUtil::getOpenFileName(this, tr("Open QR Code Image"), QString(),
                                                      tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"),
                                                      nullptr);
    if (filename.isEmpty()) return;

    QImage image(filename);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Scan QR Code"), tr("Could not read image file."));
        return;
    }

    const QString decoded = DecodeQRCode(image);
    if (decoded.isEmpty()) {
        QMessageBox::warning(this, tr("Scan QR Code"), tr("No QR code found in the image."));
        return;
    }
    tryAcceptDecoded(decoded);
}

bool QRScanDialog::tryAcceptDecoded(const QString& data)
{
    const QString trimmed = data.trimmed();
    if (trimmed.isEmpty()) return false;

    // Accept bitcoin: URIs or bare addresses / text payloads.
    m_scanned = trimmed;
    stopCamera();
    accept();
    return true;
}
