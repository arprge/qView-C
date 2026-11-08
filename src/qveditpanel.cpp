#include "qveditpanel.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

QVEditPanel::QVEditPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("editPanel");
    setFocusPolicy(Qt::NoFocus);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 2, 6, 2);
    rootLayout->setSpacing(2);

    stack = new QStackedWidget(this);
    stack->setFocusPolicy(Qt::NoFocus);
    rootLayout->addWidget(stack);

    // Crop page
    cropPage = new QWidget(stack);
    auto *cropLayout = new QHBoxLayout(cropPage);
    cropLayout->setContentsMargins(0, 0, 0, 0);
    cropLayout->setSpacing(6);
    auto *cropHint = new QLabel(
            tr("Drag the borders or corners to crop, drag inside to move · Enter to save a "
               "copy · Ctrl+Enter to replace · Esc to cancel"),
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
    adjustLayout->setSpacing(6);

    const auto addAdjustSlider = [this, adjustLayout](const QString &labelText, QSlider *&slider,
                                                      QLabel *&valueLabel) {
        auto *label = new QLabel(labelText, adjustPage);
        label->setFocusPolicy(Qt::NoFocus);

        slider = new QSlider(Qt::Horizontal, adjustPage);
        slider->setRange(-100, 100);
        slider->setValue(0);
        slider->setFocusPolicy(Qt::NoFocus);

        valueLabel = new QLabel(QString::number(slider->value()), adjustPage);
        valueLabel->setMinimumWidth(26);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel->setFocusPolicy(Qt::NoFocus);

        adjustLayout->addWidget(label);
        adjustLayout->addWidget(slider, 1);
        adjustLayout->addWidget(valueLabel);
        adjustLayout->addSpacing(6);
    };

    addAdjustSlider(tr("Brightness"), brightnessSlider, brightnessValueLabel);
    addAdjustSlider(tr("Contrast"), contrastSlider, contrastValueLabel);
    addAdjustSlider(tr("Saturation"), saturationSlider, saturationValueLabel);
    addAdjustSlider(tr("Warmth"), warmthSlider, warmthValueLabel);

    resetButton = new QPushButton(tr("Reset"), adjustPage);
    resetButton->setFocusPolicy(Qt::NoFocus);
    adjustLayout->addWidget(resetButton);
    stack->addWidget(adjustPage);

    // Draw page
    drawPage = new QWidget(stack);
    auto *drawLayout = new QHBoxLayout(drawPage);
    drawLayout->setContentsMargins(0, 0, 0, 0);
    drawLayout->setSpacing(4);

    auto *toolGroup = new QButtonGroup(drawPage);
    const struct
    {
        QString text;
        QVGraphicsView::DrawSettings::Tool tool;
    } tools[] = {
        { tr("Pen"), QVGraphicsView::DrawSettings::Tool::Pen },
        { tr("Line"), QVGraphicsView::DrawSettings::Tool::Line },
        { tr("Rectangle"), QVGraphicsView::DrawSettings::Tool::Rectangle },
        { tr("Ellipse"), QVGraphicsView::DrawSettings::Tool::Ellipse },
    };
    for (const auto &entry : tools) {
        auto *button = new QPushButton(entry.text, drawPage);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setChecked(entry.tool == currentDrawSettings.tool);
        toolGroup->addButton(button);
        drawLayout->addWidget(button);
        const auto tool = entry.tool;
        connect(button, &QPushButton::clicked, this, [this, tool]() {
            currentDrawSettings.tool = tool;
            emit drawSettingsChanged(currentDrawSettings);
        });
    }

    colorButton = new QPushButton(tr("Color"), drawPage);
    colorButton->setFocusPolicy(Qt::NoFocus);
    updateColorButton();
    connect(colorButton, &QPushButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(currentDrawSettings.color, this,
                                                    tr("Select Color"),
                                                    QColorDialog::ShowAlphaChannel);
        if (!color.isValid())
            return;
        currentDrawSettings.color = color;
        updateColorButton();
        emit drawSettingsChanged(currentDrawSettings);
    });

    auto *widthLabel = new QLabel(tr("Width"), drawPage);
    widthLabel->setFocusPolicy(Qt::NoFocus);
    auto *widthSpin = new QSpinBox(drawPage);
    widthSpin->setRange(1, 50);
    widthSpin->setValue(currentDrawSettings.width);
    widthSpin->setSuffix(tr(" px"));
    widthSpin->setFocusPolicy(Qt::NoFocus);
    connect(widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        currentDrawSettings.width = value;
        emit drawSettingsChanged(currentDrawSettings);
    });

    auto *undoButton = new QPushButton(tr("Undo"), drawPage);
    undoButton->setFocusPolicy(Qt::NoFocus);
    connect(undoButton, &QPushButton::clicked, this, &QVEditPanel::undoDrawingRequested);

    drawLayout->addSpacing(12);
    drawLayout->addWidget(colorButton);
    drawLayout->addWidget(widthLabel);
    drawLayout->addWidget(widthSpin);
    drawLayout->addStretch();
    drawLayout->addWidget(undoButton);
    stack->addWidget(drawPage);

    const auto emitAdjustChanged = [this]() {
        updateAdjustLabels();
        emit brightnessContrastChanged(brightnessSlider->value(), contrastSlider->value(),
                                       saturationSlider->value(), warmthSlider->value());
    };
    connect(brightnessSlider, &QSlider::valueChanged, this, emitAdjustChanged);
    connect(contrastSlider, &QSlider::valueChanged, this, emitAdjustChanged);
    connect(saturationSlider, &QSlider::valueChanged, this, emitAdjustChanged);
    connect(warmthSlider, &QSlider::valueChanged, this, emitAdjustChanged);

    connect(resetButton, &QPushButton::clicked, this, [this]() {
        {
            const QSignalBlocker brightnessBlocker(brightnessSlider);
            const QSignalBlocker contrastBlocker(contrastSlider);
            const QSignalBlocker saturationBlocker(saturationSlider);
            const QSignalBlocker warmthBlocker(warmthSlider);
            brightnessSlider->setValue(0);
            contrastSlider->setValue(0);
            saturationSlider->setValue(0);
            warmthSlider->setValue(0);
        }
        updateAdjustLabels();
        emit brightnessContrastChanged(0, 0, 0, 0);
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
            const QSignalBlocker saturationBlocker(saturationSlider);
            const QSignalBlocker warmthBlocker(warmthSlider);
            brightnessSlider->setValue(0);
            contrastSlider->setValue(0);
            saturationSlider->setValue(0);
            warmthSlider->setValue(0);
        }
        updateAdjustLabels();
        stack->setCurrentWidget(adjustPage);
        break;
    }
    case QVGraphicsView::EditMode::Draw: {
        stack->setCurrentWidget(drawPage);
        emit drawSettingsChanged(currentDrawSettings);
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

int QVEditPanel::saturation() const
{
    return saturationSlider->value();
}

int QVEditPanel::warmth() const
{
    return warmthSlider->value();
}

void QVEditPanel::updateAdjustLabels()
{
    brightnessValueLabel->setText(QString::number(brightnessSlider->value()));
    contrastValueLabel->setText(QString::number(contrastSlider->value()));
    saturationValueLabel->setText(QString::number(saturationSlider->value()));
    warmthValueLabel->setText(QString::number(warmthSlider->value()));
}

void QVEditPanel::updateColorButton()
{
    colorButton->setStyleSheet(
            QStringLiteral("QPushButton { border-left: 12px solid %1; }")
                    .arg(currentDrawSettings.color.name()));
}
