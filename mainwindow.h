#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QSerialPortInfo>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QSettings>
#include "fdc.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_MainWindow_destroyed();
    void baudBoxSlot(int index);
    void portBoxSlot(int index);
    void statusChangedSlot(QString status);
    void messageChangedSlot(QString message);
    void errorMessageSlot(QString title, QString message);
    void trackChangedSlot(quint8 drive, quint16 track);
    void driveChangedSlot(quint8 drive);
    void headChangedSlot(quint8 drive, bool head);
    void mountChangedSlot(quint8 drive, bool mounted, QString filename, quint16 tracks, QString size);
    void mountButtonSlot(quint8 drive);
    void unmountButtonSlot(quint8 drive);

private:
    Ui::MainWindow *ui;
    FDC *fdc;

    void assertError(QString title, QString error);
    QLabel *statusLabel;
    QLabel *messageLabel;
    QList<QSerialPortInfo> serialPorts;
    QComboBox *portBox;
    QComboBox *baudBox;
    QPushButton *mountButton[MAX_DRIVE];
    QPushButton *unmountButton[MAX_DRIVE];
    QLineEdit *fname[MAX_DRIVE];
    QLabel *enaLed[MAX_DRIVE];
    QLabel *hlLed[MAX_DRIVE];
    QPixmap *grnLed;
    QPixmap *redLed;
    QProgressBar *trackBar[MAX_DRIVE];
    QLabel *diskSize[MAX_DRIVE];
    QSettings *settings;
};
#endif // MAINWINDOW_H
