#include <QtTest>

#include "qvapplication.h"
#include "qvimagecore.h"

class ActionManagerTests : public QObject
{
    Q_OBJECT

public:
    ActionManagerTests();
    ~ActionManagerTests();

private slots:
    void testClonedActionsUntracked();
    void testEditActionsRegistered();
    void testBrightnessContrastLut();
};

ActionManagerTests::ActionManagerTests() { }

ActionManagerTests::~ActionManagerTests() { }

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

    auto *cropAction = actionManager.getAction("editcrop");
    auto *brightnessContrastAction = actionManager.getAction("brightnesscontrast");

    QVERIFY(cropAction);
    QVERIFY(brightnessContrastAction);
    QCOMPARE(cropAction->data().toStringList().last(), QString("stillimagedisable"));
    QCOMPARE(brightnessContrastAction->data().toStringList().last(),
             QString("stillimagedisable"));
    QVERIFY(!cropAction->isEnabled());
    QVERIFY(!brightnessContrastAction->isEnabled());
}

void ActionManagerTests::testBrightnessContrastLut()
{
    QImage image(2, 2, QImage::Format_ARGB32);
    image.setPixel(0, 0, qRgba(10, 20, 30, 40));
    image.setPixel(1, 0, qRgba(128, 128, 128, 255));
    image.setPixel(0, 1, qRgba(250, 250, 250, 128));
    image.setPixel(1, 1, qRgba(0, 0, 0, 0));

    const QImage unchanged = QVImageCore::applyBrightnessContrast(image, 0, 0);
    QCOMPARE(unchanged.pixel(0, 0), image.pixel(0, 0));
    QCOMPARE(unchanged.pixel(1, 1), image.pixel(1, 1));

    const QImage brighter = QVImageCore::applyBrightnessContrast(image, 100, 0);
    QCOMPARE(qRed(brighter.pixel(0, 0)), 255);
    QCOMPARE(qGreen(brighter.pixel(0, 0)), 255);
    QCOMPARE(qBlue(brighter.pixel(0, 0)), 255);
    QCOMPARE(qAlpha(brighter.pixel(0, 0)), 40);

    const QImage darker = QVImageCore::applyBrightnessContrast(image, -100, 0);
    QCOMPARE(qRed(darker.pixel(0, 0)), 0);
    QCOMPARE(qGreen(darker.pixel(0, 0)), 0);
    QCOMPARE(qBlue(darker.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(darker.pixel(0, 0)), 40);

    const QImage contrasted = QVImageCore::applyBrightnessContrast(image, 0, 50);
    QCOMPARE(qRed(contrasted.pixel(1, 0)), 128);
    QVERIFY(qRed(contrasted.pixel(0, 0)) < 10);
    QCOMPARE(qAlpha(contrasted.pixel(0, 0)), 40);
}

int main(int argc, char *argv[])
{
    QVApplication app(argc, argv);
    ActionManagerTests actionManagerTests;
    return QTest::qExec(&actionManagerTests, argc, argv);
}

#include "tst_actionmanagertests.moc"
