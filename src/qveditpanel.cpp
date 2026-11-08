#include "qveditpanel.h"

#include <array>

#include <QButtonGroup>
#include <QColorDialog>
#include <QConicalGradient>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
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

constexpr int toolIconSize = 16;
constexpr int swatchIconSize = 14;
constexpr qreal iconDevicePixelRatio = 2.0;

constexpr int adjustMinimum = -100;
constexpr int adjustMaximum = 100;
constexpr int adjustSingleStep = 2;
constexpr int adjustPageStep = 10;

constexpr int defaultDrawWidth = 4;
constexpr int minimumDrawWidth = 1;
constexpr int maximumDrawWidth = 50;

constexpr int checkerCellCount = 4;
constexpr int readoutPadding = 6;
constexpr qreal hintFontScale = 0.88;

QPixmap createIconPixmap(int logicalSize)
{
    QPixmap pixmap(logicalSize * 2, logicalSize * 2);
    pixmap.setDevicePixelRatio(iconDevicePixelRatio);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

QIcon makeSwatchIcon(const QColor &swatchColor, const QColor &borderColor)
{
    QPixmap pixmap = createIconPixmap(swatchIconSize);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF swatchRect(1.0, 1.0, swatchIconSize - 2.0, swatchIconSize - 2.0);
    if (swatchColor.alpha() < 255) {
        QPainterPath clip;
        clip.addRoundedRect(swatchRect, 3.0, 3.0);
        painter.save();
        painter.setClipPath(clip);
        painter.fillRect(swatchRect, Qt::white);

        const qreal cellSize = swatchRect.width() / checkerCellCount;
        for (int row = 0; row < checkerCellCount; ++row) {
            for (int column = 0; column < checkerCellCount; ++column) {
                if ((row + column) % 2 == 1) {
                    const QRectF cell(swatchRect.x() + column * cellSize,
                                      swatchRect.y() + row * cellSize, cellSize, cellSize);
                    painter.fillRect(cell, QColor(200, 200, 200));
                }
            }
        }
        painter.restore();
    }

    painter.setPen(QPen(borderColor, 1.0));
    painter.setBrush(swatchColor);
    painter.drawRoundedRect(swatchRect, 3.0, 3.0);

    return QIcon(pixmap);
}

QIcon makeColorWheelIcon(const QColor &borderColor)
{
    QPixmap pixmap = createIconPixmap(swatchIconSize);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF ringRect(1.0, 1.0, swatchIconSize - 2.0, swatchIconSize - 2.0);
    QConicalGradient gradient(ringRect.center(), 90.0);

    const std::array<QColor, 6> wheelColors = {
        { QColor(239, 68, 68), QColor(245, 158, 11), QColor(234, 179, 8),
          QColor(16, 185, 129), QColor(59, 130, 246), QColor(168, 85, 247) }
    };
    for (int i = 0; i < static_cast<int>(wheelColors.size()); ++i) {
        const qreal position = static_cast<qreal>(i) / static_cast<qreal>(wheelColors.size());
        gradient.setColorAt(position, wheelColors.at(i));
    }
    gradient.setColorAt(1.0, wheelColors.front());

    painter.setPen(QPen(borderColor, 0.8));
    painter.setBrush(gradient);
    painter.drawEllipse(ringRect);

    return QIcon(pixmap);
}

QFrame *makeVerticalSeparator(QWidget *parent)
{
    auto *separator = new QFrame(parent);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    return separator;
}

void setLabelBold(QLabel *label, bool bold)
{
    if (label->font().bold() == bold)
        return;

    QFont font = label->font();
    font.setBold(bold);
    label->setFont(font);
}

// UX-focused slider:
// - Left-clicking anywhere on the slider jumps straight to that value and drags immediately.
// - Double-clicking or middle-clicking resets the slider to its default value.
class AdjustSlider : public QSlider
{
public:
    explicit AdjustSlider(int defaultValue, Qt::Orientation orientation,
                          QWidget *parent = nullptr)
        : QSlider(orientation, parent), defaultValue(defaultValue)
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
            QStyleOptionSlider option;
            initStyleOption(&option);
            const QRect handleRect = style()->subControlRect(QStyle::CC_Slider, &option,
                                                             QStyle::SC_SliderHandle, this);
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
    int valueFromPoint(const QPoint &position) const
    {
        QStyleOptionSlider option;
        initStyleOption(&option);

        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option,
                                                     QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option,
                                                     QStyle::SC_SliderHandle, this);

        const int handleWidth = handle.isValid() ? handle.width() : 0;
        const int grooveLeft = groove.isValid() ? groove.x() : 0;
        const int grooveWidth = groove.isValid() ? groove.width() : width();
        const int span = grooveWidth - handleWidth;
        const int positionInGroove = position.x() - grooveLeft - handleWidth / 2;

        return QStyle::sliderValueFromPosition(minimum(), maximum(), positionInGroove,
                                               qMax(1, span), option.upsideDown);
    }

    int defaultValue = 0;
    bool trackDragging = false;
};

} // namespace

QVEditPanel::QVEditPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("editPanel"));
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

    createCropPage();
    createAdjustPage();
    createDrawPage();

    updateCropHintStyle();
    updateAdjustReadoutWidths();
    updateThemeIcons();
    updateColorButtons();
    updateAdjustLabels();
    setMode(QVGraphicsView::EditMode::Crop);
}

void QVEditPanel::createCropPage()
{
    struct AspectPreset
    {
        QString label;
        QString tooltip;
        QSizeF ratio;
    };

    const std::array<AspectPreset, 5> presets = {
        { { tr("Free"), tr("Freeform crop (hold Shift for 1:1 square)"), QSizeF(0, 0) },
          { tr("Original"), tr("Lock to original image aspect ratio"), QSizeF(-1, -1) },
          { QStringLiteral("1:1"), tr("Square aspect ratio (1:1)"), QSizeF(1, 1) },
          { QStringLiteral("4:3"), tr("Standard aspect ratio (4:3)"), QSizeF(4, 3) },
          { QStringLiteral("16:9"), tr("Widescreen aspect ratio (16:9)"), QSizeF(16, 9) } }
    };

    cropPage = new QWidget(stack);
    auto *cropLayout = new QVBoxLayout(cropPage);
    cropLayout->setContentsMargins(0, 0, 0, 0);
    cropLayout->setSpacing(0);

    auto *controlsRow = new QHBoxLayout();
    controlsRow->setContentsMargins(0, 0, 0, 0);
    controlsRow->setSpacing(4);

    auto *aspectGroup = new QButtonGroup(cropPage);
    bool firstPreset = true;
    for (const AspectPreset &preset : presets) {
        auto *button = new QToolButton(cropPage);
        button->setText(preset.label);
        button->setToolTip(preset.tooltip);
        button->setAutoRaise(true);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setChecked(firstPreset);
        firstPreset = false;

        aspectGroup->addButton(button);
        aspectButtons.append(button);
        controlsRow->addWidget(button);

        const QSizeF ratio = preset.ratio;
        connect(button, &QToolButton::clicked, this, [this, ratio]() {
            if (cropResetButton && ratio.isValid() && !ratio.isNull())
                cropResetButton->setEnabled(true);
            emit cropAspectRatioChanged(ratio);
        });
    }

    controlsRow->addWidget(makeVerticalSeparator(cropPage));

    cropSizeLabel = new QLabel(cropPage);
    cropSizeLabel->setFocusPolicy(Qt::NoFocus);
    cropSizeLabel->setToolTip(tr("Current crop dimensions in pixels"));
    controlsRow->addWidget(cropSizeLabel);

    controlsRow->addStretch();

    cropResetButton = new QPushButton(tr("Reset"), cropPage);
    cropResetButton->setFocusPolicy(Qt::NoFocus);
    cropResetButton->setToolTip(tr("Reset crop to full image"));
    cropResetButton->setEnabled(false);
    connect(cropResetButton, &QPushButton::clicked, this, [this]() {
        if (!aspectButtons.isEmpty())
            aspectButtons.first()->setChecked(true);
        emit resetCropRequested();
    });
    controlsRow->addWidget(cropResetButton);
    cropLayout->addLayout(controlsRow);

    auto *hintRow = new QHBoxLayout();
    hintRow->setContentsMargins(0, 0, 0, 0);
    hintRow->setSpacing(0);
    hintRow->addStretch();

    cropHint = new QLabel(tr("Enter: Save copy · Ctrl+Enter: Replace · Esc: Cancel"), cropPage);
    cropHint->setFocusPolicy(Qt::NoFocus);
    cropHint->setContentsMargins(0, 0, 0, 0);
    cropHint->setAlignment(Qt::AlignCenter);
    hintRow->addWidget(cropHint);
    hintRow->addStretch();
    cropLayout->addLayout(hintRow);

    stack->addWidget(cropPage);
}

void QVEditPanel::createAdjustControl(QGridLayout *grid, int row, int columnGroup,
                                      const QString &labelText, AdjustControl &control)
{
    constexpr int columnsPerControl = 3;
    const int baseColumn = columnGroup * columnsPerControl;

    auto *label = new QLabel(labelText, adjustPage);
    label->setFocusPolicy(Qt::NoFocus);

    control.slider = new AdjustSlider(0, Qt::Horizontal, adjustPage);
    control.slider->setRange(adjustMinimum, adjustMaximum);
    control.slider->setSingleStep(adjustSingleStep);
    control.slider->setPageStep(adjustPageStep);
    control.slider->setValue(0);
    control.slider->setMinimumWidth(100);
    control.slider->setToolTip(
            tr("%1 — click or drag to adjust, double-click to reset").arg(labelText));
    control.slider->setFocusPolicy(Qt::NoFocus);

    control.valueLabel = new QLabel(QStringLiteral("0"), adjustPage);
    control.valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    control.valueLabel->setFocusPolicy(Qt::NoFocus);

    grid->addWidget(label, row, baseColumn);
    grid->addWidget(control.slider, row, baseColumn + 1);
    grid->addWidget(control.valueLabel, row, baseColumn + 2);
    grid->setColumnStretch(baseColumn + 1, 1);

    connect(control.slider, &QSlider::valueChanged, this, &QVEditPanel::emitAdjustChanged);
}

void QVEditPanel::createAdjustPage()
{
    adjustPage = new QWidget(stack);

    auto *rowLayout = new QHBoxLayout(adjustPage);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(10);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(2);

    createAdjustControl(grid, 0, 0, tr("Brightness"), brightnessControl);
    createAdjustControl(grid, 0, 1, tr("Contrast"), contrastControl);
    createAdjustControl(grid, 1, 0, tr("Saturation"), saturationControl);
    createAdjustControl(grid, 1, 1, tr("Warmth"), warmthControl);

    rowLayout->addLayout(grid, 1);
    rowLayout->addWidget(makeVerticalSeparator(adjustPage));

    auto *actions = new QVBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(2);

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
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        resetAdjustSliders();
        emitAdjustChanged();
    });

    actions->addWidget(compareButton);
    actions->addWidget(resetButton);
    rowLayout->addLayout(actions);

    stack->addWidget(adjustPage);
}

void QVEditPanel::createDrawPage()
{
    struct ToolPreset
    {
        QString label;
        QVGraphicsView::DrawSettings::Tool tool;
        ToolIcon icon;
        QString tooltip;
    };

    const std::array<ToolPreset, 4> tools = {
        { { tr("Pen"), QVGraphicsView::DrawSettings::Tool::Pen, ToolIcon::Pen, QString() },
          { tr("Line"), QVGraphicsView::DrawSettings::Tool::Line, ToolIcon::Line,
            tr("Line (hold Shift to snap angle)") },
          { tr("Rectangle"), QVGraphicsView::DrawSettings::Tool::Rectangle, ToolIcon::Rectangle,
            tr("Rectangle (hold Shift for square)") },
          { tr("Ellipse"), QVGraphicsView::DrawSettings::Tool::Ellipse, ToolIcon::Ellipse,
            tr("Ellipse (hold Shift for circle)") } }
    };

    drawPage = new QWidget(stack);

    auto *drawLayout = new QHBoxLayout(drawPage);
    drawLayout->setContentsMargins(0, 0, 0, 0);
    drawLayout->setSpacing(4);

    auto *toolGroup = new QButtonGroup(drawPage);
    for (const ToolPreset &preset : tools) {
        auto *button = new QToolButton(drawPage);
        button->setText(preset.label);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setAutoRaise(true);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setChecked(preset.tool == currentDrawSettings.tool);
        if (!preset.tooltip.isEmpty())
            button->setToolTip(preset.tooltip);

        toolGroup->addButton(button);
        toolButtons.append(button);
        toolIconTypes.append(preset.icon);
        drawLayout->addWidget(button);

        const auto tool = preset.tool;
        connect(button, &QToolButton::clicked, this, [this, tool]() {
            currentDrawSettings.tool = tool;
            emit drawSettingsChanged(currentDrawSettings);
        });
    }

    drawLayout->addWidget(makeVerticalSeparator(drawPage));

    presetColors = {
        QColor(QStringLiteral("#EF4444")), // Red
        QColor(QStringLiteral("#F59E0B")), // Amber
        QColor(QStringLiteral("#10B981")), // Green
        QColor(QStringLiteral("#3B82F6")), // Blue
        QColor(QStringLiteral("#FFFFFF")), // White
        QColor(QStringLiteral("#111111")), // Black
    };
    currentDrawSettings.color = presetColors.first();

    auto *colorGroup = new QButtonGroup(drawPage);
    for (int i = 0; i < presetColors.size(); ++i) {
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
        if (color.isValid())
            currentDrawSettings.color = color;

        updateColorButtons();
        if (color.isValid())
            emit drawSettingsChanged(currentDrawSettings);
    });

    drawLayout->addWidget(makeVerticalSeparator(drawPage));

    auto *widthLabel = new QLabel(tr("Size"), drawPage);
    widthLabel->setFocusPolicy(Qt::NoFocus);

    widthSlider = new AdjustSlider(defaultDrawWidth, Qt::Horizontal, drawPage);
    widthSlider->setRange(minimumDrawWidth, maximumDrawWidth);
    widthSlider->setValue(currentDrawSettings.width);
    widthSlider->setMinimumWidth(60);
    widthSlider->setMaximumWidth(110);
    widthSlider->setToolTip(tr("Stroke width (double-click to reset to %1 px)")
                                    .arg(defaultDrawWidth));
    widthSlider->setFocusPolicy(Qt::NoFocus);

    widthSpin = new QSpinBox(drawPage);
    widthSpin->setRange(minimumDrawWidth, maximumDrawWidth);
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
}

void QVEditPanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange) {
        updateThemeIcons();
        updateColorButtons();
        updateCropHintStyle();
    } else if (event->type() == QEvent::FontChange
               || event->type() == QEvent::ApplicationFontChange) {
        updateAdjustReadoutWidths();
        updateCropHintStyle();
    }

    QWidget::changeEvent(event);
}

void QVEditPanel::setMode(QVGraphicsView::EditMode mode)
{
    QWidget *targetPage = nullptr;

    switch (mode) {
    case QVGraphicsView::EditMode::Crop:
        if (!aspectButtons.isEmpty())
            aspectButtons.first()->setChecked(true);
        targetPage = cropPage;
        break;
    case QVGraphicsView::EditMode::Adjust:
        resetAdjustSliders();
        updateAdjustLabels();
        targetPage = adjustPage;
        break;
    case QVGraphicsView::EditMode::Draw:
        setDrawHistoryCount(0);
        emit drawSettingsChanged(currentDrawSettings);
        targetPage = drawPage;
        break;
    case QVGraphicsView::EditMode::None:
        return;
    }

    // Collapse unused pages so each mode sizes strictly to its own controls
    for (int i = 0; i < stack->count(); ++i) {
        QWidget *page = stack->widget(i);
        const QSizePolicy::Policy policy =
                page == targetPage ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        page->setSizePolicy(policy, policy);
    }

    stack->setCurrentWidget(targetPage);
    stack->updateGeometry();
    updateGeometry();
}

void QVEditPanel::setCropDimensions(const QRect &rect, const QSize &imageSize)
{
    if (!cropSizeLabel || !cropResetButton)
        return;

    if (rect.isEmpty() || imageSize.isEmpty()) {
        cropSizeLabel->clear();
        setLabelBold(cropSizeLabel, false);
        cropResetButton->setEnabled(false);
        return;
    }

    const bool modified = rect != QRect(QPoint(0, 0), imageSize);
    cropSizeLabel->setText(tr("%1 × %2 px").arg(rect.width()).arg(rect.height()));
    setLabelBold(cropSizeLabel, modified);

    const bool hasFixedAspectRatio =
            !aspectButtons.isEmpty() && !aspectButtons.first()->isChecked();
    cropResetButton->setEnabled(modified || hasFixedAspectRatio);
}

void QVEditPanel::setDrawHistoryCount(int count)
{
    const bool hasItems = count > 0;
    undoButton->setEnabled(hasItems);
    clearButton->setEnabled(hasItems);
}

int QVEditPanel::brightness() const
{
    return brightnessControl.slider->value();
}

int QVEditPanel::contrast() const
{
    return contrastControl.slider->value();
}

int QVEditPanel::saturation() const
{
    return saturationControl.slider->value();
}

int QVEditPanel::warmth() const
{
    return warmthControl.slider->value();
}

void QVEditPanel::resetAdjustSliders()
{
    const std::array<QSlider *, 4> sliders = { brightnessControl.slider, contrastControl.slider,
                                               saturationControl.slider, warmthControl.slider };
    for (QSlider *slider : sliders) {
        const QSignalBlocker blocker(slider);
        slider->setValue(0);
    }
}

void QVEditPanel::emitAdjustChanged()
{
    updateAdjustLabels();
    emit brightnessContrastChanged(brightness(), contrast(), saturation(), warmth());
}

void QVEditPanel::updateCropHintStyle()
{
    if (!cropHint)
        return;

    QFont hintFont = font();
    if (hintFont.pointSizeF() > 0) {
        hintFont.setPointSizeF(hintFont.pointSizeF() * hintFontScale);
    } else if (hintFont.pixelSize() > 0) {
        hintFont.setPixelSize(qMax(9, hintFont.pixelSize() - 2));
    } else {
        hintFont.setPointSize(qMax(8, hintFont.pointSize() - 1));
    }
    cropHint->setFont(hintFont);
    cropHint->setContentsMargins(0, 0, 0, 0);
    cropHint->setFixedHeight(cropHint->fontMetrics().height());

    QColor hintColor = palette().color(QPalette::PlaceholderText);
    if (hintColor == palette().color(QPalette::WindowText)) {
        hintColor = palette().color(QPalette::WindowText);
        hintColor.setAlpha(170);
    }

    QPalette hintPalette = cropHint->palette();
    hintPalette.setColor(QPalette::WindowText, hintColor);
    cropHint->setPalette(hintPalette);
}

void QVEditPanel::updateAdjustLabels()
{
    const std::array<AdjustControl *, 4> controls = {
        { &brightnessControl, &contrastControl, &saturationControl, &warmthControl }
    };

    bool anyModified = false;
    for (const AdjustControl *control : controls) {
        const int value = control->slider->value();
        control->valueLabel->setText(value > 0 ? QStringLiteral("+%1").arg(value)
                                               : QString::number(value));
        setLabelBold(control->valueLabel, value != 0);
        anyModified = anyModified || value != 0;
    }

    resetButton->setEnabled(anyModified);
    compareButton->setEnabled(anyModified);
}

void QVEditPanel::updateAdjustReadoutWidths()
{
    QFont boldFont = font();
    boldFont.setBold(true);
    const QFontMetrics boldMetrics(boldFont);
    const int readoutWidth =
            qMax(boldMetrics.horizontalAdvance(QStringLiteral("+100")),
                 boldMetrics.horizontalAdvance(QStringLiteral("-100")))
            + readoutPadding;

    const std::array<AdjustControl *, 4> controls = {
        { &brightnessControl, &contrastControl, &saturationControl, &warmthControl }
    };
    for (const AdjustControl *control : controls) {
        if (control->valueLabel)
            control->valueLabel->setFixedWidth(readoutWidth);
    }
}

void QVEditPanel::updateThemeIcons()
{
    const QColor iconColor = palette().color(QPalette::ButtonText);
    for (int i = 0; i < toolButtons.size() && i < toolIconTypes.size(); ++i)
        toolButtons.at(i)->setIcon(createToolIcon(toolIconTypes.at(i), iconColor));
}

void QVEditPanel::updateColorButtons()
{
    if (!customColorButton)
        return;

    const QColor borderColor = palette().color(QPalette::Mid);
    const QRgb activeColor = currentDrawSettings.color.rgba();
    bool matchedPreset = false;

    for (int i = 0; i < swatchButtons.size() && i < presetColors.size(); ++i) {
        const QColor presetColor = presetColors.at(i);
        const bool active = presetColor.rgba() == activeColor;
        matchedPreset = matchedPreset || active;

        swatchButtons.at(i)->setIcon(makeSwatchIcon(presetColor, borderColor));
        swatchButtons.at(i)->setChecked(active);
    }

    customColorButton->setIcon(matchedPreset
                                       ? makeColorWheelIcon(borderColor)
                                       : makeSwatchIcon(currentDrawSettings.color, borderColor));
    customColorButton->setChecked(!matchedPreset);
}

QIcon QVEditPanel::createToolIcon(ToolIcon type, const QColor &color)
{
    QPixmap pixmap = createIconPixmap(toolIconSize);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    switch (type) {
    case ToolIcon::Pen: {
        QPainterPath wave;
        wave.moveTo(2.5, 12.5);
        wave.cubicTo(5.0, 4.5, 9.5, 12.5, 13.5, 3.5);
        painter.drawPath(wave);
        break;
    }
    case ToolIcon::Line:
        painter.drawLine(QPointF(3.0, 13.0), QPointF(13.0, 3.0));
        break;
    case ToolIcon::Rectangle:
        painter.drawRoundedRect(QRectF(2.5, 3.5, 11.0, 9.0), 1.5, 1.5);
        break;
    case ToolIcon::Ellipse:
        painter.drawEllipse(QRectF(2.5, 3.5, 11.0, 9.0));
        break;
    }

    return QIcon(pixmap);
}
