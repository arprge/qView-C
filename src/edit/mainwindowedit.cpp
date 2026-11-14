#include "mainwindow.h"
#include "qvapplication.h"
#include "qveditpanel.h"
#include "qvimagecore.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageWriter>
#include <QLayout>
#include <QMessageBox>
#include <QPixmap>
#include <QSettings>

namespace {

QStringList writableImageFilters(QStringList &suffixes)
{
    QStringList filters;
    const QList<QByteArray> supportedFormats = QImageWriter::supportedImageFormats();

    for (const QByteArray &format : supportedFormats) {
        const QString suffix = QString::fromLatin1(format).toLower();
        if (suffixes.contains(suffix))
            continue;

        suffixes << suffix;
        filters << QStringLiteral("%1 (*.%2)").arg(suffix.toUpper(), suffix);
    }

    return filters;
}

QString preferredSaveSuffix(const QFileInfo &sourceFile, const QStringList &writableSuffixes)
{
    QString suffix = sourceFile.suffix().toLower();
    if (suffix == QStringLiteral("jpe"))
        suffix = QStringLiteral("jpeg");
    if (!writableSuffixes.contains(suffix))
        suffix = QStringLiteral("png");
    return suffix;
}

} // namespace

void MainWindow::setupEditPanel()
{
    editPanel = new QVEditPanel(this);
    editPanel->hide();
    centralWidget()->layout()->addWidget(editPanel);

    connect(graphicsView, &QVGraphicsView::editModeChanged, this, &MainWindow::onEditModeChanged);
    connect(graphicsView, &QVGraphicsView::commitRequested, this, &MainWindow::onCommitRequested);
    connect(graphicsView, &QVGraphicsView::cropRectChanged, editPanel,
            &QVEditPanel::setCropDimensions);
    connect(editPanel, &QVEditPanel::cropAspectRatioChanged, graphicsView,
            &QVGraphicsView::setCropAspectRatio);
    connect(editPanel, &QVEditPanel::resetCropRequested, graphicsView,
            &QVGraphicsView::resetCropRect);
    connect(editPanel, &QVEditPanel::drawSettingsChanged, graphicsView,
            &QVGraphicsView::setDrawSettings);
    connect(editPanel, &QVEditPanel::undoDrawingRequested, graphicsView,
            &QVGraphicsView::undoLastDrawing);
    connect(editPanel, &QVEditPanel::clearDrawingsRequested, graphicsView,
            &QVGraphicsView::clearDrawings);
    connect(graphicsView, &QVGraphicsView::drawingCountChanged, editPanel,
            &QVEditPanel::setDrawHistoryCount);
    connect(editPanel, &QVEditPanel::adjustmentsChanged, graphicsView,
            &QVGraphicsView::setAdjustPreview);
    connect(editPanel, &QVEditPanel::compareOriginalChanged, graphicsView,
            &QVGraphicsView::setAdjustCompareOriginal);
}

void MainWindow::crop()
{
    graphicsView->beginEdit(QVGraphicsView::EditMode::Crop);
}

void MainWindow::brightnessContrast()
{
    graphicsView->beginEdit(QVGraphicsView::EditMode::Adjust);
}

void MainWindow::draw()
{
    graphicsView->beginEdit(QVGraphicsView::EditMode::Draw);
}

void MainWindow::onEditModeChanged(QVGraphicsView::EditMode mode)
{
    if (mode == QVGraphicsView::EditMode::None) {
        editPanel->hide();
        disableActions();
        return;
    }

    editPanel->setMode(mode);
    editPanel->show();
    disableActions();
    graphicsView->setFocus();
}

void MainWindow::onCommitRequested(QVGraphicsView::CommitAction action)
{
    const QPixmap editedPixmap = graphicsView->renderEditedPixmap();
    if (editedPixmap.isNull())
        return;

    if (action == QVGraphicsView::CommitAction::SaveCopy)
        saveEditedCopy(editedPixmap);
    else
        replaceOriginalWithEdit(editedPixmap);
}

void MainWindow::saveEditedCopy(const QPixmap &editedPixmap)
{
    const QFileInfo currentFile = getCurrentFileDetails().fileInfo;

    QStringList writableSuffixes;
    QStringList filters = writableImageFilters(writableSuffixes);
    filters << tr("All Files (*)");

    const QString defaultSuffix = preferredSaveSuffix(currentFile, writableSuffixes);
    const QString defaultFilter =
            QStringLiteral("%1 (*.%2)").arg(defaultSuffix.toUpper(), defaultSuffix);
    const QString defaultFileName =
            currentFile.completeBaseName() + QStringLiteral("-edited.") + defaultSuffix;

    QString targetDir = currentFile.absolutePath();
    if (targetDir.isEmpty() || !QDir(targetDir).exists()) {
        QSettings settings;
        settings.beginGroup(QStringLiteral("recents"));
        targetDir = settings.value(QStringLiteral("lastFileDialogDir"), QDir::homePath())
                            .toString();
    }

    auto *saveDialog = new QFileDialog(this, tr("Save Edited Copy As..."));
    saveDialog->setAttribute(Qt::WA_DeleteOnClose);
    saveDialog->setWindowModality(Qt::WindowModal);
    saveDialog->setDirectory(targetDir);
    saveDialog->setNameFilters(filters);
    if (filters.contains(defaultFilter))
        saveDialog->selectNameFilter(defaultFilter);
    saveDialog->selectFile(QDir(targetDir).filePath(defaultFileName));
    saveDialog->setDefaultSuffix(defaultSuffix);
    saveDialog->setAcceptMode(QFileDialog::AcceptSave);
    saveDialog->setFileMode(QFileDialog::AnyFile);
    saveDialog->open();

    connect(saveDialog, &QFileDialog::fileSelected, this,
            [this, editedPixmap](const QString &fileName) {
                QString errorString;
                if (!QVImageCore::savePixmapToFile(editedPixmap, fileName, &errorString)) {
                    QMessageBox::warning(this, tr("Save Failed"),
                                         tr("Could not save the image.\n%1").arg(errorString));
                    return;
                }

                QSettings settings;
                settings.beginGroup(QStringLiteral("recents"));
                settings.setValue(QStringLiteral("lastFileDialogDir"),
                                  QFileInfo(fileName).absolutePath());
                graphicsView->cancelEdit();
            });
}

void MainWindow::replaceOriginalWithEdit(const QPixmap &editedPixmap)
{
    QString errorString;
    if (!graphicsView->replaceCurrentFileWith(editedPixmap, &errorString)) {
        QMessageBox::warning(this, tr("Replace Failed"),
                             tr("Could not replace the original image.\n%1").arg(errorString));
        return;
    }

    graphicsView->finishEditCommit(editedPixmap);
    graphicsView->markCurrentFileSaved();
    refreshProperties();
    updateWindowTitle();
    updateWindowFilePath();
}
