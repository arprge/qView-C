#include "qveditpanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>

QVEditPanel::QVEditPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("editPanel");
    setFocusPolicy(Qt::NoFocus);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 4, 8, 4);

    stack = new QStackedWidget(this);
    stack->setFocusPolicy(Qt::NoFocus);
    rootLayout->addWidget(stack);

    // Crop page
    cropPage = new QWidget(stack);
    auto *cropLayout = new QHBoxLayout(cropPage);
    cropLayout->setContentsMargins(0, 0, 0, 0);
    auto *cropHint = new QLabel(
            tr("Drag to select an area · Enter to save a copy · Ctrl+Enter to replace · Esc to "
               "cancel"),
            cropPage);
    cropHint->setFocusPolicy(Qt::NoFocus);
    cropLayout->addStretch();
    cropLayout->addWidget(cropHint);
    cropLayout->addStretch();
    stack->addWidget(cropPage);

    // Adjust page
    adjustPage = new QWidget(stack);
    auto *adjustLayout = new QHBoxLayout(adjustPage);
    adjustLayout->setContentsMargins(0, 0, 0, 0);

    auto *brightnessLabel = new QLabel(tr("Brightness"), adjustPage);
    brightnessLabel->setFocusPolicy(Qt::NoFocus);

    brightnessSlider = new QSlider(Qt::Horizontal, adjustPage);
    brightnessSlider->setRange(-100, 100);
    brightnessSlider->setValue(0);
    brightnessSlider->setFocusPolicy(Qt::NoFocus);

    brightnessValueLabel = new QLabel(QString::number(brightnessSlider->value()), adjustPage);
    brightnessValueLabel->setMinimumWidth(30);
    brightnessValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    brightnessValueLabel->setFocusPolicy(Qt::NoFocus);

    auto *contrastLabel = new QLabel(tr("Contrast"), adjustPage);
    contrastLabel->setFocusPolicy(Qt::NoFocus);

    contrastSlider = new QSlider(Qt::Horizontal, adjustPage);
    contrastSlider->setRange(-100, 100);
    contrastSlider->setValue(0);
    contrastSlider->setFocusPolicy(Qt::NoFocus);

    contrastValueLabel = new QLabel(QString::number(contrastSlider->value()), adjustPage);
    contrastValueLabel->setMinimumWidth(30);
    contrastValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    contrastValueLabel->setFocusPolicy(Qt::NoFocus);

    resetButton = new QPushButton(tr("Reset"), adjustPage);
    resetButton->setFocusPolicy(Qt::NoFocus);

    adjustLayout->addWidget(brightnessLabel);
    adjustLayout->addWidget(brightnessSlider, 1);
    adjustLayout->addWidget(brightnessValueLabel);
    adjustLayout->addSpacing(12);
    adjustLayout->addWidget(contrastLabel);
    adjustLayout->addWidget(contrastSlider, 1);
    adjustLayout->addWidget(contrastValueLabel);
    adjustLayout->addSpacing(12);
    adjustLayout->addWidget(resetButton);
    stack->addWidget(adjustPage);

    // Draw page placeholder (drawing tools are added in a later phase)
    drawPage = new QWidget(stack);
    auto *drawLayout = new QHBoxLayout(drawPage);
    drawLayout->setContentsMargins(0, 0, 0, 0);
    auto *drawHint = new QLabel(tr("Drawing tools are not available yet."), drawPage);
    drawHint->setFocusPolicy(Qt::NoFocus);
    drawLayout->addStretch();
    drawLayout->addWidget(drawHint);
    drawLayout->addStretch();
    stack->addWidget(drawPage);

    connect(brightnessSlider, &QSlider::valueChanged, this, [this](int value) {
        brightnessValueLabel->setText(QString::number(value));
        emit brightnessContrastChanged(brightnessSlider->value(), contrastSlider->value());
    });
    connect(contrastSlider, &QSlider::valueChanged, this, [this](int value) {
        contrastValueLabel->setText(QString::number(value));
        emit brightnessContrastChanged(brightnessSlider->value(), contrastSlider->value());
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        {
            const QSignalBlocker brightnessBlocker(brightnessSlider);
            const QSignalBlocker contrastBlocker(contrastSlider);
            brightnessSlider->setValue(0);
            contrastSlider->setValue(0);
        }
        updateAdjustLabels();
        emit brightnessContrastChanged(0, 0);
    });

    stack->setCurrentWidget(cropPage);
}

void QVEditPanel::setMode(QVGraphicsView::EditMode mode)
{
    switch (mode) {
    case QVGraphicsView::EditMode::Crop: {
        stack->setCurrentWidget(cropPage);
        break;
    }
    case QVGraphicsView::EditMode::Adjust: {
        {
            const QSignalBlocker brightnessBlocker(brightnessSlider);
            const QSignalBlocker contrastBlocker(contrastSlider);
            brightnessSlider->setValue(0);
            contrastSlider->setValue(0);
        }
        updateAdjustLabels();
        stack->setCurrentWidget(adjustPage);
        break;
    }
    case QVGraphicsView::EditMode::Draw: {
        stack->setCurrentWidget(drawPage);
        break;
    }
    case QVGraphicsView::EditMode::None:
        break;
    }
}

int QVEditPanel::brightness() const
{
    return brightnessSlider->value();
}

int QVEditPanel::contrast() const
{
    return contrastSlider->value();
}

void QVEditPanel::updateAdjustLabels()
{
    brightnessValueLabel->setText(QString::number(brightnessSlider->value()));
    contrastValueLabel->setText(QString::number(contrastSlider->value()));
}
