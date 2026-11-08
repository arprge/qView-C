#ifndef QVEDITPANEL_H
#define QVEDITPANEL_H

#include "qvgraphicsview.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;

class QVEditPanel : public QWidget
{
    Q_OBJECT

public:
    explicit QVEditPanel(QWidget *parent = nullptr);

    void setMode(QVGraphicsView::EditMode mode);

    int brightness() const;
    int contrast() const;

signals:
    void brightnessContrastChanged(int brightness, int contrast);

private:
    void updateAdjustLabels();

    QStackedWidget *stack;

    QWidget *cropPage;
    QWidget *adjustPage;
    QWidget *drawPage;

    QSlider *brightnessSlider;
    QSlider *contrastSlider;
    QLabel *brightnessValueLabel;
    QLabel *contrastValueLabel;
    QPushButton *resetButton;
};

#endif // QVEDITPANEL_H
