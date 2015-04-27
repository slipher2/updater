#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "currentversionfetcher.h"
#include "downloadworker.h"
#include "settingsdialog.h"
#include "newsfetcher.h"
#include "aria2/src/includes/aria2/aria2.h"
#include "system.h"

#include <QDir>
#include <QStandardItemModel>
#include <QDebug>
#include <QProcess>


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    worker(nullptr),
    textBrowser(new QLabel(this)),
    newsFetcher(new NewsFetcher(ui->scrollAreaWidgetContents)),
    currentVersionFetcher(new CurrentVersionFetcher(this)),
    totalSize(0),
    paused(false),
    commandRegex("%command%"),
    networkManager(this)
{
    ui->setupUi(this);
    ui->progressBar->setValue(0);
    connect(ui->actionVerify, SIGNAL(triggered()), this, SLOT(startUpdate()));
    connect(ui->actionQuit, SIGNAL(triggered()), this, SLOT(close()));
    connect(ui->actionSettings, SIGNAL(triggered()), this, SLOT(openSettings()));
    connect(ui->changeInstallButton, SIGNAL(clicked()), this, SLOT(openSettings()));
    if (!settings.contains(Settings::INSTALL_PATH)) {
        settings.setValue(Settings::INSTALL_PATH, Sys::defaultInstallPath());
    }
    if (!settings.contains(Settings::COMMAND_LINE)) {
        settings.setValue(Settings::COMMAND_LINE, "%command%");
    }
    ui->installLocation->setText(settings.value(Settings::INSTALL_PATH).toString());
    ui->horizontalWidget->hide();
    ui->gridLayout->addWidget(textBrowser.get(), 4, 0, 1, 1);
    ui->updateButton->setIcon(QIcon(":images/ic_play_arrow_black_48dp.png"));
    ui->updateButton->setIconSize({20, 20});
    if (networkManager.isOnline()) {
        connect(newsFetcher.get(), SIGNAL(newsItemsLoaded(QStringList)), this, SLOT(onNewsLoaded(QStringList)));
        connect(currentVersionFetcher.get(), SIGNAL(onCurrentVersion(QString)), this, SLOT(onCurrentVersion(QString)));
        newsFetcher->get("https://www.unvanquished.net/?cat=3&json=1");
        currentVersionFetcher->fetchCurrentVersion("https://dl.unvanquished.net/current.txt");
    } else {
        if (settings.contains(Settings::CURRENT_VERSION)) {
            connect(ui->updateButton, SIGNAL(clicked()), this, SLOT(startGame()));
            textBrowser->setText("No internet connection. Press > to play the game anyways.");
        } else {
            textBrowser->setText("No internet connection and game not installed. Please fix.");
        }
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

bool MainWindow::close(void)
{
    stopAria();
    return QMainWindow::close();
}


void MainWindow::openSettings(void)
{
    SettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::onCurrentVersion(QString version)
{
    if (version.isEmpty()) {
        connect(ui->updateButton, SIGNAL(clicked()), this, SLOT(startGame()));
        textBrowser->setText("Invalid version received. Press > to play the game anyways.");
    } else if (settings.value(Settings::CURRENT_VERSION).toString() != version) {
        currentVersion = version;
        startUpdate();
    } else {
        connect(ui->updateButton, SIGNAL(clicked()), this, SLOT(startGame()));
        textBrowser->setText("Up to date. Press > to play the game.");
    }
}

void MainWindow::startUpdate(void)
{
    QString installDir = settings.value(Settings::INSTALL_PATH).toString();
    QDir dir(installDir);
    if (!dir.exists()) {
        if (!dir.mkpath(dir.path())) {
            textBrowser->setText(dir.canonicalPath() + " does not exist and could not be created");
            openSettings();
            return;
        }
    }
    if (!QFileInfo(installDir).isWritable()) {
        textBrowser->setText("Install dir not writable. Please select another");
        return;
    }
    ui->actionVerify->setEnabled(false);
    ui->horizontalWidget1->hide();
    textBrowser->setText("Installing to " + dir.canonicalPath());
    ui->updateButton->setIcon(QIcon(":images/ic_pause_black_48dp.png"));
    ui->updateButton->setIconSize({20, 20});

    worker = new DownloadWorker();
    worker->setDownloadDirectory(dir.canonicalPath().toStdString());
    worker->addUri("http://cdn.unvanquished.net/current.torrent");
    worker->moveToThread(&thread);
    connect(&thread, SIGNAL(finished()), worker, SLOT(deleteLater()));
    connect(worker, SIGNAL(onDownloadEvent(int)), this, SLOT(onDownloadEvent(int)));
    connect(worker, SIGNAL(downloadSpeedChanged(int)), this, SLOT(setDownloadSpeed(int)));
    connect(worker, SIGNAL(uploadSpeedChanged(int)), this, SLOT(setUploadSpeed(int)));
    connect(worker, SIGNAL(totalSizeChanged(int)), this, SLOT(setTotalSize(int)));
    connect(worker, SIGNAL(completedSizeChanged(int)), this, SLOT(setCompletedSize(int)));
    disconnect(ui->updateButton, SIGNAL(clicked()), this, SLOT(startUpdate()));
    connect(ui->updateButton, SIGNAL(clicked()), this, SLOT(toggleDownload()));
    connect(&thread, SIGNAL(started()), worker, SLOT(download()));
    thread.start();
}

void MainWindow::startGame(void)
{
    QString cmd = settings.value(Settings::INSTALL_PATH).toString() + "/" + Sys::executableName();
    QString commandLine = settings.value(Settings::COMMAND_LINE).toString();
    commandLine.replace(commandRegex, cmd);
    QProcess::startDetached(commandLine);
    close();
}

void MainWindow::onNewsLoaded(QStringList news)
{
    QVBoxLayout* layout = new QVBoxLayout(ui->scrollAreaWidgetContents);
    for (int i = 0; i < news.size(); ++i) {
        QLabel* label = new QLabel(ui->scrollAreaWidgetContents);
        label->setText(news[i]);
        label->setWordWrap(true);
        layout->addWidget(label);
    }

    ui->scrollAreaWidgetContents->setLayout(layout);
}


void MainWindow::toggleDownload(void)
{
    worker->toggle();
    paused = !paused;
    if (paused) {
        ui->updateButton->setIcon(QIcon(":images/ic_play_arrow_black_48dp.png"));
        ui->updateButton->setIconSize({20, 20});
    } else {
        ui->updateButton->setIcon(QIcon(":images/ic_pause_black_48dp.png"));
        ui->updateButton->setIconSize({20, 20});
    }
}

void MainWindow::setDownloadSpeed(int speed)
{
    ui->downloadSpeed->setText(sizeToString(speed) + "/s");
}

void MainWindow::setUploadSpeed(int speed)
{
    ui->uploadSpeed->setText(sizeToString(speed) + "/s");
}

void MainWindow::setTotalSize(int size)
{
    ui->totalSize->setText(sizeToString(size));
    totalSize = size;
}

void MainWindow::setCompletedSize(int size)
{
    ui->completedSize->setText(sizeToString(size));
    ui->progressBar->setValue((static_cast<float>(size) / totalSize) * 100);
    ui->horizontalWidget->show();
}

void MainWindow::onDownloadEvent(int event)
{
    switch (event) {
        case aria2::EVENT_ON_BT_DOWNLOAD_COMPLETE:
            ui->updateButton->setIcon(QIcon(":images/ic_play_arrow_black_48dp.png"));
            ui->updateButton->setIconSize({20, 20});
            disconnect(ui->updateButton, SIGNAL(clicked()), this, SLOT(toggleDownload()));
            connect(ui->updateButton, SIGNAL(clicked()), this, SLOT(startGame()));
            setCompletedSize(totalSize);
            setDownloadSpeed(0);
            textBrowser->setText("Up to date. Press > to play the game.");
            stopAria();
            settings.setValue(Settings::CURRENT_VERSION, currentVersion);
            break;

        case aria2::EVENT_ON_DOWNLOAD_COMPLETE:
            textBrowser->setText("Torrent downloaded");
            break;

        case aria2::EVENT_ON_DOWNLOAD_ERROR:
            textBrowser->setText("Error received while downloading");
            break;

        case aria2::EVENT_ON_DOWNLOAD_PAUSE:
            textBrowser->setText("Download paused");
            break;

        case aria2::EVENT_ON_DOWNLOAD_START:
            textBrowser->setText("Download started");
            break;

        case aria2::EVENT_ON_DOWNLOAD_STOP:
            textBrowser->setText("Download stopped");
            break;
    }
}

QString MainWindow::sizeToString(int size)
{
    static QString sizes[] = { "Bytes", "KiB", "MiB", "GiB" };
    const int num_sizes = 4;
    int i = 0;
    while (size > 1024 && i++ < 4) {
        size /= 1024;
    }

    return QString::number(size) + " " + sizes[std::min(i, num_sizes - 1)];
}

void MainWindow::stopAria(void)
{
    if (worker) {
        worker->stop();
        thread.quit();
        thread.wait();
    }
}
