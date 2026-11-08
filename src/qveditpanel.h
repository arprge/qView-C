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

    QVGraphicsView::DrawSettings drawSettings() const { return currentDrawSettings; }

signals:
    void brightnessContrastChanged(int brightness, int contrast);
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
    QLabel *brightnessValueLabel;
    QLabel *contrastValueLabel;
    QPushButton *resetButton;

    QVGraphicsView::DrawSettings currentDrawSettings;
    QPushButton *colorButton;
};

#endif // QVEDITPANEL_H
