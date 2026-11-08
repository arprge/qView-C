#ifndef QVEDITPANEL_H
#define QVEDITPANEL_H

#include "qvgraphicsview.h"

#include <QColor>
#include <QList>
#include <QWidget>

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
    void setDrawHistoryCount(int count);

    int brightness() const;
    int contrast() const;
    int saturation() const;
    int warmth() const;

    QVGraphicsView::DrawSettings drawSettings() const { return currentDrawSettings; }

signals:
    void brightnessContrastChanged(int brightness, int contrast, int saturation, int warmth);
    void compareOriginalChanged(bool showOriginal);
    void drawSettingsChanged(const QVGraphicsView::DrawSettings &settings);
    void undoDrawingRequested();
    void clearDrawingsRequested();

protected:
    void changeEvent(QEvent *event) override;

private:
    void updateAdjustLabels();
    void updateAdjustReadoutWidths();
    void updateThemeIcons();
    void updateColorButtons();

    QStackedWidget *stack = nullptr;

    QWidget *cropPage = nullptr;
    QWidget *adjustPage = nullptr;
    QWidget *drawPage = nullptr;

    QSlider *brightnessSlider = nullptr;
    QSlider *contrastSlider = nullptr;
    QSlider *saturationSlider = nullptr;
    QSlider *warmthSlider = nullptr;
    QLabel *brightnessValueLabel = nullptr;
    QLabel *contrastValueLabel = nullptr;
    QLabel *saturationValueLabel = nullptr;
    QLabel *warmthValueLabel = nullptr;
    QPushButton *compareButton = nullptr;
    QPushButton *resetButton = nullptr;

    QVGraphicsView::DrawSettings currentDrawSettings;
    QList<QToolButton *> toolButtons;
    QList<QToolButton *> swatchButtons;
    QList<QColor> presetColors;
    QToolButton *customColorButton = nullptr;
    QSlider *widthSlider = nullptr;
    QSpinBox *widthSpin = nullptr;
    QPushButton *undoButton = nullptr;
    QPushButton *clearButton = nullptr;
};

#endif // QVEDITPANEL_H
