#include "qvimagecore.h"

#include <array>

#include <QFileInfo>
#include <QFuture>
#include <QImageWriter>
#include <QSaveFile>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <QtMath>

namespace {

constexpr int channelLevels = 256;
constexpr int minAdjustment = -100;
constexpr int maxAdjustment = 100;
constexpr qint64 parallelPixelThreshold = 1000000;

using ChannelLut = std::array<uchar, channelLevels>;

struct ToneCurves
{
    ChannelLut red{};
    ChannelLut green{};
    ChannelLut blue{};
};

QByteArray imageFormatForPath(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpe")
        || suffix == QStringLiteral("jfif"))
        return QByteArrayLiteral("jpeg");
    return suffix.toUtf8();
}

int clampAdjustment(int value)
{
    return qBound(minAdjustment, value, maxAdjustment);
}

ToneCurves buildToneCurves(int brightness, int contrast, int warmth)
{
    // Gentle, mobile-gallery-style response curve:
    // - Brightness uses a smooth quadratic shadow/highlight-preserving curve (lifts midtones by up
    //   to ±32 while tapering off near 0 and 255) plus a small endpoint shift (±12).
    // - Contrast uses a subtle multiplier (~0.65x at -100 to ~1.55x at +100) around mid-gray.
    // - Warmth shifts R and B in opposite directions with midtone weighting.
    const qreal brightnessNorm = brightness / 100.0;
    const qreal midtoneShift = brightnessNorm * 32.0;
    const qreal endpointShift = brightnessNorm * 12.0;

    const qreal contrastNorm = contrast / 100.0;
    const qreal contrastFactor = qPow(1.55, contrastNorm);

    const qreal warmthNorm = warmth / 100.0;
    const qreal warmRed = warmthNorm * 24.0;
    const qreal warmGreen = warmthNorm * 4.0;
    const qreal warmBlue = -warmthNorm * 24.0;

    ToneCurves curves;
    for (int level = 0; level < channelLevels; ++level) {
        const qreal normalized = level / 255.0;
        // 4 * t * (1 - t) peaks at 1.0 in midtones and tapers to 0 at pure black/white
        const qreal midtoneWeight = 4.0 * normalized * (1.0 - normalized);
        const qreal brightened = level + midtoneShift * midtoneWeight + endpointShift;
        const qreal contrasted = ((brightened - 128.0) * contrastFactor) + 128.0;
        const qreal warmWeight = 0.35 + 0.65 * midtoneWeight;
        curves.red[level] = static_cast<uchar>(
                qBound(0, qRound(contrasted + warmRed * warmWeight), 255));
        curves.green[level] = static_cast<uchar>(
                qBound(0, qRound(contrasted + warmGreen * warmWeight), 255));
        curves.blue[level] = static_cast<uchar>(
                qBound(0, qRound(contrasted + warmBlue * warmWeight), 255));
    }
    return curves;
}

int saturationFixedPoint(int saturation)
{
    // 256 == 1.0x; 0 at -100, 448 (~1.75x) at +100
    return saturation >= 0 ? 256 + (saturation * 192) / 100
                           : 256 + (saturation * 256) / 100;
}

QRgb applyToneCurves(QRgb pixel, const ToneCurves &curves)
{
    return (pixel & 0xFF000000u)
            | (static_cast<QRgb>(curves.red[qRed(pixel)]) << 16)
            | (static_cast<QRgb>(curves.green[qGreen(pixel)]) << 8)
            | static_cast<QRgb>(curves.blue[qBlue(pixel)]);
}

QRgb applySaturation(QRgb pixel, const ToneCurves &curves, int saturationFp)
{
    int red = curves.red[qRed(pixel)];
    int green = curves.green[qGreen(pixel)];
    int blue = curves.blue[qBlue(pixel)];

    // Rec.709 luma in 8-bit fixed-point
    const int luma = (54 * red + 183 * green + 19 * blue) >> 8;
    red = qBound(0, luma + (((red - luma) * saturationFp) >> 8), 255);
    green = qBound(0, luma + (((green - luma) * saturationFp) >> 8), 255);
    blue = qBound(0, luma + (((blue - luma) * saturationFp) >> 8), 255);

    return (pixel & 0xFF000000u) | (static_cast<QRgb>(red) << 16)
            | (static_cast<QRgb>(green) << 8) | static_cast<QRgb>(blue);
}

} // namespace

QRect QVImageCore::clampCropRect(const QRect &pixelRect, const QSize &imageSize)
{
    return pixelRect.normalized().intersected(QRect(QPoint(0, 0), imageSize));
}

void QVImageCore::cropImage(const QRect &pixelRect)
{
    if (!currentFileDetails.isPixmapLoaded || currentFileDetails.isMovieLoaded)
        return;

    const QRect boundedRect = clampCropRect(pixelRect, loadedPixmap.size());
    if (boundedRect.isEmpty())
        return;

    loadedPixmap = loadedPixmap.copy(boundedRect);
    currentFileDetails.loadedPixmapSize = loadedPixmap.size();
    emit updateLoadedPixmapItem();
}

void QVImageCore::setLoadedPixmap(const QPixmap &pixmap)
{
    if (pixmap.isNull())
        return;

    loadedPixmap = pixmap;
    currentFileDetails.loadedPixmapSize = loadedPixmap.size();
}

QImage QVImageCore::applyAdjustments(const QImage &source, int brightness, int contrast,
                                     int saturation, int warmth)
{
    if (source.isNull())
        return source;

    brightness = clampAdjustment(brightness);
    contrast = clampAdjustment(contrast);
    saturation = clampAdjustment(saturation);
    warmth = clampAdjustment(warmth);

    if (brightness == 0 && contrast == 0 && saturation == 0 && warmth == 0)
        return source;

    const bool wasPremultiplied = source.format() == QImage::Format_ARGB32_Premultiplied;
    const bool opaque = !source.hasAlphaChannel();

    // Opaque images need no (un)premultiplication; keep RGB32 as-is.
    QImage result = source.convertToFormat(opaque ? QImage::Format_RGB32 : QImage::Format_ARGB32);

    const ToneCurves curves = buildToneCurves(brightness, contrast, warmth);
    const int saturationFp = saturationFixedPoint(saturation);
    const bool applySaturationToChannel = saturation != 0;

    const int width = result.width();
    const int height = result.height();
    const auto processRows = [&result, &curves, width, saturationFp,
                              applySaturationToChannel](int firstRow, int lastRow) {
        for (int y = firstRow; y < lastRow; ++y) {
            auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
            if (!applySaturationToChannel) {
                for (int x = 0; x < width; ++x)
                    line[x] = applyToneCurves(line[x], curves);
            } else {
                for (int x = 0; x < width; ++x)
                    line[x] = applySaturation(line[x], curves, saturationFp);
            }
        }
    };

    // Each thread works on distinct scanlines, so large images can be split safely.
    const int threadCount = qMin(QThread::idealThreadCount(), height);
    if (static_cast<qint64>(width) * height < parallelPixelThreshold || threadCount < 2) {
        processRows(0, height);
    } else {
        // Detach before sharing the buffer across threads
        result.detach();
        QList<QFuture<void>> futures;
        const int chunk = (height + threadCount - 1) / threadCount;
        for (int first = 0; first < height; first += chunk)
            futures.append(QtConcurrent::run(processRows, first, qMin(first + chunk, height)));
        for (auto &future : futures)
            future.waitForFinished();
    }

    if (wasPremultiplied)
        result = result.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    return result;
}

bool QVImageCore::saveCopy(const QString &filePath, QString *errorString) const
{
    if (!currentFileDetails.isPixmapLoaded || currentFileDetails.isMovieLoaded) {
        if (errorString)
            *errorString = tr("No still image is loaded.");
        return false;
    }

    QByteArray format = imageFormatForPath(filePath);
    if (format.isEmpty())
        format = QByteArrayLiteral("png");

    if (!QImageWriter::supportedImageFormats().contains(format)) {
        if (errorString)
            *errorString = tr("The image format is not supported for saving.");
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorString)
            *errorString = file.errorString();
        return false;
    }

    QImageWriter writer(&file, format);
    if (!writer.write(loadedPixmap.toImage())) {
        if (errorString)
            *errorString = writer.errorString();
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        if (errorString)
            *errorString = file.errorString();
        return false;
    }

    return true;
}

bool QVImageCore::replaceCurrentFile(QString *errorString)
{
    if (!currentFileDetails.isPixmapLoaded || currentFileDetails.isMovieLoaded) {
        if (errorString)
            *errorString = tr("No still image is loaded.");
        return false;
    }

    const QString filePath = currentFileDetails.fileInfo.absoluteFilePath();
    if (filePath.isEmpty()) {
        if (errorString)
            *errorString = tr("The current file has no valid path.");
        return false;
    }

    return saveCopy(filePath, errorString);
}

void QVImageCore::markCurrentFileSaved()
{
    currentFileDetails.fileInfo.refresh();
    currentFileDetails.baseImageSize = loadedPixmap.size();
    // The saved pixels already contain the current rotation. Reset it so a
    // reload does not rotate the image a second time.
    currentRotation = 0;
    // Size/date sorting may have changed with the new file contents
    updateFolderInfo();
}
