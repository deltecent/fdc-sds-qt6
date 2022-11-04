#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "fdc.h"
#include "qlineedit.h"
#include "redledx18.xpm"
#include "offledx18.xpm"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    fdc = new FDC;

    settings = new QSettings("Deltec", "FDC+");

    // Pixmaps
    redLed = new QPixmap(redled_xpm);
    offLed = new QPixmap(offled_xpm);

    // Status and Message Labels
    statusLabel = this->findChild<QLabel *>(QString("statusLabel"));
    messageLabel = this->findChild<QLabel *>(QString("messageLabel"));

    // Track Bars
    for (int drive = 0; drive < MAX_DRIVE; drive++) {
        trackBar[drive] = this->findChild<QProgressBar *>(QString("progressBar%1").arg(drive));
        trackBar[drive]->setValue(0);
        enaLed[drive] = this->findChild<QLabel *>(QString("ena%1").arg(drive));
        enaLed[drive]->setPixmap(*offLed);
        hlLed[drive] = this->findChild<QLabel *>(QString("hl%1").arg(drive));
        hlLed[drive]->setPixmap(*offLed);
        fname[drive] = this->findChild<QLineEdit *>(QString("fname%1").arg(drive));
        mountButton[drive] = this->findChild<QPushButton *>(QString("mountButton%1").arg(drive));
        unmountButton[drive] = this->findChild<QPushButton *>(QString("unmountButton%1").arg(drive));
        diskSize[drive] = this->findChild<QLabel *>(QString("size%1").arg(drive));

        connect(mountButton[drive], &QPushButton::clicked, [this, drive] { mountButtonSlot(drive); });
        connect(unmountButton[drive], &QPushButton::clicked, [this, drive] { unmountButtonSlot(drive); });
    }

    portBox = this->findChild<QComboBox *>(QString("portBox"));
    portBox->addItem(tr("None"));
    serialPorts = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : serialPorts) {
        portBox->addItem(info.portName());
    }
    portBox->setCurrentIndex(portBox->findText(settings->value("port", "").toString()));
    connect(portBox, &QComboBox::currentIndexChanged, this, &MainWindow::portBoxSlot);

    baudBox = this->findChild<QComboBox *>(QString("baudBox"));
    baudBox->addItem("230.4K", 230400);
    baudBox->addItem("403.2K", 403200);
    baudBox->addItem("460.8K", 460800);
    baudBox->setCurrentIndex(settings->value("baud", 1).toInt());    // Default to 403.2K
    connect(baudBox, &QComboBox::currentIndexChanged, this, &MainWindow::baudBoxSlot);

    connect(fdc, &FDC::statusChanged, this, &MainWindow::statusChangedSlot);
    connect(fdc, &FDC::messageChanged, this, &MainWindow::messageChangedSlot);
    connect(fdc, &FDC::errorMessage, this, &MainWindow::errorMessageSlot);
    connect(fdc, &FDC::mountChanged, this, &MainWindow::mountChangedSlot);
    connect(fdc, &FDC::trackChanged, this, &MainWindow::trackChangedSlot);
    connect(fdc, &FDC::driveChanged, this, &MainWindow::driveChangedSlot);
    connect(fdc, &FDC::headChanged, this, &MainWindow::headChangedSlot);

    // If a port has been saved, try and open it
    if (portBox->currentIndex() != -1) {
        fdc->openPort(portBox->itemText(portBox->currentIndex()), baudBox->itemData(baudBox->currentIndex()).toInt());
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_MainWindow_destroyed()
{
    fdc->closePort();
    fdc->unmountDisk(0);
}

void MainWindow::portBoxSlot(int index) {
    if (index > 0) {
        fdc->openPort(portBox->itemText(index), baudBox->itemData(baudBox->currentIndex()).toInt());
    }
    else {
        fdc->closePort();
    }

    settings->setValue("port", portBox->itemText(index));
}

void MainWindow::baudBoxSlot(int index) {
    fdc->setBaud(baudBox->itemData(index).toInt());
    settings->setValue("baud", index);
}

void MainWindow::mountButtonSlot(quint8 drive) {
    QString filename = QFileDialog::getOpenFileName(this, tr("Open Disk Image"), settings->value("path", "").toString(), tr("Disk Image Files (*.dsk);;All Files (*.*)"));

    if (filename.length()) {
        QFileInfo finfo(filename);

        settings->setValue("path", finfo.filePath());

        if (fdc->mountDisk(drive, filename)) {
            mountButton[drive]->setEnabled(false);
            unmountButton[drive]->setEnabled(true);
        }
        else {
            assertError(tr("Could not open file"), tr("Unable to open file"));
        }
    }
}

void MainWindow::unmountButtonSlot(quint8 drive) {
    fdc->unmountDisk(drive);

    mountButton[drive]->setEnabled(true);
    unmountButton[drive]->setEnabled(false);
}

void MainWindow::statusChangedSlot(QString status) {
        statusLabel->setText(status);
}

void MainWindow::messageChangedSlot(QString message) {
        messageLabel->setText(message);
}

void MainWindow::errorMessageSlot(QString title, QString message) {
    QMessageBox::critical(this, title, message);
}

void MainWindow::mountChangedSlot(quint8 drive, bool mounted, QString filename, quint16 tracks, QString size) {

    if (drive < MAX_DRIVE) {
        if (mounted) {
            trackBar[drive]->setMaximum(tracks);
            trackBar[drive]->setValue(0);
            diskSize[drive]->setText(size);
            fname[drive]->setText(filename);
            fname[drive]->setEnabled(true);
        }
        else {
            diskSize[drive]->clear();
            fname[drive]->clear();
            fname[drive]->setEnabled(false);
        }
        qDebug() << drive << mounted << filename << tracks << size;
    }
    else {
        assertError(tr("mountChanged"), QString("Drive number %1 is out of range").arg(drive));
    }
}

void MainWindow::trackChangedSlot(quint8 drive, quint16 track) {
    qDebug() << "Track changed slot" << drive << track;

    if (drive < MAX_DRIVE) {
        trackBar[drive]->setValue(track);
    }
    else {
        assertError(tr("trackChanged"), QString("Drive number %1 is out of range").arg(drive));
    }
}

void MainWindow::driveChangedSlot(quint8 drive) {
    for (int i = 0; i < MAX_DRIVE; i++) {
        enaLed[i]->setPixmap((i == drive) ? *redLed : *offLed);
    }
}

void MainWindow::headChangedSlot(quint8 drive, bool head) {
    if (drive <= MAX_DRIVE) {
        hlLed[drive]->setPixmap((head) ? *redLed : *offLed);
    }
    else {
        assertError(tr("headChanged"), QString("Drive number %1 is out of range").arg(drive));
    }
}

void MainWindow::assertError(QString title, QString error) {
    QMessageBox::warning(this, title, error, QMessageBox::Ok);
}
