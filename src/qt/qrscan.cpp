// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qt/qrscan.h>

#include <qt/guiutil.h>
#include <qt/platformstyle.h>

#include <qt/quirc/quirc.h>

#include <cstring>
#include <functional>

#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoFrame>

#ifdef ENABLE_QRSCAN_CAMERA
#include <QAbstractVideoBuffer>
#include <QAbstractVideoSurface>
#include <QCamera>
#include <QCameraInfo>
#include <QVideoSurfaceFormat>
#endif

namespace {

QString DecodeGrayscaleQR(const QImage& gray_in)
{
    if (gray_in.isNull() || gray_in.format() != QImage::Format_Grayscale8) return {};
    if (gray_in.width() <= 0 || gray_in.height() <= 0) return {};

    quirc* qr = quirc_new();
    if (!qr) return {};

    if (quirc_resize(qr, gray_in.width(), gray_in.height()) < 0) {
        quirc_destroy(qr);
        return {};
    }

    int w = 0, h = 0;
    uint8_t* buf = quirc_begin(qr, &w, &h);
    if (!buf || w != gray_in.width() || h != gray_in.height()) {
        quirc_destroy(qr);
        return {};
    }

    for (int y = 0; y < h; ++y) {
        memcpy(buf + y * w, gray_in.constScanLine(y), static_cast<size_t>(w));
    }
    quirc_end(qr);

    QString result;
    const int count = quirc_count(qr);
    for (int i = 0; i < count; ++i) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(qr, i, &code);
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS) {
            // Mirrored images flip the grid; try again after reflecting corners.
            quirc_flip(&code);
            if (quirc_decode(&code, &data) != QUIRC_SUCCESS) {
                continue;
            }
        }
        result = QString::fromUtf8(reinterpret_cast<const char*>(data.payload), data.payload_len);
        if (!result.isEmpty()) break;
    }

    quirc_destroy(qr);
    return result;
}

QImage ToDecodeSizedGrayscale(const QImage& image_in)
{
    if (image_in.isNull()) return {};

    QImage image = image_in;
    // Camera frames are often 1080p+; quirc is faster and more reliable on moderate sizes.
    constexpr int max_dim = 960;
    if (image.width() > max_dim || image.height() > max_dim) {
        image = image.scaled(max_dim, max_dim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image.convertToFormat(QImage::Format_Grayscale8);
}

#ifdef ENABLE_QRSCAN_CAMERA
bool IsPlanarYFormat(QVideoFrame::PixelFormat fmt)
{
    switch (fmt) {
    case QVideoFrame::Format_YUV420P:
    case QVideoFrame::Format_YV12:
    case QVideoFrame::Format_NV12:
    case QVideoFrame::Format_NV21:
    case QVideoFrame::Format_IMC1:
    case QVideoFrame::Format_IMC2:
    case QVideoFrame::Format_IMC3:
    case QVideoFrame::Format_IMC4:
    case QVideoFrame::Format_Y8:
        return true;
    default:
        return false;
    }
}

/** Convert a camera frame to QImage. macOS often delivers NV12, which has no QImage format mapping. */
QImage VideoFrameToImage(const QVideoFrame& frame_in)
{
    if (!frame_in.isValid()) return {};

    QVideoFrame frame(frame_in);
    if (!frame.map(QAbstractVideoBuffer::ReadOnly)) return {};

    QImage image;
    const QVideoFrame::PixelFormat pix = frame.pixelFormat();
    const int w = frame.width();
    const int h = frame.height();
    const int stride = frame.bytesPerLine();
    const uchar* bits = frame.bits();

    if (bits && w > 0 && h > 0 && stride >= w && IsPlanarYFormat(pix)) {
        // Luma plane is first and sufficient for QR decoding / preview.
        image = QImage(w, h, QImage::Format_Grayscale8);
        if (!image.isNull()) {
            for (int y = 0; y < h; ++y) {
                memcpy(image.scanLine(y), bits + y * stride, static_cast<size_t>(w));
            }
        }
    } else if (bits && w > 0 && h > 0) {
        const QImage::Format fmt = QVideoFrame::imageFormatFromPixelFormat(pix);
        if (fmt != QImage::Format_Invalid) {
            image = QImage(bits, w, h, stride, fmt).copy();
        }
    }
    frame.unmap();

    if (image.isNull()) {
        // Qt 5.15+ can convert additional formats.
        image = QVideoFrame(frame_in).image();
    }
    return image;
}

/**
 * Custom viewfinder surface.
 *
 * QVideoProbe often receives no frames on macOS when a QCameraViewfinder is used.
 * Setting this surface as the camera viewfinder delivers frames directly.
 */
class QRScanVideoSurface : public QAbstractVideoSurface
{
public:
    explicit QRScanVideoSurface(QObject* parent = nullptr) : QAbstractVideoSurface(parent) {}

    std::function<void(const QImage&)> on_frame;

    QList<QVideoFrame::PixelFormat> supportedPixelFormats(QAbstractVideoBuffer::HandleType type) const override
    {
        if (type != QAbstractVideoBuffer::NoHandle) return {};
        return {
            QVideoFrame::Format_ARGB32,
            QVideoFrame::Format_ARGB32_Premultiplied,
            QVideoFrame::Format_RGB32,
            QVideoFrame::Format_RGB24,
            QVideoFrame::Format_RGB565,
            QVideoFrame::Format_RGB555,
            QVideoFrame::Format_NV12,
            QVideoFrame::Format_NV21,
            QVideoFrame::Format_YUV420P,
            QVideoFrame::Format_YV12,
            QVideoFrame::Format_Y8,
            QVideoFrame::Format_UYVY,
            QVideoFrame::Format_YUYV,
            QVideoFrame::Format_IMC1,
            QVideoFrame::Format_IMC2,
            QVideoFrame::Format_IMC3,
            QVideoFrame::Format_IMC4,
        };
    }

    bool present(const QVideoFrame& frame) override
    {
        if (!frame.isValid()) return false;
        const QImage image = VideoFrameToImage(frame);
        if (image.isNull()) return false;
        if (on_frame) on_frame(image);
        return true;
    }
};
#endif

} // namespace

QString DecodeQRCode(const QImage& image_in)
{
    QImage gray = ToDecodeSizedGrayscale(image_in);
    if (gray.isNull()) return {};

    QString result = DecodeGrayscaleQR(gray);
    if (!result.isEmpty()) return result;

    // Some codes / camera paths produce inverted or mirrored luminance.
    QImage inverted = gray;
    inverted.invertPixels();
    result = DecodeGrayscaleQR(inverted);
    if (!result.isEmpty()) return result;

    result = DecodeGrayscaleQR(gray.mirrored(/*horizontal=*/true, /*vertical=*/false));
    if (!result.isEmpty()) return result;

    return DecodeGrayscaleQR(inverted.mirrored(/*horizontal=*/true, /*vertical=*/false));
}

class QRScanDialog::QRScanDialogPrivate
{
public:
    QLabel* status{nullptr};
    QLabel* preview{nullptr};
#ifdef ENABLE_QRSCAN_CAMERA
    QCamera* camera{nullptr};
    QRScanVideoSurface* surface{nullptr};
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

    d->preview = new QLabel(this);
    d->preview->setAlignment(Qt::AlignCenter);
    d->preview->setMinimumSize(320, 240);
    d->preview->setStyleSheet("QLabel { background-color: #111; color: #ccc; }");
#ifdef ENABLE_QRSCAN_CAMERA
    d->preview->setText(tr("Starting camera…"));
#else
    d->preview->setText(tr("Camera support is not available in this build.\nUse “Open Image…” to decode a QR code from a picture."));
    d->preview->setWordWrap(true);
#endif
    layout->addWidget(d->preview, /*stretch=*/1);

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
    d->surface = new QRScanVideoSurface(this);
    d->throttle = new QTimer(this);
    d->throttle->setSingleShot(true);
    d->throttle->setInterval(150);

    d->surface->on_frame = [this](const QImage& image) {
        // Always refresh preview from the same frames we decode.
        d->preview->setPixmap(QPixmap::fromImage(image).scaled(d->preview->size(), Qt::KeepAspectRatio, Qt::FastTransformation));

        if (d->decoding || (d->throttle && d->throttle->isActive())) return;
        d->decoding = true;
        processFrameImage(image);
        d->decoding = false;
        if (d->throttle) d->throttle->start();
    };
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
        d->preview->setText(tr("No camera found"));
        return;
    }

    // Prefer the back/primary camera when available; otherwise the first device.
    QCameraInfo chosen = cameras.first();
    for (const QCameraInfo& info : cameras) {
        if (info.position() == QCamera::BackFace) {
            chosen = info;
            break;
        }
    }

    d->camera = new QCamera(chosen, this);
    d->camera->setCaptureMode(QCamera::CaptureViewfinder);
    d->camera->setViewfinder(d->surface);

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
