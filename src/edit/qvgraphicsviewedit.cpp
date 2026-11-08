#include "qvgraphicsview.h"
#include "qvapplication.h"
#include "settingsmanager.h"

#include <array>

#include <QColor>
#include <QCursor>
#include <QEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QtMath>

namespace {

constexpr int maxAdjustment = 100;
constexpr int maxPreviewDimension = 1920;

constexpr qreal cropHandleHitRadius = 12.0;
constexpr qreal minimumCropSize = 1.0;
// Note: a default-constructed QSizeF is (-1, -1), which means "Original aspect", not "Free"
const QSizeF freeCropAspectRatio(0.0, 0.0);
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

void QVGraphicsView::setupEditOverlay()
{
    cropDimItem = new QGraphicsPathItem();
    cropDimItem->setBrush(QColor(0, 0, 0, 110));
    cropDimItem->setPen(Qt::NoPen);
    cropDimItem->setZValue(10);
    cropDimItem->setAcceptedMouseButtons(Qt::NoButton);
    cropDimItem->hide();
    scene()->addItem(cropDimItem);

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
    scene()->addItem(cropSelectionItem);

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
    connect(adjustPreviewTimer, &QTimer::timeout, this, &QVGraphicsView::applyAdjustPreview);
}

void QVGraphicsView::updateEditEnterCursor()
{
    if (editMode == EditMode::Draw)
        viewport()->setCursor(Qt::CrossCursor);
    else if (editMode == EditMode::Crop)
        updateCropCursor(cropHandleAt(mapToScene(mapFromGlobal(QCursor::pos()))));
    else
        viewport()->setCursor(Qt::ArrowCursor);
}

bool QVGraphicsView::handleEditMousePress(QMouseEvent *event)
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
        return true;
    }

    if (editMode == EditMode::Draw && event->button() == Qt::LeftButton) {
        beginDrawing(mapToScene(event->pos()));
        event->accept();
        return true;
    }

    return false;
}

bool QVGraphicsView::handleEditMouseMove(QMouseEvent *event)
{
    if (editMode == EditMode::Crop && event->buttons() == Qt::NoButton)
        updateCropCursor(cropHandleAt(mapToScene(event->pos())));

    if (editMode == EditMode::Crop && cropDragging) {
        dragCropTo(mapToScene(event->pos()), event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
        return true;
    }

    if (editMode == EditMode::Draw && isDrawing) {
        updateDrawing(mapToScene(event->pos()), event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
        return true;
    }

    return false;
}

bool QVGraphicsView::handleEditMouseRelease(QMouseEvent *event)
{
    if (editMode == EditMode::Crop && event->button() == Qt::LeftButton) {
        if (cropDragging)
            dragCropTo(mapToScene(event->pos()), event->modifiers().testFlag(Qt::ShiftModifier));
        cropDragging = false;
        cropDragHandle = CropHandle::None;
        updateCropCursor(cropHandleAt(mapToScene(event->pos())));
        event->accept();
        return true;
    }

    if (editMode == EditMode::Draw && event->button() == Qt::LeftButton && isDrawing) {
        finishDrawing(mapToScene(event->pos()), event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
        return true;
    }

    return false;
}

bool QVGraphicsView::handleEditShortcutOverride(QEvent *event)
{
    if (event->type() != QEvent::ShortcutOverride || !isEditing())
        return false;

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    const bool isEnterKey =
            keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;
    if (isEnterKey || keyEvent->key() == Qt::Key_Escape
        || (editMode == EditMode::Draw && keyEvent->matches(QKeySequence::Undo))) {
        keyEvent->accept();
        return true;
    }

    return false;
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

bool QVGraphicsView::beginEdit(EditMode mode)
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
    cropAspectRatio = freeCropAspectRatio;
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
    const QPixmap finalPixmap = QPixmap::fromImage(QVImageCore::applyAdjustments(
            editBaseImage, pendingBrightness, pendingContrast, pendingSaturation, pendingWarmth));
    imageCore.setLoadedPixmap(finalPixmap);
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

    imageCore.setLoadedPixmap(flattened);
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
        imageCore.setLoadedPixmap(editBasePixmap);
        showPixmap(editBasePixmap);
    }

    resetEditState();

    if (qvGetSettingBool(ScalingEnabled) && !isOriginalSize)
        expensiveScaleTimerNew->start();
}

void QVGraphicsView::setAdjustPreview(int brightness, int contrast, int saturation, int warmth)
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
        applyAdjustPreview();
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

void QVGraphicsView::applyAdjustPreview()
{
    if (editMode != EditMode::Adjust || editPreviewImage.isNull())
        return;

    if (!hasPendingAdjustments()) {
        showPixmap(editBasePixmap);
        return;
    }

    const QPixmap preview = QPixmap::fromImage(QVImageCore::applyAdjustments(
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

    cropAspectRatio = freeCropAspectRatio;
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
