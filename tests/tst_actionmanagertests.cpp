#include <QtTest>

#include "qvapplication.h"
#include "qveditpanel.h"
#include "qvimagecore.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

class ActionManagerTests : public QObject
{
    Q_OBJECT

public:
    ActionManagerTests();
    ~ActionManagerTests();

private slots:
    void testClonedActionsUntracked();
    void testEditActionsRegistered();
    void testAdjustmentsLut();
    void testAdjustmentsPremultiplied();
    void testAdjustmentsOpaque();
    void testAdjustmentsLargeImage();
    void testClampCropRect();
    void testEditPanelCropBehavior();
    void testEditPanelAdjustBehavior();
    void testEditPanelDrawBehavior();

private:
    static QPushButton *findVisibleButton(QVEditPanel &panel, const QString &text);
    static QToolButton *findVisibleToolButton(QVEditPanel &panel, const QString &text);
    static QSlider *findSliderByToolTip(QVEditPanel &panel, const QString &text);
};

ActionManagerTests::ActionManagerTests() { }

ActionManagerTests::~ActionManagerTests() { }

QPushButton *ActionManagerTests::findVisibleButton(QVEditPanel &panel, const QString &text)
{
    const QList<QPushButton *> buttons = panel.findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text() == text && button->isVisible())
            return button;
    }
    return nullptr;
}

QToolButton *ActionManagerTests::findVisibleToolButton(QVEditPanel &panel, const QString &text)
{
    const QList<QToolButton *> buttons = panel.findChildren<QToolButton *>();
    for (QToolButton *button : buttons) {
        if (button->text() == text && button->isVisible())
            return button;
    }
    return nullptr;
}

QSlider *ActionManagerTests::findSliderByToolTip(QVEditPanel &panel, const QString &text)
{
    const QList<QSlider *> sliders = panel.findChildren<QSlider *>();
    for (QSlider *slider : sliders) {
        if (slider->toolTip().contains(text))
            return slider;
    }
    return nullptr;
}

void ActionManagerTests::testClonedActionsUntracked()
{
    // Get initial counts of certain actions
    int fullscreenCount = qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length();
    int openCount = qvApp->getActionManager().getAllInstancesOfAction("open").length();
    qDebug() << fullscreenCount;

    // Have window clone actions
    MainWindow window;
    window.show();
    // Make sure they were cloned
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length()
            != fullscreenCount);
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("open").length() != openCount);
    // Untrack them
    window.close();

    // Make sure the count has not changed from the initial
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length(),
             fullscreenCount);
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("open").length(), openCount);
}

void ActionManagerTests::testEditActionsRegistered()
{
    const auto &actionManager = qvApp->getActionManager();

    const QAction *cropAction = actionManager.getAction("editcrop");
    const QAction *brightnessContrastAction = actionManager.getAction("brightnesscontrast");
    const QAction *drawAction = actionManager.getAction("draw");

    QVERIFY(cropAction);
    QVERIFY(brightnessContrastAction);
    QVERIFY(drawAction);

    const QString stillImageDisable = QStringLiteral("stillimagedisable");
    QCOMPARE(drawAction->data().toStringList().last(), stillImageDisable);
    QCOMPARE(cropAction->data().toStringList().last(), stillImageDisable);
    QCOMPARE(brightnessContrastAction->data().toStringList().last(), stillImageDisable);
    QVERIFY(!drawAction->isEnabled());
    QVERIFY(!cropAction->isEnabled());
    QVERIFY(!brightnessContrastAction->isEnabled());
}

void ActionManagerTests::testAdjustmentsLut()
{
    QImage image(2, 2, QImage::Format_ARGB32);
    image.setPixel(0, 0, qRgba(10, 20, 30, 40));
    image.setPixel(1, 0, qRgba(128, 128, 128, 255));
    image.setPixel(0, 1, qRgba(250, 250, 250, 128));
    image.setPixel(1, 1, qRgba(0, 0, 0, 0));

    const QImage unchanged = QVImageCore::applyAdjustments(image, 0, 0);
    QCOMPARE(unchanged.pixel(0, 0), image.pixel(0, 0));
    QCOMPARE(unchanged.pixel(1, 1), image.pixel(1, 1));

    const QImage brighter = QVImageCore::applyAdjustments(image, 100, 0);
    QVERIFY(qRed(brighter.pixel(0, 0)) > 10);
    QVERIFY(qGreen(brighter.pixel(0, 0)) > 20);
    QVERIFY(qBlue(brighter.pixel(0, 0)) > 30);
    QVERIFY(qRed(brighter.pixel(1, 0)) > 128);
    QCOMPARE(qRed(brighter.pixel(0, 1)), 255);
    QCOMPARE(qAlpha(brighter.pixel(0, 0)), 40);

    const QImage darker = QVImageCore::applyAdjustments(image, -100, 0);
    QCOMPARE(qRed(darker.pixel(0, 0)), 0);
    QVERIFY(qRed(darker.pixel(1, 0)) < 128);
    QVERIFY(qRed(darker.pixel(0, 1)) < 250);
    QCOMPARE(qAlpha(darker.pixel(0, 0)), 40);

    const QImage contrasted = QVImageCore::applyAdjustments(image, 0, 50);
    QCOMPARE(qRed(contrasted.pixel(1, 0)), 128);
    QVERIFY(qRed(contrasted.pixel(0, 1)) > 250);
    QCOMPARE(qAlpha(contrasted.pixel(0, 0)), 40);

    // Saturation: -100 desaturates to grayscale (R == G == B), +100 boosts chroma
    const QImage gray = QVImageCore::applyAdjustments(image, 0, 0, -100, 0);
    QCOMPARE(qRed(gray.pixel(0, 0)), qGreen(gray.pixel(0, 0)));
    QCOMPARE(qGreen(gray.pixel(0, 0)), qBlue(gray.pixel(0, 0)));
    QCOMPARE(qAlpha(gray.pixel(0, 0)), 40);

    // Warmth: positive warmth boosts red and lowers blue
    const QImage warm = QVImageCore::applyAdjustments(image, 0, 0, 0, 50);
    QVERIFY(qRed(warm.pixel(1, 0)) > 128);
    QVERIFY(qBlue(warm.pixel(1, 0)) < 128);
}

void ActionManagerTests::testAdjustmentsPremultiplied()
{
    QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
    image.setPixel(0, 0, qPremultiply(qRgba(100, 100, 100, 128)));

    const QImage result = QVImageCore::applyAdjustments(image, 20, 0);
    QCOMPARE(result.format(), QImage::Format_ARGB32_Premultiplied);
    const QRgb pixel = qUnpremultiply(result.pixel(0, 0));
    QVERIFY(qRed(pixel) > 100);
    QCOMPARE(qAlpha(pixel), 128);
}

void ActionManagerTests::testAdjustmentsOpaque()
{
    QImage image(1, 1, QImage::Format_RGB32);
    image.setPixel(0, 0, qRgb(60, 128, 200));

    const QImage result = QVImageCore::applyAdjustments(image, 0, 100);
    QCOMPARE(qAlpha(result.pixel(0, 0)), 255);
    QVERIFY(qRed(result.pixel(0, 0)) < 60);
    QCOMPARE(qGreen(result.pixel(0, 0)), 128);
    QVERIFY(qBlue(result.pixel(0, 0)) > 200);
}

void ActionManagerTests::testAdjustmentsLargeImage()
{
    // Large enough to take the multithreaded path
    QImage image(1500, 1000, QImage::Format_ARGB32);
    image.fill(qRgba(100, 100, 100, 200));
    image.setPixel(1499, 999, qRgba(0, 0, 0, 10));

    const QImage singleThreadRef =
            QVImageCore::applyAdjustments(image.copy(0, 0, 2, 2), 50, 0);
    const int expectedMid = qRed(singleThreadRef.pixel(0, 0));

    const QImage result = QVImageCore::applyAdjustments(image, 50, 0);
    QVERIFY(expectedMid > 100 && expectedMid < 200);
    QCOMPARE(qRed(result.pixel(0, 0)), expectedMid);
    QCOMPARE(qAlpha(result.pixel(0, 0)), 200);
    QCOMPARE(qRed(result.pixel(750, 500)), expectedMid);
    QCOMPARE(qAlpha(result.pixel(1499, 999)), 10);
}

void ActionManagerTests::testClampCropRect()
{
    const QSize size(100, 50);
    QCOMPARE(QVImageCore::clampCropRect(QRect(10, 10, 20, 20), size), QRect(10, 10, 20, 20));
    QCOMPARE(QVImageCore::clampCropRect(QRect(-10, -10, 30, 30), size), QRect(0, 0, 20, 20));
    QCOMPARE(QVImageCore::clampCropRect(QRect(90, 40, 50, 50), size), QRect(90, 40, 10, 10));
    QVERIFY(QVImageCore::clampCropRect(QRect(200, 200, 10, 10), size).isEmpty());
    QVERIFY(QVImageCore::clampCropRect(QRect(), size).isEmpty());
}

void ActionManagerTests::testEditPanelCropBehavior()
{
    QVEditPanel panel;
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    // Palette and font changes should safely refresh icons and readouts
    QEvent paletteEvent(QEvent::PaletteChange);
    QApplication::sendEvent(&panel, &paletteEvent);
    QEvent fontEvent(QEvent::FontChange);
    QApplication::sendEvent(&panel, &fontEvent);

    // Crop mode: live dimensions readout, aspect ratio presets, and Reset button state
    panel.setMode(QVGraphicsView::EditMode::Crop);

    panel.setCropDimensions(QRect(0, 0, 1920, 1080), QSize(1920, 1080));
    QPushButton *cropResetButton = findVisibleButton(panel, QStringLiteral("Reset"));
    QVERIFY(cropResetButton);
    QVERIFY(!cropResetButton->isEnabled());

    panel.setCropDimensions(QRect(100, 100, 800, 600), QSize(1920, 1080));
    QVERIFY(cropResetButton->isEnabled());

    QToolButton *squarePreset = findVisibleToolButton(panel, QStringLiteral("1:1"));
    QVERIFY(squarePreset);

    QSignalSpy ratioSpy(&panel, &QVEditPanel::cropAspectRatioChanged);
    squarePreset->click();
    QCOMPARE(ratioSpy.count(), 1);
    QCOMPARE(ratioSpy.at(0).at(0).toSizeF(), QSizeF(1, 1));

    QSignalSpy resetCropSpy(&panel, &QVEditPanel::resetCropRequested);
    cropResetButton->click();
    QCOMPARE(resetCropSpy.count(), 1);
}

void ActionManagerTests::testEditPanelAdjustBehavior()
{
    QVEditPanel panel;
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    // Adjust mode: Reset and Compare start disabled; modifying a slider enables them
    panel.setMode(QVGraphicsView::EditMode::Adjust);
    QCOMPARE(panel.brightness(), 0);
    QCOMPARE(panel.contrast(), 0);
    QCOMPARE(panel.saturation(), 0);
    QCOMPARE(panel.warmth(), 0);

    QPushButton *compareButton = findVisibleButton(panel, QStringLiteral("Compare"));
    QPushButton *resetButton = findVisibleButton(panel, QStringLiteral("Reset"));
    QVERIFY(compareButton && resetButton);
    QVERIFY(!compareButton->isEnabled());
    QVERIFY(!resetButton->isEnabled());

    QSlider *brightnessSlider = findSliderByToolTip(panel, QStringLiteral("Brightness"));
    QVERIFY(brightnessSlider);

    QSignalSpy adjustSpy(&panel, &QVEditPanel::adjustmentsChanged);
    brightnessSlider->setValue(40);
    QCOMPARE(panel.brightness(), 40);
    QVERIFY(compareButton->isEnabled());
    QVERIFY(resetButton->isEnabled());
    QCOMPARE(adjustSpy.count(), 1);

    // Press & hold Compare emits compareOriginalChanged(true/false)
    QSignalSpy compareSpy(&panel, &QVEditPanel::compareOriginalChanged);
    QTest::mousePress(compareButton, Qt::LeftButton);
    QTest::mouseRelease(compareButton, Qt::LeftButton);
    QCOMPARE(compareSpy.count(), 2);
    QCOMPARE(compareSpy.at(0).at(0).toBool(), true);
    QCOMPARE(compareSpy.at(1).at(0).toBool(), false);

    // Clicking Reset resets all sliders to 0 and disables Compare & Reset
    resetButton->click();
    QCOMPARE(panel.brightness(), 0);
    QVERIFY(!compareButton->isEnabled());
    QVERIFY(!resetButton->isEnabled());

    // Clicking on left side of track jumps directly toward negative values
    QTest::mousePress(brightnessSlider, Qt::LeftButton, Qt::NoModifier,
                      QPoint(10, brightnessSlider->height() / 2));
    QTest::mouseRelease(brightnessSlider, Qt::LeftButton, Qt::NoModifier,
                        QPoint(10, brightnessSlider->height() / 2));
    QVERIFY(panel.brightness() < -50);

    // Double-click on an Adjust slider resets it back to 0
    brightnessSlider->setValue(65);
    QCOMPARE(panel.brightness(), 65);
    QTest::mouseDClick(brightnessSlider, Qt::LeftButton, Qt::NoModifier,
                       brightnessSlider->rect().center());
    QCOMPARE(panel.brightness(), 0);
}

void ActionManagerTests::testEditPanelDrawBehavior()
{
    QVEditPanel panel;
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    // Draw mode: Undo and Clear track drawing history count
    panel.setMode(QVGraphicsView::EditMode::Draw);
    QPushButton *undoButton = findVisibleButton(panel, QStringLiteral("Undo"));
    QPushButton *clearButton = findVisibleButton(panel, QStringLiteral("Clear"));
    QVERIFY(undoButton && clearButton);
    QVERIFY(!undoButton->isEnabled());
    QVERIFY(!clearButton->isEnabled());

    panel.setDrawHistoryCount(2);
    QVERIFY(undoButton->isEnabled());
    QVERIFY(clearButton->isEnabled());

    QSignalSpy clearSpy(&panel, &QVEditPanel::clearDrawingsRequested);
    clearButton->click();
    QCOMPARE(clearSpy.count(), 1);

    panel.setDrawHistoryCount(0);
    QVERIFY(!undoButton->isEnabled());
    QVERIFY(!clearButton->isEnabled());

    // Switching tool updates DrawSettings
    QToolButton *rectangleButton = findVisibleToolButton(panel, QStringLiteral("Rectangle"));
    QVERIFY(rectangleButton);

    QSignalSpy drawSpy(&panel, &QVEditPanel::drawSettingsChanged);
    rectangleButton->click();
    QCOMPARE(panel.drawSettings().tool, QVGraphicsView::DrawSettings::Tool::Rectangle);
    QCOMPARE(drawSpy.count(), 1);

    // Ensure the Draw panel stays a single slim row vertically
    QVERIFY2(panel.minimumSizeHint().height() < 80,
             qPrintable(QString("Draw panel too tall: %1").arg(panel.minimumSizeHint().height())));
}

int main(int argc, char *argv[])
{
    QVApplication app(argc, argv);
    ActionManagerTests actionManagerTests;
    return QTest::qExec(&actionManagerTests, argc, argv);
}

#include "tst_actionmanagertests.moc"
