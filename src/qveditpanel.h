#ifndef QVEDITPANEL_H
#define QVEDITPANEL_H

#include "qvgraphicsview.h"

#include <QColor>
#include <QIcon>
#include <QList>
#include <QWidget>

class QGridLayout;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QToolButton;

class QVEditPanel : public QWidget
{
    Q_OBJECT

public:
    explicit QVEditPanel(QWidget *parent = nullptr);

    void setMode(QVGraphicsView::EditMode mode);
    void setCropDimensions(const QRect &rect, const QSize &imageSize);
    void setDrawHistoryCount(int count);

    int brightness() const;
    int contrast() const;
    int saturation() const;
    int warmth() const;

    QVGraphicsView::DrawSettings drawSettings() const { return currentDrawSettings; }

signals:
    void cropAspectRatioChanged(const QSizeF &aspect);
    void resetCropRequested();
    void brightnessContrastChanged(int brightness, int contrast, int saturation, int warmth);
    void compareOriginalChanged(bool showOriginal);
    void drawSettingsChanged(const QVGraphicsView::DrawSettings &settings);
    void undoDrawingRequested();
    void clearDrawingsRequested();

protected:
    void changeEvent(QEvent *event) override;

private:
    struct AdjustControl
    {
        QSlider *slider = nullptr;
        QLabel *valueLabel = nullptr;
    };

    enum class ToolIcon { Pen, Line, Rectangle, Ellipse };

    void createCropPage();
    void createAdjustPage();
    void createDrawPage();
    void createAdjustControl(QGridLayout *grid, int row, int columnGroup,
                             const QString &labelText, AdjustControl &control);

    void resetAdjustSliders();
    void emitAdjustChanged();
    void updateCropHintStyle();
    void updateAdjustLabels();
    void updateAdjustReadoutWidths();
    void updateThemeIcons();
    void updateColorButtons();

    static QIcon createToolIcon(ToolIcon type, const QColor &color);

    QStackedWidget *stack = nullptr;

    QWidget *cropPage = nullptr;
    QWidget *adjustPage = nullptr;
    QWidget *drawPage = nullptr;

    QList<QToolButton *> aspectButtons;
    QLabel *cropSizeLabel = nullptr;
    QLabel *cropHint = nullptr;
    QPushButton *cropResetButton = nullptr;

    AdjustControl brightnessControl;
    AdjustControl contrastControl;
    AdjustControl saturationControl;
    AdjustControl warmthControl;
    QPushButton *compareButton = nullptr;
    QPushButton *resetButton = nullptr;

    QVGraphicsView::DrawSettings currentDrawSettings;
    QList<QToolButton *> toolButtons;
    QList<ToolIcon> toolIconTypes;
    QList<QToolButton *> swatchButtons;
    QList<QColor> presetColors;
    QToolButton *customColorButton = nullptr;
    QSlider *widthSlider = nullptr;
    QSpinBox *widthSpin = nullptr;
    QPushButton *undoButton = nullptr;
    QPushButton *clearButton = nullptr;
};

#endif // QVEDITPANEL_H
