#ifndef FDC_H
#define FDC_H

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QFile>
#include <QTimer>

#define MAX_DRIVE           4
#define CMD_LEN             8           // does not include checksum bytes
#define CRC_LEN             2			// length of CRC
#define CMDBUF_SIZE         CMD_LEN+CRC_LEN
#define TRKBUF_SIZE     	137*32      // maximum valid track length

#define STATE_CMD           0x00        // CMD State
#define STATE_WRIT          0x01        // WRIT State

#define STAT_OK     		0x0000      // OK
#define STAT_NOT_READY		0x0001		// Not Ready
#define STAT_CHECKSUM_ERR	0x0002		// Checksum Error
#define STAT_WRITE_ERR		0x0003		// Write Error

#define FDC_TIMEOUT         2000        // 2000ms timeout

typedef struct FDC_COMMAND {
    union {
        quint8 asBytes[CMDBUF_SIZE];
        struct {
            char command[4];
            union {
                quint16 param1;
                quint16 rcode;
            };
            union {
                quint16 param2;
                quint16 rdata;
            };
            quint16 checksum;
        };
    };
} fdc_command_t;

class FDC : public QObject
{
    Q_OBJECT;

public:
    FDC() noexcept;
    ~FDC();
    bool openPort(QString name, qint32 baudRate);
    void closePort();
    bool setBaud(qint32 baudRate);
    bool mountDisk(qint8 drive, QString filename);
    void unmountDisk(qint8 drive);

private:
    QSerialPort *serialPort;
    QTimer *timeoutTimer;
    quint16 checkSum(quint8 *data, quint16 size);
    bool statResponse(fdc_command_t *cmd);
    bool readTrack(fdc_command_t *cmd);
    bool writeResponse(fdc_command_t *cmd);
    bool writeTrack(fdc_command_t *cmd);
    qint16 updateTrack(qint16 driveNum, qint16 trackNum);
    quint16 statPkts;
    quint16 readPkts;
    quint16 writePkts;
    quint16 crcErrs;
    quint16 outPkts;
    fdc_command_t *cmd;
    qint8 state;
    bool connected;
    quint8 cmdBuf[CMDBUF_SIZE];
    quint8 tmpBuf[TRKBUF_SIZE + CRC_LEN];
    quint8 inBuf[TRKBUF_SIZE + CRC_LEN];
    quint8 outBuf[TRKBUF_SIZE + CRC_LEN];
    qint64 tmpBufIdx;
    qint64 inBufIdx;
    qint64 outBufIdx;
    qint64 outBufSent;
    quint8 driveSize[MAX_DRIVE];
    quint16 maxTrack[MAX_DRIVE];
    quint16 curTrack[MAX_DRIVE];
    quint8 headStatus[MAX_DRIVE];
    quint8 driveSelected;
    quint8 mountStatus[MAX_DRIVE];
    QIODevice::OpenMode openMode[MAX_DRIVE];
    QFile *driveFile[MAX_DRIVE];

private slots:
    void readData();
    void sentData(qint64 bytes);
    void timeoutSlot();

signals:
    void statusChanged(QString status);
    void messageChanged(QString message);
    void errorMessage(QString title, QString message);
    void trackChanged(quint8 drive, quint16 track);
    void headChanged(quint8 drive, bool head);
    void driveChanged(quint8 drive);
    void mountChanged(qint8 drive, bool mounted, QString filename, quint16 tracks, QString size);
};

#endif // FDC_H
