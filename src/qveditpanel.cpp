#include "qveditpanel.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QConicalGradient>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// UX-focused slider:
// - Left-clicking anywhere on the slider jumps straight to that value and drags immediately
// - Double-clicking or middle-clicking resets the slider to its default value (0 or initial)
class AdjustSlider : public QSlider
{
public:
    explicit AdjustSlider(int defaultVal, Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent), defaultValue(defaultVal)
    {
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::MiddleButton) {
            setValue(defaultValue);
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton) {
            QStyleOptionSlider opt;
            initStyleOption(&opt);
            const QRect handleRect =
                    style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
            if (!handleRect.contains(event->pos())) {
                trackDragging = true;
                setSliderDown(true);
                setValue(valueFromPoint(event->pos()));
                event->accept();
                return;
            }
        }
        QSlider::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (trackDragging) {
            setValue(valueFromPoint(event->pos()));
            event->accept();
            return;
        }
        QSlider::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (trackDragging && event->button() == Qt::LeftButton) {
            trackDragging = false;
            setSliderDown(false);
            event->accept();
            return;
        }
        QSlider::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            trackDragging = false;
            setSliderDown(false);
            setValue(defaultValue);
            event->accept();
            return;
        }
        QSlider::mouseDoubleClickEvent(event);
    }

private:
    int valueFromPoint(const QPoint &pt) const
    {
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        const QRect groove =
                style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
        const QRect handle =
                style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
        const int handleWidth = handle.isValid() ? handle.width() : 0;
        const int grooveLeft = groove.isValid() ? groove.x() : 0;
        const int span = (groove.isValid() ? groove.width() : width()) - handleWidth;
        const int pos = pt.x() - grooveLeft - handleWidth / 2;
        return QStyle::sliderValueFromPosition(minimum(), maximum(), pos, qMax(1, span),
                                               opt.upsideDown);
    }

    int defaultValue;
    bool trackDragging = false;
};

enum class ToolIcon { Pen, Line, Rectangle, Ellipse };

QIcon makeToolIcon(ToolIcon type, const QColor &color)
{
    constexpr int size = 16;
    QPixmap pix(size * 2, size * 2);
    pix.setDevicePixelRatio(2.0);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (type) {
    case ToolIcon::Pen: {
        QPainterPath wave;
        wave.moveTo(2.5, 12.5);
        wave.cubicTo(5.0, 4.5, 9.5, 12.5, 13.5, 3.5);
        p.drawPath(wave);
        break;
    }
    case ToolIcon::Line:
        p.drawLine(QPointF(3.0, 13.0), QPointF(13.0, 3.0));
        break;
    case ToolIcon::Rectangle:
        p.drawRoundedRect(QRectF(2.5, 3.5, 11.0, 9.0), 1.5, 1.5);
        break;
    case ToolIcon::Ellipse:
        p.drawEllipse(QRectF(2.5, 3.5, 11.0, 9.0));
        break;
    }

    return QIcon(pix);
}

QIcon makeSwatchIcon(const QColor &swatchColor, const QColor &borderColor)
{
    constexpr int size = 14;
    QPixmap pix(size * 2, size * 2);
    pix.setDevicePixelRatio(2.0);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF swatchRect(1.0, 1.0, size - 2.0, size - 2.0);
    if (swatchColor.alpha() < 255) {
        QPainterPath clip;
        clip.addRoundedRect(swatchRect, 3.0, 3.0);
        p.save();
        p.setClipPath(clip);
        p.fillRect(swatchRect, Qt::white);
        const qreal cell = swatchRect.width() / 4.0;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if ((r + c) % 2 == 1) {
                    p.fillRect(QRectF(swatchRect.x() + c * cell, swatchRect.y() + r * cell, cell,
                                      cell),
                               QColor(200, 200, 200));
                }
            }
        }
        p.restore();
    }

    p.setPen(QPen(borderColor, 1.0));
    p.setBrush(swatchColor);
    p.drawRoundedRect(swatchRect, 3.0, 3.0);

    return QIcon(pix);
}

QIcon makeColorWheelIcon(const QColor &borderColor)
{
    constexpr int size = 14;
    QPixmap pix(size * 2, size * 2);
    pix.setDevicePixelRatio(2.0);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF ringRect(1.0, 1.0, size - 2.0, size - 2.0);
    QConicalGradient grad(ringRect.center(), 90.0);
    grad.setColorAt(0.0 / 6.0, QColor(239, 68, 68));
    grad.setColorAt(1.0 / 6.0, QColor(245, 158, 11));
    grad.setColorAt(2.0 / 6.0, QColor(234, 179, 8));
    grad.setColorAt(3.0 / 6.0, QColor(16, 185, 129));
    grad.setColorAt(4.0 / 6.0, QColor(59, 130, 246));
    grad.setColorAt(5.0 / 6.0, QColor(168, 85, 247));
    grad.setColorAt(1.0, QColor(239, 68, 68));

    p.setPen(QPen(borderColor, 0.8));
    p.setBrush(grad);
    p.drawEllipse(ringRect);

    return QIcon(pix);
}

QFrame *makeVerticalSeparator(QWidget *parent)
{
    auto *sep = new QFrame(parent);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    return sep;
}

} // namespace

QVEditPanel::QVEditPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("editPanel");
    setFocusPolicy(Qt::NoFocus);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Window);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *topLine = new QFrame(this);
    topLine->setFrameShape(QFrame::HLine);
    topLine->setFrameShadow(QFrame::Sunken);
    rootLayout->addWidget(topLine);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(8, 4, 8, 4);
    contentLayout->setSpacing(6);
    rootLayout->addLayout(contentLayout);

    stack = new QStackedWidget(this);
    stack->setFocusPolicy(Qt::NoFocus);
    contentLayout->addWidget(stack);

    // -------------------------------------------------------------------------
    // 1. Crop Page (unchanged)
    // -------------------------------------------------------------------------
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

    // -------------------------------------------------------------------------
    // 2. Adjust Page (UX-focused: 2x2 grid, click-to-jump + double-click reset,
    //    Compare original hold button, smart Reset)
    // -------------------------------------------------------------------------
    adjustPage = new QWidget(stack);
    auto *adjustRowLayout = new QHBoxLayout(adjustPage);
    adjustRowLayout->setContentsMargins(0, 0, 0, 0);
    adjustRowLayout->setSpacing(10);

    auto *adjustGrid = new QGridLayout();
    adjustGrid->setContentsMargins(0, 0, 0, 0);
    adjustGrid->setHorizontalSpacing(6);
    adjustGrid->setVerticalSpacing(2);

    const auto addAdjustSlider = [this, adjustGrid](int row, int colGroup, const QString &labelText,
                                                  QSlider *&slider, QLabel *&valueLabel) {
        const int baseCol = colGroup * 3;

        auto *label = new QLabel(labelText, adjustPage);
        label->setFocusPolicy(Qt::NoFocus);

        slider = new AdjustSlider(0, Qt::Horizontal, adjustPage);
        slider->setRange(-100, 100);
        slider->setSingleStep(2);
        slider->setPageStep(10);
        slider->setValue(0);
        slider->setMinimumWidth(100);
        slider->setToolTip(tr("%1 — click or drag to adjust, double-click to reset").arg(labelText));
        slider->setFocusPolicy(Qt::NoFocus);

        valueLabel = new QLabel(QStringLiteral("0"), adjustPage);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel->setFocusPolicy(Qt::NoFocus);

        adjustGrid->addWidget(label, row, baseCol);
        adjustGrid->addWidget(slider, row, baseCol + 1);
        adjustGrid->addWidget(valueLabel, row, baseCol + 2);
        adjustGrid->setColumnStretch(baseCol + 1, 1);
    };

    addAdjustSlider(0, 0, tr("Brightness"), brightnessSlider, brightnessValueLabel);
    addAdjustSlider(0, 1, tr("Contrast"), contrastSlider, contrastValueLabel);
    addAdjustSlider(1, 0, tr("Saturation"), saturationSlider, saturationValueLabel);
    addAdjustSlider(1, 1, tr("Warmth"), warmthSlider, warmthValueLabel);

    adjustRowLayout->addLayout(adjustGrid, 1);
    adjustRowLayout->addWidget(makeVerticalSeparator(adjustPage));

    auto *adjustActions = new QVBoxLayout();
    adjustActions->setContentsMargins(0, 0, 0, 0);
    adjustActions->setSpacing(2);

    compareButton = new QPushButton(tr("Compare"), adjustPage);
    compareButton->setFocusPolicy(Qt::NoFocus);
    compareButton->setToolTip(tr("Press and hold to view the original image"));
    connect(compareButton, &QPushButton::pressed, this,
            [this]() { emit compareOriginalChanged(true); });
    connect(compareButton, &QPushButton::released, this,
            [this]() { emit compareOriginalChanged(false); });

    resetButton = new QPushButton(tr("Reset"), adjustPage);
    resetButton->setFocusPolicy(Qt::NoFocus);
    resetButton->setToolTip(tr("Reset all adjustments to 0"));

    adjustActions->addWidget(compareButton);
    adjustActions->addWidget(resetButton);
    adjustRowLayout->addLayout(adjustActions);
    stack->addWidget(adjustPage);

    // -------------------------------------------------------------------------
    // 3. Draw Page (UX-focused: native theme-aware tool buttons, quick swatches,
    //    direct width slider + spinbox, state-aware Undo & Clear)
    // -------------------------------------------------------------------------
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
        auto *button = new QToolButton(drawPage);
        button->setText(entry.text);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setAutoRaise(true);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setChecked(entry.tool == currentDrawSettings.tool);
        if (entry.tool == QVGraphicsView::DrawSettings::Tool::Line)
            button->setToolTip(tr("Line (hold Shift to snap angle)"));
        else if (entry.tool == QVGraphicsView::DrawSettings::Tool::Rectangle)
            button->setToolTip(tr("Rectangle (hold Shift for square)"));
        else if (entry.tool == QVGraphicsView::DrawSettings::Tool::Ellipse)
            button->setToolTip(tr("Ellipse (hold Shift for circle)"));
        toolGroup->addButton(button);
        toolButtons.append(button);
        drawLayout->addWidget(button);
        const auto tool = entry.tool;
        connect(button, &QToolButton::clicked, this, [this, tool]() {
            currentDrawSettings.tool = tool;
            emit drawSettingsChanged(currentDrawSettings);
        });
    }

    drawLayout->addWidget(makeVerticalSeparator(drawPage));

    presetColors = {
        QColor("#EF4444"), // Red
        QColor("#F59E0B"), // Amber
        QColor("#10B981"), // Green
        QColor("#3B82F6"), // Blue
        QColor("#FFFFFF"), // White
        QColor("#111111"), // Black
    };
    currentDrawSettings.color = presetColors.first();

    auto *colorGroup = new QButtonGroup(drawPage);
    for (int i = 0; i < presetColors.size(); i++) {
        const QColor swatchColor = presetColors.at(i);
        auto *swatch = new QToolButton(drawPage);
        swatch->setAutoRaise(true);
        swatch->setCheckable(true);
        swatch->setFocusPolicy(Qt::NoFocus);
        swatch->setToolTip(swatchColor.name());
        swatch->setChecked(i == 0);
        colorGroup->addButton(swatch);
        swatchButtons.append(swatch);
        drawLayout->addWidget(swatch);

        connect(swatch, &QToolButton::clicked, this, [this, swatchColor]() {
            currentDrawSettings.color = swatchColor;
            updateColorButtons();
            emit drawSettingsChanged(currentDrawSettings);
        });
    }

    customColorButton = new QToolButton(drawPage);
    customColorButton->setText(tr("More…"));
    customColorButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    customColorButton->setAutoRaise(true);
    customColorButton->setCheckable(true);
    customColorButton->setFocusPolicy(Qt::NoFocus);
    customColorButton->setToolTip(tr("Pick a custom color"));
    colorGroup->addButton(customColorButton);
    drawLayout->addWidget(customColorButton);

    connect(customColorButton, &QToolButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(currentDrawSettings.color, this,
                                                    tr("Select Color"),
                                                    QColorDialog::ShowAlphaChannel);
        if (!color.isValid()) {
            updateColorButtons();
            return;
        }
        currentDrawSettings.color = color;
        updateColorButtons();
        emit drawSettingsChanged(currentDrawSettings);
    });

    drawLayout->addWidget(makeVerticalSeparator(drawPage));

    auto *widthLabel = new QLabel(tr("Size"), drawPage);
    widthLabel->setFocusPolicy(Qt::NoFocus);

    widthSlider = new AdjustSlider(4, Qt::Horizontal, drawPage);
    widthSlider->setRange(1, 50);
    widthSlider->setValue(currentDrawSettings.width);
    widthSlider->setMinimumWidth(60);
    widthSlider->setMaximumWidth(110);
    widthSlider->setToolTip(tr("Stroke width (double-click to reset to 4 px)"));
    widthSlider->setFocusPolicy(Qt::NoFocus);

    widthSpin = new QSpinBox(drawPage);
    widthSpin->setRange(1, 50);
    widthSpin->setValue(currentDrawSettings.width);
    widthSpin->setSuffix(tr(" px"));
    widthSpin->setFocusPolicy(Qt::NoFocus);

    connect(widthSlider, &QSlider::valueChanged, this, [this](int value) {
        const QSignalBlocker blocker(widthSpin);
        widthSpin->setValue(value);
        currentDrawSettings.width = value;
        emit drawSettingsChanged(currentDrawSettings);
    });
    connect(widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        const QSignalBlocker blocker(widthSlider);
        widthSlider->setValue(value);
        currentDrawSettings.width = value;
        emit drawSettingsChanged(currentDrawSettings);
    });

    drawLayout->addWidget(widthLabel);
    drawLayout->addWidget(widthSlider);
    drawLayout->addWidget(widthSpin);
    drawLayout->addStretch();

    undoButton = new QPushButton(tr("Undo"), drawPage);
    undoButton->setFocusPolicy(Qt::NoFocus);
    undoButton->setToolTip(tr("Remove last annotation (Ctrl+Z)"));
    undoButton->setEnabled(false);
    connect(undoButton, &QPushButton::clicked, this, &QVEditPanel::undoDrawingRequested);

    clearButton = new QPushButton(tr("Clear"), drawPage);
    clearButton->setFocusPolicy(Qt::NoFocus);
    clearButton->setToolTip(tr("Remove all annotations"));
    clearButton->setEnabled(false);
    connect(clearButton, &QPushButton::clicked, this, &QVEditPanel::clearDrawingsRequested);

    drawLayout->addWidget(undoButton);
    drawLayout->addWidget(clearButton);
    stack->addWidget(drawPage);

    // -------------------------------------------------------------------------
    // Signal connections for Adjust
    // -------------------------------------------------------------------------
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

    updateAdjustReadoutWidths();
    updateThemeIcons();
    updateColorButtons();
    updateAdjustLabels();
    setMode(QVGraphicsView::EditMode::Crop);
}

void QVEditPanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        updateThemeIcons();
        updateColorButtons();
    } else if (event->type() == QEvent::FontChange
               || event->type() == QEvent::ApplicationFontChange) {
        updateAdjustReadoutWidths();
    }
    QWidget::changeEvent(event);
}

void QVEditPanel::setMode(QVGraphicsView::EditMode mode)
{
    QWidget *targetPage = nullptr;
    switch (mode) {
    case QVGraphicsView::EditMode::Crop:
        targetPage = cropPage;
        break;
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
        targetPage = adjustPage;
        break;
    }
    case QVGraphicsView::EditMode::Draw:
        setDrawHistoryCount(0);
        emit drawSettingsChanged(currentDrawSettings);
        targetPage = drawPage;
        break;
    case QVGraphicsView::EditMode::None:
        return;
    }

    // Collapse unused pages so each mode sizes strictly to its own controls
    for (int i = 0; i < stack->count(); i++) {
        QWidget *page = stack->widget(i);
        const QSizePolicy::Policy policy =
                page == targetPage ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        page->setSizePolicy(policy, policy);
    }
    stack->setCurrentWidget(targetPage);
    stack->updateGeometry();
    updateGeometry();
}

void QVEditPanel::setDrawHistoryCount(int count)
{
    const bool hasItems = count > 0;
    undoButton->setEnabled(hasItems);
    clearButton->setEnabled(hasItems);
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
    const auto formatSigned = [](QSlider *slider, QLabel *label) {
        const int v = slider->value();
        label->setText(v > 0 ? QStringLiteral("+%1").arg(v) : QString::number(v));
        QFont f = label->font();
        f.setBold(v != 0);
        label->setFont(f);
    };
    formatSigned(brightnessSlider, brightnessValueLabel);
    formatSigned(contrastSlider, contrastValueLabel);
    formatSigned(saturationSlider, saturationValueLabel);
    formatSigned(warmthSlider, warmthValueLabel);

    const bool anyModified = brightnessSlider->value() != 0 || contrastSlider->value() != 0
            || saturationSlider->value() != 0 || warmthSlider->value() != 0;
    resetButton->setEnabled(anyModified);
    compareButton->setEnabled(anyModified);
}

void QVEditPanel::updateAdjustReadoutWidths()
{
    if (!brightnessValueLabel || !contrastValueLabel || !saturationValueLabel || !warmthValueLabel)
        return;

    QFont boldFont = font();
    boldFont.setBold(true);
    const QFontMetrics boldMetrics(boldFont);
    const int readoutWidth = qMax(boldMetrics.horizontalAdvance(QStringLiteral("+100")),
                                  boldMetrics.horizontalAdvance(QStringLiteral("-100")))
            + 6;
    brightnessValueLabel->setFixedWidth(readoutWidth);
    contrastValueLabel->setFixedWidth(readoutWidth);
    saturationValueLabel->setFixedWidth(readoutWidth);
    warmthValueLabel->setFixedWidth(readoutWidth);
}

void QVEditPanel::updateThemeIcons()
{
    if (toolButtons.isEmpty())
        return;

    const QColor iconColor = palette().color(QPalette::ButtonText);
    const ToolIcon types[4] = { ToolIcon::Pen, ToolIcon::Line, ToolIcon::Rectangle,
                                ToolIcon::Ellipse };
    for (int i = 0; i < toolButtons.size() && i < 4; i++)
        toolButtons.at(i)->setIcon(makeToolIcon(types[i], iconColor));
}

void QVEditPanel::updateColorButtons()
{
    if (!customColorButton)
        return;

    const QColor borderColor = palette().color(QPalette::Mid);
    bool matchedPreset = false;
    for (int i = 0; i < swatchButtons.size(); i++) {
        const QColor c = presetColors.at(i);
        const bool active = (c.rgba() == currentDrawSettings.color.rgba());
        if (active)
            matchedPreset = true;
        swatchButtons.at(i)->setIcon(makeSwatchIcon(c, borderColor));
        swatchButtons.at(i)->setChecked(active);
    }

    customColorButton->setIcon(matchedPreset
                                       ? makeColorWheelIcon(borderColor)
                                       : makeSwatchIcon(currentDrawSettings.color, borderColor));
    customColorButton->setChecked(!matchedPreset);
}
