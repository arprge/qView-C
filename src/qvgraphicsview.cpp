#include "qvgraphicsview.h"
#include "qvapplication.h"
#include "qvinfodialog.h"
#include "qvcocoafunctions.h"
#include "settingsmanager.h"

#include <array>

#include <QRandomGenerator>
#include <QWheelEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QPainter>
#include <QKeyEvent>
#include <QPainterPath>
#include <QColor>
#include <QPen>
#include <QSettings>
#include <QMessageBox>
#include <QMovie>
#include <QtMath>
#include <QGestureEvent>
#include <QScrollBar>

namespace {

constexpr int maxAdjustment = 100;
constexpr int maxPreviewDimension = 1920;

constexpr qreal cropHandleHitRadius = 12.0;
constexpr qreal minimumCropSize = 1.0;
constexpr qreal minimumCropAspectBaseSize = 16.0;
constexpr qreal cropGridMinimumSize = 24.0;

constexpr qreal drawingZValue = 5.0;
constexpr qreal clickDotSegment = 0.01;
constexpr qreal degenerateDrawingThreshold = 1.0;

constexpr qreal pi = 3.14159265358979323846;
constexpr qreal snapAngleIncrement = pi / 4.0;

QPointF constrainedDrawingTarget(const QPointF &start, const QPointF &current,
                                 QVGraphicsView::DrawSettings::Tool tool)
{
    const QPointF delta = current - start;

    if (tool == QVGraphicsView::DrawSettings::Tool::Line) {
        // Snap the line to the nearest 45-degree angle
        const qreal length = qHypot(delta.x(), delta.y());
        if (length <= 0.0)
            return current;

        const qreal angle = qAtan2(delta.y(), delta.x());
        const qreal snappedAngle = qRound(angle / snapAngleIncrement) * snapAngleIncrement;
        return start
                + QPointF(length * qCos(snappedAngle), length * qSin(snappedAngle));
    }

    // Constrain rectangles and ellipses to a square/circle
    const qreal side = qMax(qAbs(delta.x()), qAbs(delta.y()));
    return start
            + QPointF(delta.x() < 0.0 ? -side : side, delta.y() < 0.0 ? -side : side);
}

} // namespace

QVGraphicsView::QVGraphicsView(QWidget *parent) : QGraphicsView(parent)
{
    // GraphicsView setup
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setFrameShape(QFrame::NoFrame);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    viewport()->setAutoFillBackground(false);

    // part of a pathetic attempt at gesture support
    grabGesture(Qt::PinchGesture);

    // Scene setup
    auto *scene = new QGraphicsScene(-1000000.0, -1000000.0, 2000000.0, 2000000.0, this);
    setScene(scene);

    // Initialize other variables
    currentScale = 1.0;
    scaledSize = QSize();
    isOriginalSize = false;
    lastZoomEventPos = QPoint(-1, -1);
    lastZoomRoundingError = QPointF();
    lastScrollRoundingError = QPointF();
    mousePressButton = Qt::MouseButton::NoButton;
    mousePressModifiers = Qt::KeyboardModifier::NoModifier;
    mousePressPosition = QPoint();

    zoomBasisScaleFactor = 1.0;

    connect(&imageCore, &QVImageCore::animatedFrameChanged, this,
            &QVGraphicsView::animatedFrameChanged);
    connect(&imageCore, &QVImageCore::fileChanged, this, &QVGraphicsView::postLoad);
    connect(&imageCore, &QVImageCore::updateLoadedPixmapItem, this,
            &QVGraphicsView::updateLoadedPixmapItem);

    // Should replace the other timer eventually
    expensiveScaleTimerNew = new QTimer(this);
    expensiveScaleTimerNew->setSingleShot(true);
    expensiveScaleTimerNew->setInterval(50);
    connect(expensiveScaleTimerNew, &QTimer::timeout, this, [this] { scaleExpensively(); });

    loadedPixmapItem = new QGraphicsPixmapItem();
    scene->addItem(loadedPixmapItem);

    cropDimItem = new QGraphicsPathItem();
    cropDimItem->setBrush(QColor(0, 0, 0, 110));
    cropDimItem->setPen(Qt::NoPen);
    cropDimItem->setZValue(10);
    cropDimItem->setAcceptedMouseButtons(Qt::NoButton);
    cropDimItem->hide();
    scene->addItem(cropDimItem);

    cropSelectionItem = new QGraphicsRectItem();
    cropSelectionItem->setBrush(Qt::NoBrush);
    // Solid, thicker border (cosmetic: constant on-screen width at any zoom) with a dark
    // outline behind it so it stays visible on both light and dark images.
    QPen cropPen(QColor(255, 255, 255, 255), 2, Qt::SolidLine);
    cropPen.setCosmetic(true);
    cropSelectionItem->setPen(cropPen);
    cropSelectionItem->setZValue(11);
    cropSelectionItem->setAcceptedMouseButtons(Qt::NoButton);
    cropSelectionItem->hide();
    scene->addItem(cropSelectionItem);

    cropOutlineItem = new QGraphicsRectItem(cropSelectionItem);
    QPen outlinePen(QColor(0, 0, 0, 170), 4, Qt::SolidLine);
    outlinePen.setCosmetic(true);
    cropOutlineItem->setPen(outlinePen);
    cropOutlineItem->setBrush(Qt::NoBrush);
    cropOutlineItem->setZValue(-1);
    cropOutlineItem->setAcceptedMouseButtons(Qt::NoButton);

    // Subtle rule-of-thirds composition grid inside the crop selection
    cropGridOutlineItem = new QGraphicsPathItem(cropSelectionItem);
    QPen gridOutlinePen(QColor(0, 0, 0, 80), 2, Qt::SolidLine);
    gridOutlinePen.setCosmetic(true);
    cropGridOutlineItem->setPen(gridOutlinePen);
    cropGridOutlineItem->setBrush(Qt::NoBrush);
    cropGridOutlineItem->setZValue(-0.5);
    cropGridOutlineItem->setAcceptedMouseButtons(Qt::NoButton);

    cropGridItem = new QGraphicsPathItem(cropSelectionItem);
    QPen gridPen(QColor(255, 255, 255, 110), 1, Qt::SolidLine);
    gridPen.setCosmetic(true);
    cropGridItem->setPen(gridPen);
    cropGridItem->setBrush(Qt::NoBrush);
    cropGridItem->setZValue(-0.4);
    cropGridItem->setAcceptedMouseButtons(Qt::NoButton);

    // Square grips that keep a constant on-screen size regardless of zoom
    for (auto &handleItem : cropHandleItems) {
        handleItem = new QGraphicsRectItem(QRectF(-6, -6, 12, 12), cropSelectionItem);
        handleItem->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        handleItem->setBrush(Qt::white);
        handleItem->setPen(QPen(QColor(0, 0, 0, 220), 1));
        handleItem->setAcceptedMouseButtons(Qt::NoButton);
    }

    adjustPreviewTimer = new QTimer(this);
    adjustPreviewTimer->setSingleShot(true);
    adjustPreviewTimer->setInterval(12);
    connect(adjustPreviewTimer, &QTimer::timeout, this,
            &QVGraphicsView::applyBrightnessContrastPreview);

    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this,
            &QVGraphicsView::settingsUpdated);
    settingsUpdated();
}

// Events

void QVGraphicsView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (!isOriginalSize)
        resetScale();
    else
        centerOn(loadedPixmapItem);
}

void QVGraphicsView::dropEvent(QDropEvent *event)
{
    QGraphicsView::dropEvent(event);
    loadMimeData(event->mimeData());
}

void QVGraphicsView::dragEnterEvent(QDragEnterEvent *event)
{
    QGraphicsView::dragEnterEvent(event);
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void QVGraphicsView::dragMoveEvent(QDragMoveEvent *event)
{
    QGraphicsView::dragMoveEvent(event);
    event->acceptProposedAction();
}

void QVGraphicsView::dragLeaveEvent(QDragLeaveEvent *event)
{
    QGraphicsView::dragLeaveEvent(event);
    event->accept();
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
void QVGraphicsView::enterEvent(QEvent *event)
#else
void QVGraphicsView::enterEvent(QEnterEvent *event)
#endif
{
    QGraphicsView::enterEvent(event);
    if (editMode == EditMode::Draw)
        viewport()->setCursor(Qt::CrossCursor);
    else if (editMode == EditMode::Crop)
        updateCropCursor(cropHandleAt(mapToScene(mapFromGlobal(QCursor::pos()))));
    else
        viewport()->setCursor(Qt::ArrowCursor);
}

void QVGraphicsView::mousePressEvent(QMouseEvent *event)
{
    if (editMode == EditMode::Crop && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        cropDragHandle = cropHandleAt(scenePos);
        if (cropDragHandle != CropHandle::None) {
            cropDragging = true;
            cropDragStartScene = scenePos;
            cropRectAtDragStart = cropRect;
        }
        event->accept();
        return;
    }

    if (editMode == EditMode::Draw && event->button() == Qt::LeftButton) {
        beginDrawing(mapToScene(event->pos()));
        event->accept();
        return;
    }

    const auto startWindowMove = [this, event]() {
#ifdef COCOA_LOADED
        return QVCocoaFunctions::startSystemMove(window());
#else
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        return window()->windowHandle()->startSystemMove();
#else
        Q_UNUSED(event)
        return false;
#endif
#endif
    };

    const auto startFallbackWindowMove = [this, event]() {
        mousePressButton = event->button();
        mousePressModifiers = event->modifiers();
        mousePressPosition = event->pos();
    };

    // Check for Ctrl/Cmd drag
    if (event->button() == Qt::LeftButton &&
        event->modifiers().testFlag(Qt::ControlModifier) &&
        qvApp->getSettingsManager().getBool(SettingsManager::Setting::CtrlDragWindow)) {
        const auto windowState = window()->windowState();
        if (!windowState.testFlag(Qt::WindowFullScreen)
            && !windowState.testFlag(Qt::WindowMaximized)) {
            if (!startWindowMove()) {
                startFallbackWindowMove();
            }
            return;
        }
    }

    // Check for titlebar region drag
    if (event->button() == Qt::LeftButton) {
        const auto windowState = window()->windowState();
        if (!windowState.testFlag(Qt::WindowFullScreen)
            && !windowState.testFlag(Qt::WindowMaximized)) {
#ifdef COCOA_LOADED
            // Check if click is in titlebar region
            int titlebarHeight = QVCocoaFunctions::getTitlebarHeight(window()->windowHandle());
            if (event->pos().y() <= titlebarHeight) {
                if (!startWindowMove()) {
                    startFallbackWindowMove();
                }
                return;
            }
#endif
        }
    }

    QGraphicsView::mousePressEvent(event);
}

void QVGraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    if (editMode == EditMode::Crop && event->buttons() == Qt::NoButton) {
        updateCropCursor(cropHandleAt(mapToScene(event->pos())));
    }

    if (editMode == EditMode::Crop && cropDragging) {
        dragCropTo(mapToScene(event->pos()), event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
        return;
    }

    if (editMode == EditMode::Draw && isDrawing) {
        updateDrawing(mapToScene(event->pos()), event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
        return;
    }

    if (mousePressButton == Qt::LeftButton) {
        if (mousePressModifiers.testFlag(Qt::ControlModifier)
            && !event->modifiers().testFlag(Qt::ControlModifier)) {
            mousePressButton = Qt::NoButton;
            mousePressModifiers = Qt::NoModifier;
            QGraphicsView::mouseMoveEvent(event);
            return;
        }

        const QPoint delta = event->pos() - mousePressPosition;
        window()->move(window()->pos() + delta);
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void QVGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    if (editMode == EditMode::Crop && event->button() == Qt::LeftButton) {
        if (cropDragging)
            dragCropTo(mapToScene(event->pos()), event->modifiers().testFlag(Qt::ShiftModifier));
        cropDragging = false;
        cropDragHandle = CropHandle::None;
        updateCropCursor(cropHandleAt(mapToScene(event->pos())));
        event->accept();
        return;
    }

    if (editMode == EditMode::Draw && event->button() == Qt::LeftButton && isDrawing) {
        finishDrawing(mapToScene(event->pos()), event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
        return;
    }

    mousePressButton = Qt::NoButton;
    mousePressModifiers = Qt::NoModifier;
    QGraphicsView::mouseReleaseEvent(event);
    if (!isEditing())
        viewport()->setCursor(Qt::ArrowCursor);
}

void QVGraphicsView::keyPressEvent(QKeyEvent *event)
{
    if (isEditing()) {
        const bool isEnterKey = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;

        if (isEnterKey) {
            if (event->modifiers().testFlag(Qt::ControlModifier))
                emit commitRequested(CommitAction::ReplaceOriginal);
            else
                emit commitRequested(CommitAction::SaveCopy);

            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Escape) {
            cancelEdit();
            event->accept();
            return;
        }

        if (editMode == EditMode::Draw && event->matches(QKeySequence::Undo)) {
            undoLastDrawing();
            event->accept();
            return;
        }
    }

    QGraphicsView::keyPressEvent(event);
}

bool QVGraphicsView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && isEditing()) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const bool isEnterKey =
                keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;
        if (isEnterKey || keyEvent->key() == Qt::Key_Escape
            || (editMode == EditMode::Draw && keyEvent->matches(QKeySequence::Undo))) {
            keyEvent->accept();
            return true;
        }
    }

    // this is for touchpad pinch gestures
    if (event->type() == QEvent::Gesture) {
        auto *gestureEvent = static_cast<QGestureEvent *>(event);
        if (QGesture *pinch = gestureEvent->gesture(Qt::PinchGesture)) {
            auto *pinchGesture = static_cast<QPinchGesture *>(pinch);
            QPinchGesture::ChangeFlags changeFlags = pinchGesture->changeFlags();

            if (changeFlags & QPinchGesture::ScaleFactorChanged) {
                const QPoint hotPoint = mapFromGlobal(pinchGesture->hotSpot().toPoint());
                zoom(pinchGesture->scaleFactor(), hotPoint);
            }

            // Fun rotation stuff maybe later
            //            if (changeFlags & QPinchGesture::RotationAngleChanged) {
            //                qreal rotationDelta = pinchGesture->rotationAngle() -
            //                pinchGesture->lastRotationAngle(); rotate(rotationDelta);
            //                centerOn(loadedPixmapItem);
            //            }
            return true;
        }
    } else if (event->type() == QEvent::NativeGesture) {
        auto *nativeEvent = static_cast<QNativeGestureEvent *>(event);
        if (nativeEvent->gestureType() == Qt::ZoomNativeGesture) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
            const QPoint eventPos = nativeEvent->position().toPoint();
#else
            const QPoint eventPos = nativeEvent->pos();
#endif
            zoom(nativeEvent->value() + 1, eventPos);
            return true;
        }
    }
    return QGraphicsView::event(event);
}

void QVGraphicsView::wheelEvent(QWheelEvent *event)
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    const QPoint eventPos = event->position().toPoint();
#else
    const QPoint eventPos = event->pos();
#endif

    const bool modifierPressed = event->modifiers().testFlag(Qt::ControlModifier);
    bool dontZoom = qvGetSettingInt(ScrollZoom) == 2;
    if (modifierPressed) {
        dontZoom = !dontZoom;
    }

    bool touchDeviceDetected = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Auto-detect touchpad
    touchDeviceDetected = event->device()->type() == QInputDevice::DeviceType::TouchPad
            || event->device()->type() == QInputDevice::DeviceType::TouchScreen;
    // Real touchpads are likely to exhibit these characteristics in empirical testing
    touchDeviceDetected = touchDeviceDetected && event->phase() != Qt::NoScrollPhase;
    if (touchDeviceDetected && qvGetSettingInt(ScrollZoom) == 1) {
        // If this is a touch device, override setting
        dontZoom = !modifierPressed;
    }
#endif

    if (dontZoom) {
        const qreal scrollDivisor = 2.0; // To make scrolling less sensitive
        qreal scrollX = event->angleDelta().x() * (isRightToLeft() ? 1 : -1) / scrollDivisor;
        qreal scrollY = event->angleDelta().y() * -1 / scrollDivisor;

        if (event->modifiers() & Qt::ShiftModifier)
            std::swap(scrollX, scrollY);

        QPointF targetScrollDelta = QPointF(scrollX, scrollY) - lastScrollRoundingError;
        QPoint roundedScrollDelta = targetScrollDelta.toPoint();

        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + roundedScrollDelta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + roundedScrollDelta.y());

        lastScrollRoundingError = roundedScrollDelta - targetScrollDelta;

        return;
    }

    const int yDelta = event->angleDelta().y();
    const qreal yScale = 120.0;

    if (yDelta == 0)
        return;

    const qreal zoomAmountPerWheelClick = qvGetSettingInt(ScaleFactor)/100.0;
    qreal zoomFactor = zoomAmountPerWheelClick;
    if (qvGetSettingBool(FractionalZoom) || touchDeviceDetected) {
        const qreal fractionalWheelClicks = qFabs(yDelta) / yScale;
        zoomFactor *= fractionalWheelClicks;
    }
    zoomFactor += 1.0;

    if (yDelta < 0)
        zoomFactor = qPow(zoomFactor, -1);

    zoom(zoomFactor, eventPos);
}

// Functions

QMimeData *QVGraphicsView::getMimeData() const
{
    auto *mimeData = new QMimeData();
    if (!getCurrentFileDetails().isPixmapLoaded)
        return mimeData;

    mimeData->setUrls(
            { QUrl::fromLocalFile(imageCore.getCurrentFileDetails().fileInfo.absoluteFilePath()) });
    mimeData->setImageData(imageCore.getLoadedPixmap().toImage());
    return mimeData;
}

void QVGraphicsView::loadMimeData(const QMimeData *mimeData)
{
    if (mimeData == nullptr)
        return;

    if (!mimeData->hasUrls())
        return;

    const QList<QUrl> urlList = mimeData->urls();

    bool first = true;
    for (const auto &url : urlList) {
        if (first) {
            loadFile(url.toString());
            emit cancelSlideshow();
            first = false;
            continue;
        }
        QVApplication::openFile(url.toString());
    }
}

void QVGraphicsView::loadFile(const QString &fileName)
{
    imageCore.loadFile(fileName);
}

void QVGraphicsView::reloadFile()
{
    if (!getCurrentFileDetails().isPixmapLoaded)
        return;

    imageCore.loadFile(getCurrentFileDetails().fileInfo.absoluteFilePath(), true);
}

void QVGraphicsView::postLoad()
{
    if (isEditing())
        resetEditState();

    updateLoadedPixmapItem();
    qvApp->getActionManager().addFileToRecentsList(getCurrentFileDetails().fileInfo);

    emit fileChanged();
}

void QVGraphicsView::zoomIn(const QPoint &pos)
{
    zoom(qvGetSettingInt(ScaleFactor)/100.0 + 1, pos);
}

void QVGraphicsView::zoomOut(const QPoint &pos)
{
    zoom(qPow(qvGetSettingInt(ScaleFactor)/100.0 + 1, -1), pos);
}

void QVGraphicsView::zoom(qreal scaleFactor, const QPoint &pos)
{
    // don't zoom too far out, dude
    currentScale *= scaleFactor;
    if (currentScale >= 500 || currentScale <= 0.01) {
        currentScale *= qPow(scaleFactor, -1);
        return;
    }

    updateFilteringMode();

    if (pos != lastZoomEventPos) {
        lastZoomEventPos = pos;
        lastZoomRoundingError = QPointF();
    }
    const QPointF scenePos = mapToScene(pos) - lastZoomRoundingError;

    zoomBasisScaleFactor *= scaleFactor;
    setTransform(QTransform(zoomBasis).scale(zoomBasisScaleFactor, zoomBasisScaleFactor));
    absoluteTransform.scale(scaleFactor, scaleFactor);

    // If we are zooming in, we have a point to zoom towards, the mouse is on top of the viewport,
    // and cursor zooming is enabled
    if (currentScale > 1.00001 && pos != QPoint(-1, -1) && underMouse()
        && qvGetSettingBool(CursorZoom)) {
        const QPointF p1mouse = mapFromScene(scenePos);
        const QPointF move = p1mouse - pos;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                                        + (move.x() * (isRightToLeft() ? -1 : 1)));
        verticalScrollBar()->setValue(verticalScrollBar()->value() + move.y());
        lastZoomRoundingError = mapToScene(pos) - scenePos;
    } else {
        centerOn(loadedPixmapItem);
    }

    if (qvGetSettingBool(ScalingEnabled) && !isOriginalSize && !isEditing()) {
        expensiveScaleTimerNew->start();
    }
}

void QVGraphicsView::scaleExpensively()
{
    if (isEditing())
        return;

    // Determine if mirrored or flipped
    bool mirrored = false;
    if (transform().m11() < 0)
        mirrored = true;

    bool flipped = false;
    if (transform().m22() < 0)
        flipped = true;

    // If we are above maximum scaling size
    if ((currentScale >= MAX_EXPENSIVE_SCALING_SIZE)
        || (!qvGetSettingBool(ScalingTwoEnabled) && currentScale > 1.00001)) {
        // Return to original size
        makeUnscaled();
        return;
    }

    // Map size of the original pixmap to the scale acquired in fitting with modification from
    // zooming percentage
    const QRectF mappedRect =
            absoluteTransform.mapRect(QRectF({}, getCurrentFileDetails().loadedPixmapSize));
    const QSizeF mappedPixmapSize = mappedRect.size() * devicePixelRatioF();

    // Undo mirror/flip before new transform
    if (mirrored)
        scale(-1, 1);

    if (flipped)
        scale(1, -1);

    // Set image to scaled version
    loadedPixmapItem->setPixmap(imageCore.scaleExpensively(mappedPixmapSize));

    // Reset transformation
    setTransform(
            QTransform::fromScale(qPow(devicePixelRatioF(), -1), qPow(devicePixelRatioF(), -1)));

    // Redo mirror/flip after new transform
    if (mirrored)
        scale(-1, 1);

    if (flipped)
        scale(1, -1);

    // Set zoombasis
    zoomBasis = transform();
    zoomBasisScaleFactor = 1.0;
}

void QVGraphicsView::makeUnscaled()
{
    // Determine if mirrored or flipped
    bool mirrored = false;
    if (transform().m11() < 0)
        mirrored = true;

    bool flipped = false;
    if (transform().m22() < 0)
        flipped = true;

    // Return to original size
    if (getCurrentFileDetails().isMovieLoaded)
        loadedPixmapItem->setPixmap(getLoadedMovie().currentPixmap());
    else
        loadedPixmapItem->setPixmap(getLoadedPixmap());

    setTransform(absoluteTransform);

    // Redo mirror/flip after new transform
    if (mirrored)
        scale(-1, 1);

    if (flipped)
        scale(1, -1);

    // Reset transformation
    zoomBasis = transform();
    zoomBasisScaleFactor = 1.0;
}

void QVGraphicsView::updateFilteringMode()
{
    const bool exceededSmoothScaleLimit = currentScale >= MAX_FILTERING_SIZE;
    loadedPixmapItem->setTransformationMode(!exceededSmoothScaleLimit
                                                            && qvGetSettingBool(FilteringEnabled)
                                                    ? Qt::SmoothTransformation
                                                    : Qt::FastTransformation);
}

void QVGraphicsView::animatedFrameChanged(QRect rect)
{
    Q_UNUSED(rect)

    if (qvGetSettingBool(ScalingEnabled)) {
        scaleExpensively();
    } else {
        loadedPixmapItem->setPixmap(getLoadedMovie().currentPixmap());
    }
}

void QVGraphicsView::updateLoadedPixmapItem()
{
    // set pixmap and offset
    loadedPixmapItem->setPixmap(getLoadedPixmap());
    scaledSize = loadedPixmapItem->boundingRect().size().toSize();

    resetScale();

    emit updatedLoadedPixmapItem();
}

void QVGraphicsView::resetScale()
{
    if (!getCurrentFileDetails().isPixmapLoaded)
        return;

    fitInViewMarginless(loadedPixmapItem);

    if (qvGetSettingBool(ScalingEnabled) && !isEditing())
        expensiveScaleTimerNew->start();
}

void QVGraphicsView::originalSize()
{
    if (isOriginalSize) {
        // If we are at the actual original size
        if (transform() == QTransform()) {
            resetScale(); // back to normal mode
            return;
        }
    }
    makeUnscaled();

    resetTransform();
    centerOn(loadedPixmapItem);

    zoomBasis = transform();
    zoomBasisScaleFactor = 1.0;
    absoluteTransform = transform();

    isOriginalSize = true;
}

void QVGraphicsView::goToFile(const GoToFileMode &mode, int index)
{
    bool shouldRetryFolderInfoUpdate = false;

    // Update folder info only after a little idle time as an optimization for when
    // the user is rapidly navigating through files.
    if (!getCurrentFileDetails().timeSinceLoaded.isValid()
        || getCurrentFileDetails().timeSinceLoaded.hasExpired(3000)) {
        // Make sure the file still exists because if it disappears from the file listing we'll lose
        // track of our index within the folder. Use the static 'exists' method to avoid caching.
        // If we skip updating now, flag it for retry later once we locate a new file.
        if (QFile::exists(getCurrentFileDetails().fileInfo.absoluteFilePath()))
            imageCore.updateFolderInfo();
        else
            shouldRetryFolderInfoUpdate = true;
    }

    const auto &fileList = getCurrentFileDetails().folderFileInfoList;
    if (fileList.isEmpty())
        return;

    int newIndex = getCurrentFileDetails().loadedIndexInFolder;
    int searchDirection = 0;

    switch (mode) {
    case GoToFileMode::constant: {
        newIndex = index;
        break;
    }
    case GoToFileMode::first: {
        newIndex = 0;
        searchDirection = 1;
        break;
    }
    case GoToFileMode::previous: {
        if (newIndex == 0) {
            if (qvGetSettingBool(LoopFoldersEnabled))
                newIndex = fileList.size() - 1;
            else
                emit cancelSlideshow();
        } else
            newIndex--;
        searchDirection = -1;
        break;
    }
    case GoToFileMode::next: {
        if (fileList.size() - 1 == newIndex) {
            if (qvGetSettingBool(LoopFoldersEnabled))
                newIndex = 0;
            else
                emit cancelSlideshow();
        } else
            newIndex++;
        searchDirection = 1;
        break;
    }
    case GoToFileMode::last: {
        newIndex = fileList.size() - 1;
        searchDirection = -1;
        break;
    }
    case GoToFileMode::random: {
        if (fileList.size() > 1) {
            int randomIndex;
            do {
                randomIndex = QRandomGenerator::global()->bounded(
                        int(fileList.size()));
            } while (randomIndex == newIndex);
            newIndex = randomIndex;
        }
        searchDirection = 1;
        break;
    }
    }

    if (searchDirection != 0) {
        while (searchDirection == 1 && newIndex < fileList.size() - 1
               && !QFile::exists(fileList.value(newIndex).absoluteFilePath))
            newIndex++;
        while (searchDirection == -1 && newIndex > 0
               && !QFile::exists(fileList.value(newIndex).absoluteFilePath))
            newIndex--;
    }

    const QString nextImageFilePath = fileList.value(newIndex).absoluteFilePath;

    if (!QFile::exists(nextImageFilePath)
        || nextImageFilePath == getCurrentFileDetails().fileInfo.absoluteFilePath())
        return;

    if (shouldRetryFolderInfoUpdate) {
        // If the user just deleted a file through qView, closeImage will have been called which
        // empties currentFileDetails.fileInfo. In this case updateFolderInfo can't infer the
        // directory from fileInfo like it normally does, so we'll explicity pass in the folder
        // here.
        imageCore.updateFolderInfo(QFileInfo(nextImageFilePath).path());
    }

    loadFile(nextImageFilePath);
}

void QVGraphicsView::fitInViewMarginless(const QRectF &rect)
{
#ifdef COCOA_LOADED
    int obscuredHeight = QVCocoaFunctions::getObscuredHeight(window()->windowHandle());
#else
    int obscuredHeight = 0;
#endif

    // Set adjusted image size / bounding rect based on
    QSize adjustedImageSize = getCurrentFileDetails().loadedPixmapSize;
    QRectF adjustedBoundingRect = rect;

    switch (qvGetSettingInt(CropMode)) { // should be enum tbh
    case 1: // only take into account height
    {
        adjustedImageSize.setWidth(1);
        adjustedBoundingRect.setWidth(1);
        break;
    }
    case 2: // only take into account width
    {
        adjustedImageSize.setHeight(1);
        adjustedBoundingRect.setHeight(1);
        break;
    }
    }
    adjustedBoundingRect.moveCenter(rect.center());

    if (!scene() || adjustedBoundingRect.isNull())
        return;

    // Reset the view scale to 1:1.
    QRectF unity = transform().mapRect(QRectF(0, 0, 1, 1));
    if (unity.isEmpty())
        return;
    scale(1 / unity.width(), 1 / unity.height());

    // Determine what we are resizing to
    const int adjWidth = width() - MARGIN;
    const int adjHeight = height() - MARGIN - obscuredHeight;

    QRectF viewRect;
    // Resize to window size unless you are meant to stop at the actual size, basically
    if (qvGetSettingBool(PastActualSizeEnabled)
        || (adjustedImageSize.width() >= adjWidth || adjustedImageSize.height() >= adjHeight)) {
        viewRect = viewport()->rect().adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);
        viewRect.setHeight(viewRect.height() - obscuredHeight);
    } else {
        // stop at actual size
        viewRect = QRect(QPoint(), getCurrentFileDetails().loadedPixmapSize);
        QPoint center = this->rect().center();
        center.setY(center.y() - obscuredHeight);
        viewRect.moveCenter(center);
    }

    if (viewRect.isEmpty())
        return;

    // Find the ideal x / y scaling ratio to fit \a rect in the view.
    QRectF sceneRect = transform().mapRect(adjustedBoundingRect);
    if (sceneRect.isEmpty())
        return;

    qreal xratio = viewRect.width() / sceneRect.width();
    qreal yratio = viewRect.height() / sceneRect.height();

    xratio = yratio = qMin(xratio, yratio);

    // Find and set the transform required to fit the original image
    // Compact version of above code
    QRectF sceneRect2 = transform().mapRect(QRectF({}, adjustedImageSize));
    qreal absoluteRatio =
            qMin(viewRect.width() / sceneRect2.width(), viewRect.height() / sceneRect2.height());

    absoluteTransform = QTransform::fromScale(absoluteRatio, absoluteRatio);

    // Scale and center on the center of \a rect.
    scale(xratio, yratio);
    centerOn(adjustedBoundingRect.center());

    // variables
    zoomBasis = transform();

    isOriginalSize = false;
    currentScale = 1.0;
    updateFilteringMode();
    zoomBasisScaleFactor = 1.0;
}

void QVGraphicsView::fitInViewMarginless(const QGraphicsItem *item)
{
    return fitInViewMarginless(item->sceneBoundingRect());
}

void QVGraphicsView::centerOn(const QPointF &pos)
{
#ifdef COCOA_LOADED
    int obscuredHeight = QVCocoaFunctions::getObscuredHeight(window()->windowHandle());
#else
    int obscuredHeight = 0;
#endif

    qreal width = viewport()->width();
    qreal height = viewport()->height() - obscuredHeight;
    QPointF viewPoint = transform().map(pos);

    if (isRightToLeft()) {
        qint64 horizontal = 0;
        horizontal += horizontalScrollBar()->minimum();
        horizontal += horizontalScrollBar()->maximum();
        horizontal -= int(viewPoint.x() - width / 2.0);
        horizontalScrollBar()->setValue(horizontal);
    } else {
        horizontalScrollBar()->setValue(int(viewPoint.x() - width / 2.0));
    }

    verticalScrollBar()->setValue(int(viewPoint.y() - obscuredHeight - (height / 2.0)));
}

void QVGraphicsView::centerOn(qreal x, qreal y)
{
    centerOn(QPointF(x, y));
}

void QVGraphicsView::centerOn(const QGraphicsItem *item)
{
    centerOn(item->sceneBoundingRect().center());
}

void QVGraphicsView::settingsUpdated()
{
    if (getCurrentFileDetails().isPixmapLoaded)
        resetScale();
}

void QVGraphicsView::closeImage()
{
    imageCore.closeImage();
}

void QVGraphicsView::jumpToNextFrame()
{
    imageCore.jumpToNextFrame();
}

void QVGraphicsView::setPaused(const bool &desiredState)
{
    imageCore.setPaused(desiredState);
}

void QVGraphicsView::setSpeed(const int &desiredSpeed)
{
    imageCore.setSpeed(desiredSpeed);
}

void QVGraphicsView::rotateImage(int rotation)
{
    imageCore.rotateImage(rotation);
}

void QVGraphicsView::clearEditBuffers()
{
    adjustPreviewTimer->stop();
    editBasePixmap = QPixmap();
    editBaseImage = QImage();
    editPreviewImage = QImage();
    pendingBrightness = 0;
    pendingContrast = 0;
    pendingSaturation = 0;
    pendingWarmth = 0;
    adjustComparingOriginal = false;
}

bool QVGraphicsView::hasPendingAdjustments() const
{
    return pendingBrightness != 0 || pendingContrast != 0 || pendingSaturation != 0
            || pendingWarmth != 0;
}

void QVGraphicsView::showPixmap(const QPixmap &pixmap)
{
    loadedPixmapItem->setScale(1.0);
    loadedPixmapItem->setPixmap(pixmap);
    scaledSize = loadedPixmapItem->boundingRect().size().toSize();
}

bool QVGraphicsView::beginEdit(const EditMode &mode)
{
    if (mode == EditMode::None) {
        cancelEdit();
        return false;
    }

    const QVImageCore::FileDetails &fileDetails = getCurrentFileDetails();
    if (!fileDetails.isPixmapLoaded || fileDetails.isMovieLoaded)
        return false;

    if (isEditing())
        cancelEdit();

    makeUnscaled();
    isOriginalSize = false;

    clearEditBuffers();

    cropDimItem->hide();
    cropSelectionItem->hide();
    cropAspectRatio = QSizeF();
    cropDragging = false;
    cropDragHandle = CropHandle::None;
    hasCropSelection = false;
    clearDrawings();

    mousePressButton = Qt::NoButton;
    mousePressModifiers = Qt::NoModifier;

    editMode = mode;

    switch (editMode) {
    case EditMode::Crop:
        setDragMode(QGraphicsView::NoDrag);
        viewport()->setMouseTracking(true);
        viewport()->setCursor(Qt::ArrowCursor);

        // Start with the borders on the image edges; the user drags them inward
        cropRect = loadedPixmapItem->boundingRect();
        hasCropSelection = true;
        updateCropOverlay();
        break;
    case EditMode::Adjust:
        editBasePixmap = imageCore.getLoadedPixmap();
        editBaseImage = editBasePixmap.toImage();
        if (editBaseImage.width() > maxPreviewDimension
            || editBaseImage.height() > maxPreviewDimension) {
            editPreviewImage = editBaseImage.scaled(maxPreviewDimension, maxPreviewDimension,
                                                    Qt::KeepAspectRatio, Qt::SmoothTransformation);
        } else {
            editPreviewImage = editBaseImage;
        }
        showPixmap(editBasePixmap);
        setDragMode(QGraphicsView::ScrollHandDrag);
        viewport()->setCursor(Qt::ArrowCursor);
        break;
    case EditMode::Draw:
        setDragMode(QGraphicsView::NoDrag);
        viewport()->setCursor(Qt::CrossCursor);
        break;
    case EditMode::None:
        break;
    }

    emit editModeChanged(editMode);
    return true;
}

bool QVGraphicsView::commitCropEdit()
{
    const QRect pixelRect = cropPixelRect();
    if (pixelRect.width() < 1 || pixelRect.height() < 1)
        return false;

    // Borders still on the image edges: nothing to crop
    if (pixelRect == QRect(QPoint(0, 0), getLoadedPixmap().size()))
        return false;

    cropDimItem->hide();
    cropSelectionItem->hide();
    imageCore.cropImage(pixelRect);
    return true;
}

bool QVGraphicsView::commitAdjustEdit()
{
    adjustPreviewTimer->stop();

    // Nothing changed: stay in edit mode instead of prompting to save
    if (!hasPendingAdjustments())
        return false;

    // Compute the full-resolution result on commit
    const QPixmap finalPixmap = QPixmap::fromImage(QVImageCore::applyBrightnessContrast(
            editBaseImage, pendingBrightness, pendingContrast, pendingSaturation, pendingWarmth));
    imageCore.setPreviewPixmap(finalPixmap);
    showPixmap(finalPixmap);
    return true;
}

bool QVGraphicsView::commitDrawEdit()
{
    // Nothing drawn: stay in edit mode instead of prompting to save
    if (drawingItems.isEmpty())
        return false;

    const QPixmap flattened = flattenDrawings();
    if (flattened.isNull())
        return false;

    imageCore.setPreviewPixmap(flattened);
    showPixmap(flattened);
    return true;
}

bool QVGraphicsView::applyEdit()
{
    if (!isEditing())
        return false;

    bool committed = false;
    switch (editMode) {
    case EditMode::Crop:
        committed = commitCropEdit();
        break;
    case EditMode::Adjust:
        committed = commitAdjustEdit();
        break;
    case EditMode::Draw:
        committed = commitDrawEdit();
        break;
    case EditMode::None:
        break;
    }

    if (!committed)
        return false;

    resetEditState();

    if (qvGetSettingBool(ScalingEnabled) && !isOriginalSize)
        expensiveScaleTimerNew->start();

    return true;
}

void QVGraphicsView::cancelEdit()
{
    if (!isEditing())
        return;

    if (editMode == EditMode::Adjust && !editBasePixmap.isNull()) {
        imageCore.setPreviewPixmap(editBasePixmap);
        showPixmap(editBasePixmap);
    }

    resetEditState();

    if (qvGetSettingBool(ScalingEnabled) && !isOriginalSize)
        expensiveScaleTimerNew->start();
}

void QVGraphicsView::setBrightnessContrastPreview(int brightness, int contrast, int saturation,
                                                  int warmth)
{
    if (editMode != EditMode::Adjust)
        return;

    pendingBrightness = qBound(-maxAdjustment, brightness, maxAdjustment);
    pendingContrast = qBound(-maxAdjustment, contrast, maxAdjustment);
    pendingSaturation = qBound(-maxAdjustment, saturation, maxAdjustment);
    pendingWarmth = qBound(-maxAdjustment, warmth, maxAdjustment);

    if (!adjustComparingOriginal && !adjustPreviewTimer->isActive())
        adjustPreviewTimer->start();
}

void QVGraphicsView::setAdjustCompareOriginal(bool showOriginal)
{
    if (editMode != EditMode::Adjust || editBasePixmap.isNull())
        return;

    adjustComparingOriginal = showOriginal;
    if (showOriginal) {
        adjustPreviewTimer->stop();
        showPixmap(editBasePixmap);
    } else {
        applyBrightnessContrastPreview();
    }
}

void QVGraphicsView::undoLastDrawing()
{
    if (drawingItems.isEmpty())
        return;

    delete drawingItems.takeLast();
    emit drawingCountChanged(drawingItems.size());
}

QPen QVGraphicsView::makeDrawingPen() const
{
    QPen pen(drawSettings.color, drawSettings.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    return pen;
}

void QVGraphicsView::beginDrawing(const QPointF &scenePos)
{
    drawStartScenePos = scenePos;
    isDrawing = true;

    const QPen pen = makeDrawingPen();
    QGraphicsItem *item = nullptr;

    switch (drawSettings.tool) {
    case DrawSettings::Tool::Pen: {
        activePenPath = QPainterPath(scenePos);
        // Tiny segment so a single click leaves a dot (round caps)
        activePenPath.lineTo(scenePos + QPointF(clickDotSegment, 0.0));
        auto *pathItem = new QGraphicsPathItem(activePenPath);
        pathItem->setPen(pen);
        pathItem->setBrush(Qt::NoBrush);
        item = pathItem;
        break;
    }
    case DrawSettings::Tool::Line: {
        auto *lineItem = new QGraphicsLineItem(QLineF(scenePos, scenePos));
        lineItem->setPen(pen);
        item = lineItem;
        break;
    }
    case DrawSettings::Tool::Rectangle: {
        auto *rectItem = new QGraphicsRectItem(QRectF(scenePos, QSizeF(0, 0)));
        rectItem->setPen(pen);
        rectItem->setBrush(Qt::NoBrush);
        item = rectItem;
        break;
    }
    case DrawSettings::Tool::Ellipse: {
        auto *ellipseItem = new QGraphicsEllipseItem(QRectF(scenePos, QSizeF(0, 0)));
        ellipseItem->setPen(pen);
        ellipseItem->setBrush(Qt::NoBrush);
        item = ellipseItem;
        break;
    }
    }

    item->setZValue(drawingZValue);
    item->setAcceptedMouseButtons(Qt::NoButton);
    scene()->addItem(item);
    activeDrawingItem = item;
}

void QVGraphicsView::updateDrawing(const QPointF &scenePos, bool constrain)
{
    if (!isDrawing || !activeDrawingItem)
        return;

    const QPointF targetPos =
            constrain && drawSettings.tool != DrawSettings::Tool::Pen
            ? constrainedDrawingTarget(drawStartScenePos, scenePos, drawSettings.tool)
            : scenePos;

    switch (drawSettings.tool) {
    case DrawSettings::Tool::Pen:
        activePenPath.lineTo(targetPos);
        static_cast<QGraphicsPathItem *>(activeDrawingItem)->setPath(activePenPath);
        break;
    case DrawSettings::Tool::Line:
        static_cast<QGraphicsLineItem *>(activeDrawingItem)
                ->setLine(QLineF(drawStartScenePos, targetPos));
        break;
    case DrawSettings::Tool::Rectangle:
        static_cast<QGraphicsRectItem *>(activeDrawingItem)
                ->setRect(QRectF(drawStartScenePos, targetPos).normalized());
        break;
    case DrawSettings::Tool::Ellipse:
        static_cast<QGraphicsEllipseItem *>(activeDrawingItem)
                ->setRect(QRectF(drawStartScenePos, targetPos).normalized());
        break;
    }
}

void QVGraphicsView::finishDrawing(const QPointF &scenePos, bool constrain)
{
    updateDrawing(scenePos, constrain);

    // Discard degenerate shapes (a plain click); pen clicks stay as dots
    const QPointF delta = scenePos - drawStartScenePos;
    const bool degenerate = qAbs(delta.x()) < degenerateDrawingThreshold
            && qAbs(delta.y()) < degenerateDrawingThreshold;

    if (activeDrawingItem && degenerate && drawSettings.tool != DrawSettings::Tool::Pen) {
        delete activeDrawingItem;
    } else if (activeDrawingItem) {
        drawingItems.append(activeDrawingItem);
        emit drawingCountChanged(drawingItems.size());
    }

    activeDrawingItem = nullptr;
    activePenPath = QPainterPath();
    isDrawing = false;
}

QPixmap QVGraphicsView::flattenDrawings() const
{
    QImage out = imageCore.getLoadedPixmap().toImage();
    if (out.isNull())
        return QPixmap();

    if (out.format() == QImage::Format_Indexed8 || out.format() == QImage::Format_Mono
        || out.format() == QImage::Format_MonoLSB) {
        out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    // Render only the annotations on top of the already-copied base image
    const bool pixmapWasVisible = loadedPixmapItem->isVisible();
    loadedPixmapItem->hide();
    cropDimItem->hide();
    cropSelectionItem->hide();

    {
        QPainter painter(&out);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setClipRect(out.rect());
        scene()->render(&painter, QRectF(out.rect()), QRectF(out.rect()));
    }

    loadedPixmapItem->setVisible(pixmapWasVisible);

    return QPixmap::fromImage(out);
}

void QVGraphicsView::clearDrawings()
{
    activeDrawingItem = nullptr;
    activePenPath = QPainterPath();
    isDrawing = false;
    const bool hadItems = !drawingItems.isEmpty();
    qDeleteAll(drawingItems);
    drawingItems.clear();
    if (hadItems)
        emit drawingCountChanged(0);
}

void QVGraphicsView::resetEditState()
{
    if (!isEditing())
        return;

    clearEditBuffers();
    loadedPixmapItem->setScale(1.0);
    clearDrawings();

    cropDragging = false;
    cropDragHandle = CropHandle::None;
    hasCropSelection = false;
    cropDimItem->hide();
    cropSelectionItem->hide();
    viewport()->setMouseTracking(false);

    editMode = EditMode::None;
    setDragMode(QGraphicsView::ScrollHandDrag);
    viewport()->setCursor(Qt::ArrowCursor);

    emit editModeChanged(EditMode::None);
}

void QVGraphicsView::applyBrightnessContrastPreview()
{
    if (editMode != EditMode::Adjust || editPreviewImage.isNull())
        return;

    if (!hasPendingAdjustments()) {
        showPixmap(editBasePixmap);
        return;
    }

    const QPixmap preview = QPixmap::fromImage(QVImageCore::applyBrightnessContrast(
            editPreviewImage, pendingBrightness, pendingContrast, pendingSaturation,
            pendingWarmth));
    const qreal scaleFactor =
            static_cast<qreal>(editBaseImage.width()) / qMax(1, preview.width());
    loadedPixmapItem->setPixmap(preview);
    loadedPixmapItem->setScale(scaleFactor);
}

QVGraphicsView::CropHandles QVGraphicsView::cropHandleAt(const QPointF &scenePos) const
{
    if (!hasCropSelection)
        return CropHandle::None;

    // Grab tolerance is constant in screen pixels, so convert it to scene units
    const qreal scale = qMax(qAbs(transform().m11()), 0.0001);
    const qreal tolerance = cropHandleHitRadius / scale;

    const bool nearLeft = qAbs(scenePos.x() - cropRect.left()) <= tolerance;
    const bool nearRight = qAbs(scenePos.x() - cropRect.right()) <= tolerance;
    const bool nearTop = qAbs(scenePos.y() - cropRect.top()) <= tolerance;
    const bool nearBottom = qAbs(scenePos.y() - cropRect.bottom()) <= tolerance;

    const QRectF grabArea = cropRect.adjusted(-tolerance, -tolerance, tolerance, tolerance);
    if (!grabArea.contains(scenePos))
        return CropHandle::None;

    CropHandles handle = CropHandle::None;

    // Prefer the nearer edge when a tiny selection makes both candidates match
    if (nearLeft && nearRight) {
        handle |= (scenePos.x() - cropRect.left() < cropRect.right() - scenePos.x())
                ? CropHandle::Left
                : CropHandle::Right;
    } else if (nearLeft) {
        handle |= CropHandle::Left;
    } else if (nearRight) {
        handle |= CropHandle::Right;
    }

    if (nearTop && nearBottom) {
        handle |= (scenePos.y() - cropRect.top() < cropRect.bottom() - scenePos.y())
                ? CropHandle::Top
                : CropHandle::Bottom;
    } else if (nearTop) {
        handle |= CropHandle::Top;
    } else if (nearBottom) {
        handle |= CropHandle::Bottom;
    }

    if (handle != CropHandle::None)
        return handle;

    return cropRect.contains(scenePos) ? CropHandle::Move : CropHandle::None;
}

void QVGraphicsView::updateCropCursor(CropHandles handle)
{
    if (editMode != EditMode::Crop)
        return;

    // The view may be mirrored/flipped; swap sides so diagonal cursors match what is shown
    if (transform().m11() < 0 && (handle & (CropHandle::Left | CropHandle::Right)))
        handle ^= (CropHandle::Left | CropHandle::Right);
    if (transform().m22() < 0 && (handle & (CropHandle::Top | CropHandle::Bottom)))
        handle ^= (CropHandle::Top | CropHandle::Bottom);

    Qt::CursorShape shape = Qt::ArrowCursor;
    if (handle == CropHandle::Move) {
        shape = cropDragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
    } else if (handle == (CropHandle::Left | CropHandle::Top)
               || handle == (CropHandle::Right | CropHandle::Bottom)) {
        shape = Qt::SizeFDiagCursor;
    } else if (handle == (CropHandle::Right | CropHandle::Top)
               || handle == (CropHandle::Left | CropHandle::Bottom)) {
        shape = Qt::SizeBDiagCursor;
    } else if (handle == CropHandle::Left || handle == CropHandle::Right) {
        shape = Qt::SizeHorCursor;
    } else if (handle == CropHandle::Top || handle == CropHandle::Bottom) {
        shape = Qt::SizeVerCursor;
    }

    viewport()->setCursor(shape);
}

void QVGraphicsView::setCropAspectRatio(const QSizeF &aspect)
{
    if (editMode != EditMode::Crop || !hasCropSelection)
        return;

    cropAspectRatio = aspect;
    const qreal aspectRatio = effectiveCropAspectRatio(false);
    if (aspectRatio <= 0.0) {
        updateCropOverlay();
        return;
    }

    const QRectF bounds = loadedPixmapItem->boundingRect();
    if (bounds.isEmpty())
        return;

    // Fit the largest box of the requested ratio inside the current selection (or the full
    // bounds if the selection is tiny), centered on the current selection.
    const QRectF base =
            cropRect.width() >= minimumCropAspectBaseSize
                    && cropRect.height() >= minimumCropAspectBaseSize
            ? cropRect
            : bounds;

    qreal width = base.width();
    qreal height = qRound(width / aspectRatio);
    if (height > base.height()) {
        height = base.height();
        width = qRound(height * aspectRatio);
    }
    width = qBound(1.0, static_cast<qreal>(qRound(width)), bounds.width());
    height = qBound(1.0, static_cast<qreal>(qRound(height)), bounds.height());

    const QPointF center = cropRect.center();
    qreal left = qRound(center.x() - width / 2.0);
    qreal top = qRound(center.y() - height / 2.0);
    left = qBound(bounds.left(), left, bounds.right() - width);
    top = qBound(bounds.top(), top, bounds.bottom() - height);

    cropRect = QRectF(left, top, width, height);
    updateCropOverlay();
}

void QVGraphicsView::resetCropRect()
{
    if (editMode != EditMode::Crop)
        return;

    cropAspectRatio = QSizeF();
    cropRect = loadedPixmapItem->boundingRect();
    hasCropSelection = true;
    updateCropOverlay();
}

qreal QVGraphicsView::effectiveCropAspectRatio(bool constrain) const
{
    if (cropAspectRatio.width() < 0.0 && cropAspectRatio.height() < 0.0) {
        const QSize imageSize = getLoadedPixmap().size();
        if (imageSize.width() > 0 && imageSize.height() > 0) {
            return static_cast<qreal>(imageSize.width())
                    / static_cast<qreal>(imageSize.height());
        }
        return 0.0;
    }

    if (cropAspectRatio.width() > 0.0 && cropAspectRatio.height() > 0.0)
        return cropAspectRatio.width() / cropAspectRatio.height();

    if (constrain)
        return 1.0; // Shift in Free mode constrains to a 1:1 square

    return 0.0;
}

void QVGraphicsView::dragCropTo(const QPointF &scenePos, bool constrain)
{
    if (!cropDragging || cropDragHandle == CropHandle::None)
        return;

    const QRectF bounds = loadedPixmapItem->boundingRect();
    // Whole image pixels only, so the overlay always matches the cropped result
    const qreal dx = qRound(scenePos.x() - cropDragStartScene.x());
    const qreal dy = qRound(scenePos.y() - cropDragStartScene.y());
    const qreal aspectRatio = effectiveCropAspectRatio(constrain);

    const bool horizontalHandle =
            cropDragHandle.testFlag(CropHandle::Left) || cropDragHandle.testFlag(CropHandle::Right);
    const bool verticalHandle = cropDragHandle.testFlag(CropHandle::Top)
            || cropDragHandle.testFlag(CropHandle::Bottom);

    QRectF rect = cropRectAtDragStart;
    if (cropDragHandle == CropHandle::Move) {
        rect = dragCropMove(rect, bounds, dx, dy);
    } else if (aspectRatio <= 0.0) {
        rect = dragCropFree(rect, bounds, dx, dy);
    } else if (horizontalHandle && verticalHandle) {
        rect = dragCropCorner(rect, bounds, dx, dy, aspectRatio);
    } else if (horizontalHandle) {
        rect = dragCropHorizontalEdge(rect, bounds, dx, aspectRatio);
    } else if (verticalHandle) {
        rect = dragCropVerticalEdge(rect, bounds, dy, aspectRatio);
    }

    cropRect = rect;
    updateCropOverlay();
}

QRectF QVGraphicsView::dragCropMove(QRectF rect, const QRectF &bounds, qreal dx, qreal dy) const
{
    rect.translate(dx, dy);
    rect.moveLeft(qBound(bounds.left(), rect.left(), bounds.right() - rect.width()));
    rect.moveTop(qBound(bounds.top(), rect.top(), bounds.bottom() - rect.height()));
    return rect;
}

QRectF QVGraphicsView::dragCropFree(QRectF rect, const QRectF &bounds, qreal dx, qreal dy) const
{
    if (cropDragHandle.testFlag(CropHandle::Left))
        rect.setLeft(qBound(bounds.left(), rect.left() + dx, rect.right() - minimumCropSize));
    if (cropDragHandle.testFlag(CropHandle::Right))
        rect.setRight(qBound(rect.left() + minimumCropSize, rect.right() + dx, bounds.right()));
    if (cropDragHandle.testFlag(CropHandle::Top))
        rect.setTop(qBound(bounds.top(), rect.top() + dy, rect.bottom() - minimumCropSize));
    if (cropDragHandle.testFlag(CropHandle::Bottom))
        rect.setBottom(
                qBound(rect.top() + minimumCropSize, rect.bottom() + dy, bounds.bottom()));
    return rect;
}

QRectF QVGraphicsView::dragCropCorner(QRectF rect, const QRectF &bounds, qreal dx, qreal dy,
                                      qreal aspectRatio) const
{
    const bool dragLeft = cropDragHandle.testFlag(CropHandle::Left);
    const bool dragTop = cropDragHandle.testFlag(CropHandle::Top);

    // Anchor the opposite corner
    const qreal anchorX = dragLeft ? rect.right() : rect.left();
    const qreal anchorY = dragTop ? rect.bottom() : rect.top();
    const qreal maxWidth = dragLeft ? (anchorX - bounds.left()) : (bounds.right() - anchorX);
    const qreal maxHeight = dragTop ? (anchorY - bounds.top()) : (bounds.bottom() - anchorY);

    const qreal rawWidth = dragLeft ? (rect.width() - dx) : (rect.width() + dx);
    const qreal rawHeight = dragTop ? (rect.height() - dy) : (rect.height() + dy);

    qreal width = qMax(minimumCropSize, qMax(rawWidth, rawHeight * aspectRatio));
    qreal height = width / aspectRatio;
    if (width > maxWidth) {
        width = maxWidth;
        height = width / aspectRatio;
    }
    if (height > maxHeight) {
        height = maxHeight;
        width = height * aspectRatio;
    }
    width = qMax(minimumCropSize, static_cast<qreal>(qRound(width)));
    height = qMax(minimumCropSize, static_cast<qreal>(qRound(height)));

    const qreal left = dragLeft ? (anchorX - width) : anchorX;
    const qreal top = dragTop ? (anchorY - height) : anchorY;
    return QRectF(left, top, width, height);
}

QRectF QVGraphicsView::dragCropHorizontalEdge(QRectF rect, const QRectF &bounds, qreal dx,
                                              qreal aspectRatio) const
{
    const bool dragLeft = cropDragHandle.testFlag(CropHandle::Left);

    // Scale height symmetrically around the vertical center
    const qreal anchorX = dragLeft ? rect.right() : rect.left();
    const qreal centerY = rect.center().y();
    const qreal maxWidthByX = dragLeft ? (anchorX - bounds.left()) : (bounds.right() - anchorX);
    const qreal maxHeightByY = 2.0 * qMin(centerY - bounds.top(), bounds.bottom() - centerY);
    const qreal maxWidth = qMin(maxWidthByX, maxHeightByY * aspectRatio);

    const qreal rawWidth = dragLeft ? (rect.width() - dx) : (rect.width() + dx);
    const qreal width = qBound(minimumCropSize, static_cast<qreal>(qRound(rawWidth)),
                               qMax(minimumCropSize, static_cast<qreal>(qRound(maxWidth))));
    const qreal height = qMax(minimumCropSize, static_cast<qreal>(qRound(width / aspectRatio)));
    qreal top = qRound(centerY - height / 2.0);
    top = qBound(bounds.top(), top, bounds.bottom() - height);
    const qreal left = dragLeft ? (anchorX - width) : anchorX;
    return QRectF(left, top, width, height);
}

QRectF QVGraphicsView::dragCropVerticalEdge(QRectF rect, const QRectF &bounds, qreal dy,
                                            qreal aspectRatio) const
{
    const bool dragTop = cropDragHandle.testFlag(CropHandle::Top);

    // Scale width symmetrically around the horizontal center
    const qreal anchorY = dragTop ? rect.bottom() : rect.top();
    const qreal centerX = rect.center().x();
    const qreal maxHeightByY = dragTop ? (anchorY - bounds.top()) : (bounds.bottom() - anchorY);
    const qreal maxWidthByX = 2.0 * qMin(centerX - bounds.left(), bounds.right() - centerX);
    const qreal maxHeight = qMin(maxHeightByY, maxWidthByX / aspectRatio);

    const qreal rawHeight = dragTop ? (rect.height() - dy) : (rect.height() + dy);
    const qreal height = qBound(minimumCropSize, static_cast<qreal>(qRound(rawHeight)),
                                qMax(minimumCropSize, static_cast<qreal>(qRound(maxHeight))));
    const qreal width = qMax(minimumCropSize, static_cast<qreal>(qRound(height * aspectRatio)));
    qreal left = qRound(centerX - width / 2.0);
    left = qBound(bounds.left(), left, bounds.right() - width);
    const qreal top = dragTop ? (anchorY - height) : anchorY;
    return QRectF(left, top, width, height);
}

void QVGraphicsView::updateCropOverlay()
{
    cropSelectionItem->setRect(cropRect);
    cropOutlineItem->setRect(cropRect);

    // Rule-of-thirds composition lines
    QPainterPath gridPath;
    if (cropRect.width() >= cropGridMinimumSize && cropRect.height() >= cropGridMinimumSize) {
        const qreal x1 = cropRect.left() + cropRect.width() / 3.0;
        const qreal x2 = cropRect.left() + (cropRect.width() * 2.0) / 3.0;
        const qreal y1 = cropRect.top() + cropRect.height() / 3.0;
        const qreal y2 = cropRect.top() + (cropRect.height() * 2.0) / 3.0;

        gridPath.moveTo(x1, cropRect.top());
        gridPath.lineTo(x1, cropRect.bottom());
        gridPath.moveTo(x2, cropRect.top());
        gridPath.lineTo(x2, cropRect.bottom());
        gridPath.moveTo(cropRect.left(), y1);
        gridPath.lineTo(cropRect.right(), y1);
        gridPath.moveTo(cropRect.left(), y2);
        gridPath.lineTo(cropRect.right(), y2);
    }
    cropGridItem->setPath(gridPath);
    cropGridOutlineItem->setPath(gridPath);

    cropSelectionItem->show();
    updateCropDimOverlay(cropRect);

    // Corners and edge midpoints
    const QPointF center = cropRect.center();
    const std::array<QPointF, 8> points = {
        { cropRect.topLeft(),
          QPointF(center.x(), cropRect.top()),
          cropRect.topRight(),
          QPointF(cropRect.right(), center.y()),
          cropRect.bottomRight(),
          QPointF(center.x(), cropRect.bottom()),
          cropRect.bottomLeft(),
          QPointF(cropRect.left(), center.y()) }
    };

    for (std::size_t i = 0; i < points.size(); ++i)
        cropHandleItems.at(i)->setPos(cropSelectionItem->mapFromScene(points.at(i)));

    emit cropRectChanged(cropPixelRect(), getLoadedPixmap().size());
}

void QVGraphicsView::updateCropDimOverlay(const QRectF &selection)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    path.addRect(loadedPixmapItem->boundingRect());
    path.addRect(selection);

    cropDimItem->setPath(path);
    cropDimItem->show();
}

QRect QVGraphicsView::cropPixelRect() const
{
    if (!hasCropSelection)
        return {};

    return QVImageCore::clampCropRect(cropRect.toAlignedRect(), getLoadedPixmap().size());
}
