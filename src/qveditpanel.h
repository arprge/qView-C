#ifndef QVEDITPANEL_H
#define QVEDITPANEL_H

#include "qvgraphicsview.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

class QVEditPanel : public QWidget
{
    Q_OBJECT

public:
    explicit QVEditPanel(QWidget *parent = nullptr);

    void setMode(QVGraphicsView::EditMode mode);

    int brightness() const;
    int contrast() const;
    int saturation() const;
    int warmth() const;

    QVGraphicsView::DrawSettings drawSettings() const { return currentDrawSettings; }

signals:
    void brightnessContrastChanged(int brightness, int contrast, int saturation, int warmth);
    void drawSettingsChanged(const QVGraphicsView::DrawSettings &settings);
    void undoDrawingRequested();

private:
    void updateAdjustLabels();
    void updateColorButton();

    QStackedWidget *stack;

    QWidget *cropPage;
    QWidget *adjustPage;
    QWidget *drawPage;

    QSlider *brightnessSlider;
    QSlider *contrastSlider;
    QSlider *saturationSlider;
    QSlider *warmthSlider;
    QLabel *brightnessValueLabel;
    QLabel *contrastValueLabel;
    QLabel *saturationValueLabel;
    QLabel *warmthValueLabel;
    QPushButton *resetButton;

    QVGraphicsView::DrawSettings currentDrawSettings;
    QPushButton *colorButton;
};

#endif // QVEDITPANEL_H
