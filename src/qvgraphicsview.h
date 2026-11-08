#ifndef QVGRAPHICSVIEW_H
#define QVGRAPHICSVIEW_H

#include "qvimagecore.h"
#include <QGraphicsView>
#include <QImageReader>
#include <QMimeData>
#include <QDir>
#include <QTimer>
#include <QFileInfo>
#include <QColor>
#include <QPainterPath>
#include <QPen>
#include <QList>

class QGraphicsPathItem;
class QGraphicsRectItem;
class QKeyEvent;

class QVGraphicsView : public QGraphicsView
{
    Q_OBJECT

public:
    QVGraphicsView(QWidget *parent = nullptr);

    enum class ScaleMode { resetScale, zoom };
    Q_ENUM(ScaleMode)

    enum class GoToFileMode { constant, first, previous, next, last, random };
    Q_ENUM(GoToFileMode)

    enum class EditMode { None, Crop, Adjust, Draw };
    Q_ENUM(EditMode)

    enum class CommitAction { SaveCopy, ReplaceOriginal };
    Q_ENUM(CommitAction)

    struct DrawSettings
    {
        enum class Tool { Pen, Line, Rectangle, Ellipse };
        Tool tool = Tool::Pen;
        QColor color = Qt::red;
        int width = 4;
    };

    QMimeData *getMimeData() const;
    void loadMimeData(const QMimeData *mimeData);
    void loadFile(const QString &fileName);

    void reloadFile();

    void zoomIn(const QPoint &pos = QPoint(-1, -1));

    void zoomOut(const QPoint &pos = QPoint(-1, -1));

    void zoom(qreal scaleFactor, const QPoint &pos = QPoint(-1, -1));

    void scaleExpensively();
    void makeUnscaled();

    void resetScale();
    void originalSize();

    void goToFile(const GoToFileMode &mode, int index = 0);

    void settingsUpdated();

    void closeImage();
    void jumpToNextFrame();
    void setPaused(const bool &desiredState);
    void setSpeed(const int &desiredSpeed);
    void rotateImage(int rotation);

    bool beginEdit(const EditMode &mode);
    bool applyEdit();
    void cancelEdit();
    bool isEditing() const { return editMode != EditMode::None; }
    EditMode getEditMode() const { return editMode; }

    void setBrightnessContrastPreview(int brightness, int contrast, int saturation = 0,
                                      int warmth = 0);

    void setDrawSettings(const DrawSettings &settings) { drawSettings = settings; }
    const DrawSettings &getDrawSettings() const { return drawSettings; }
    void undoLastDrawing();
    void clearDrawings();

    bool saveCopy(const QString &filePath, QString *errorString = nullptr) const
    {
        return imageCore.saveCopy(filePath, errorString);
    }
    bool replaceCurrentFile(QString *errorString = nullptr)
    {
        return imageCore.replaceCurrentFile(errorString);
    }
    void markCurrentFileSaved() { imageCore.markCurrentFileSaved(); }

    const QVImageCore::FileDetails &getCurrentFileDetails() const
    {
        return imageCore.getCurrentFileDetails();
    }
    const QPixmap &getLoadedPixmap() const { return imageCore.getLoadedPixmap(); }
    const QMovie &getLoadedMovie() const { return imageCore.getLoadedMovie(); }

signals:
    void cancelSlideshow();

    void fileChanged();

    void updatedLoadedPixmapItem();

    void editModeChanged(QVGraphicsView::EditMode mode);

    void commitRequested(QVGraphicsView::CommitAction action);

protected:
    void wheelEvent(QWheelEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    void dropEvent(QDropEvent *event) override;

    void dragEnterEvent(QDragEnterEvent *event) override;

    void dragMoveEvent(QDragMoveEvent *event) override;

    void dragLeaveEvent(QDragLeaveEvent *event) override;

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEvent *event) override;
#else
    void enterEvent(QEnterEvent *event) override;
#endif

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;

    bool event(QEvent *event) override;

    void fitInViewMarginless(const QRectF &rect);
    void fitInViewMarginless(const QGraphicsItem *item);

    void centerOn(const QPointF &pos);

    void centerOn(qreal x, qreal y);

    void centerOn(const QGraphicsItem *item);

private slots:
    void animatedFrameChanged(QRect rect);

    void postLoad();

    void updateLoadedPixmapItem();

private:
    void updateFilteringMode();
    void resetEditState();
    void applyBrightnessContrastPreview();
    // Crop border handling: handles are bit flags (a corner is two edges combined)
    enum CropHandle {
        HandleNone = 0,
        HandleLeft = 1,
        HandleRight = 2,
        HandleTop = 4,
        HandleBottom = 8,
        HandleMove = 16
    };
    int cropHandleAt(const QPointF &scenePos) const;
    void updateCropCursor(int handle);
    void dragCropTo(const QPointF &scenePos);
    void updateCropOverlay();
    void updateCropDimOverlay(const QRectF &selection);
    QRect cropPixelRect() const;

    QGraphicsPixmapItem *loadedPixmapItem;

    constexpr static int MARGIN = -2;
    constexpr static qreal MAX_EXPENSIVE_SCALING_SIZE = 3;

    // Set to too high a value to activate for now...
    constexpr static qreal MAX_FILTERING_SIZE = 5000;

    qreal currentScale;
    QSize scaledSize;
    bool isOriginalSize;
    QPoint lastZoomEventPos;
    QPointF lastZoomRoundingError;
    QPointF lastScrollRoundingError;

    QTransform absoluteTransform;
    QTransform zoomBasis;
    qreal zoomBasisScaleFactor;

    QVImageCore imageCore{ this };

    QTimer *expensiveScaleTimerNew;
    QPointF centerPoint;
    Qt::MouseButton mousePressButton;
    Qt::KeyboardModifiers mousePressModifiers;
    QPoint mousePressPosition;

    // Edit mode state
    EditMode editMode = EditMode::None;

    // Crop state
    QGraphicsPathItem *cropDimItem = nullptr;
    QGraphicsRectItem *cropSelectionItem = nullptr;
    QRectF cropRect;               // current selection in scene (== image pixel) coordinates
    QRectF cropRectAtDragStart;
    QPointF cropDragStartScene;
    int cropDragHandle = HandleNone;
    QGraphicsRectItem *cropHandleItems[8] = {};
    QGraphicsRectItem *cropOutlineItem = nullptr;
    bool cropDragging = false;
    bool hasCropSelection = false;

    // Brightness/contrast state
    QTimer *adjustPreviewTimer = nullptr;
    QPixmap editBasePixmap;
    // Cached once per edit session so slider ticks don't re-convert the pixmap
    QImage editBaseImage;
    // Downscaled working copy (~2 MP max) so slider dragging stays instant on huge photos
    QImage editPreviewImage;
    int pendingBrightness = 0;
    int pendingContrast = 0;
    int pendingSaturation = 0;
    int pendingWarmth = 0;

    // Draw state (annotations live in scene coordinates == image pixels while editing)
    DrawSettings drawSettings;
    QList<QGraphicsItem *> drawingItems;
    QGraphicsItem *activeDrawingItem = nullptr;
    QPainterPath activePenPath;
    QPointF drawStartScenePos;
    bool isDrawing = false;

    void beginDrawing(const QPointF &scenePos);
    void updateDrawing(const QPointF &scenePos);
    void finishDrawing(const QPointF &scenePos);
    QPen makeDrawingPen() const;
    QPixmap flattenDrawings() const;
};
#endif // QVGRAPHICSVIEW_H
