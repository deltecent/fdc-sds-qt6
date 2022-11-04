/**********************************************************************************
*
*  Altair FDC+ Serial Disk Server
*      This program serves Altair disk images over a high speed serial port
*      for computers running the FDC+ Enhanced Floppy Disk Controller.
*
*     Version     Date        Author         Notes
*      1.0     10/27/2022     P. Linstruth   Original
*
***********************************************************************************
*
* This version is based on the FDC+ Serial Drive Server 1.3 by Mike Douglas
*
***********************************************************************************
*
*  Communication with the server is over a serial port at 403.2K Baud, 8N1.
*  All transactions are initiated by the FDC. The second choice for baud rate
*  is 460.8K. Finally, 230.4K is the most likely supported baud rate on the PC
*  if 403.2K and 460.8K aren't avaialable.
*
*  403.2K is the preferred rate as it allows full-speed operation and is the
*  most accurate of the three baud rate choices on the FDC. 460.8K also allows
*  full speed operation, but the baud rate is off by about 3.5%. This works, but
*  is borderline. 230.4K is available on most all serial ports, is within 2% of
*  the FDC baud rate, but runs at 80%-90% of real disk speed.
*
*  FDC TO SERVER COMMANDS
*    Commands from the FDC to the server are fixed length, ten byte messages. The
*    first four bytes are a command in ASCII, the remaining six bytes are grouped
*    as three 16 bit words (little endian). The checksum is the 16 bit sum of the
*    first eight bytes of the message.
*
*    Bytes 0-3   Bytes 4-5 as Word   Bytes 6-7 as Word   Bytes 8-9 as Word
*    ---------   -----------------   -----------------   -----------------
*     Command       Parameter 1         Parameter 2           Checksum
*
*    Commands:
*      STAT - Provide and request drive status. The FDC sends the selected drive
*             number and head load status in Parameter 1 and the current track
*             number in Parameter 2. The Server responds with drive mount status
*             (see below). The LSB of Parameter 1 contains the currently selected
*             drive number or 0xff is no drive is selected. The MSB of parameter 1
;             is non-zero if the head is loaded, zero if not loaded.
*
*             The FDC issues the STAT command about ten times per second so that
*             head status and track number information is updated quickly. The
*             server may also want to assume the drive is selected, the head is
*             loaded, and update the track number whenever a READ is received.
*
*      READ - Read specified track. Parameter 1 contains the drive number in the
*             MSNibble. The lower 12 bits contain the track number. Transfer length
*             length is in Parameter 2 and must be the track length. Also see
*             "Transfer of Track Data" below.
*
*      WRIT - Write specified track. Parameter 1 contains the drive number in the
*             MSNibble. The lower 12 bits contain the track number. Transfer length
*             must be track length. Server responds with WRIT response when ready
*             for the FDC to send the track of data. See "Transfer of Track Data" below.
*
*
*  SERVER TO FDC
*    Reponses from the server to the FDC are fixed length, ten byte messages. The
*    first four bytes are a response command in ASCII, the remaining six bytes are
*    grouped as three 16 bit words (little endian). The checksum is the 16 bit sum
*    of the first eight bytes of the message.
*
*    Bytes 0-3   Bytes 4-5 as Word   Bytes 6-7 as Word   Bytes 8-9 as Word
*    ---------   -----------------   -----------------   -----------------
*     Command      Response Code        Reponse Data          Checksum
*
*    Commands:
*      STAT - Returns drive status in Response Data with one bit per drive. "1" means a
*             drive image is mounted, "0" means not mounted. Bits 15-0 correspond to
*             drive numbers 15-0. Response code is ignored by the FDC.
*
*      WRIT - Issued in repsonse to a WRIT command from the FDC. This response is
*             used to tell the FDC that the server is ready to accept continuous transfer
*             of a full track of data (response code word set to "OK." If the request
*             can't be fulfilled (e.g., specified drive not mounted), the reponse code
*             is set to NOT READY. The Response Data word is don't care.
*
*      WSTA - Final status of the write command after receiving the track data is returned
*             in the repsonse code field. The Response Data word is don't care.
*
*    Reponse Code:
*      0x0000 - OK
*      0x0001 - Not Ready (e.g., write request to unmounted drive)
*      0x0002 - Checksum error (e.g., on the block of write data)
*      0x0003 - Write error (e.g., write to disk failed)
*
*
*  TRANSFER OF TRACK DATA
*    Track data is sent as a sequence of bytes followed by a 16 bit, little endian
*    checksum. Note the Transfer Length field does NOT include the two bytes of
*    the checksum. The following notes apply to both the FDC and the server.
*
*  ERROR RECOVERY
*    The FDC uses a timeout of one second after the last byte of a message or data block
*        is sent to determine if a transmission was ignored.
*
*    The server should ignore commands with an invalid checksum. The FDC may retry the
*        command if no response is received. An invalid checksum on a block of write
*        data should not be ignored, instead, the WRIT response should have the
*        Reponse Code field set to 0x002, checksum error.
*
*    The FDC ignores responses with an invalid checksum. The FDC may retry the command
*        that generated the response by sending the command again.
*
***********************************************************************************/

#include <QFileInfo>
#include <QDebug>
#include "fdc.h"

FDC::FDC() noexcept
{
    serialPort = new QSerialPort();

    for (int drive = 0; drive < MAX_DRIVE; drive++) {
        driveSize[drive] = 0;
        maxTrack[drive] = 77;
        curTrack[drive] = 0;
        headStatus[drive] = 0;
        mountStatus[drive] = false;
        driveFile[drive] = new QFile();
    }

    driveSelected = 0xff;

    statPkts = 0;
    outPkts = 0;
    readPkts = 0;
    writePkts = 0;
    crcErrs = 0;
    tmpBufIdx = 0;
    inBufIdx = 0;
    outBufIdx = 0;

    connected = false;

    state = STATE_CMD;

    // Start timeout timer
    timeoutTimer = new QTimer(this);
    timeoutTimer->setTimerType(Qt::VeryCoarseTimer);
    connect(timeoutTimer, &QTimer::timeout, this, &FDC::timeoutSlot);
    timeoutTimer->start(FDC_TIMEOUT);	// Start timeout timer
}

FDC::~FDC()
{
    delete serialPort;
}

bool FDC::openPort(QString name, qint32 baudRate)
{
    bool r;

    if (serialPort->isOpen()) {
        closePort();
    }

    serialPort->setPortName(name);

    serialPort->setDataBits(QSerialPort::Data8);
    serialPort->setParity(QSerialPort::NoParity);
    serialPort->setStopBits(QSerialPort::OneStop);

    if ((r = serialPort->open(QIODeviceBase::ReadWrite))) {

        if ((r = setBaud(baudRate)) == false) {
            qDebug() << name << serialPort->errorString();
            closePort();
        }
        else {
            serialPort->setDataTerminalReady(true);
            serialPort->setRequestToSend(true);

            connect(serialPort, &QSerialPort::readyRead, this, &FDC::readData);
            connect(serialPort, &QSerialPort::bytesWritten, this, &FDC::sentData);

            connected = true;

            emit statusChanged(tr("Online"));
        }
    }
    else {
        qDebug() << name << serialPort->errorString();
    }

    return r;
}

void FDC::closePort()
{
    if (serialPort->isOpen()) {
        serialPort->disconnect(this);
        serialPort->close();

        connected = false;

        emit statusChanged(tr("Offline"));
    }
}

bool FDC::setBaud(qint32 baudRate)
{
    bool r;

    if ((r = serialPort->setBaudRate(baudRate)) == false) {
        emit errorMessage(tr("COM Port Error"), QString("Could not set %1 baudrate to %2").arg(serialPort->portName()).arg(baudRate));
        emit statusChanged(tr("Offline"));
        qDebug() << "Serial Port Error" << QString("Could not set baudrate to %1").arg(baudRate);
    }

    return r;
}

void FDC::timeoutSlot()
{
    if (serialPort->isOpen()) {
        serialPort->clear();

        tmpBufIdx = 0;

        if (connected) {
            connected = false;

            emit statusChanged(tr("Communications timeout"));
        }

        qDebug() << "TIMEOUT";
    }
    else {
        emit statusChanged(tr("Offline"));
    }

    state = STATE_CMD;
}

void FDC::sentData(qint64 bytes)
{

}

void FDC::readData()
{
    qint64 r;

    if (serialPort->bytesAvailable() <= sizeof(tmpBuf) - tmpBufIdx) {
        r = serialPort->read((char *) &tmpBuf[tmpBufIdx], sizeof(tmpBuf) - tmpBufIdx);
    }
    else {
        serialPort->clear();

        tmpBufIdx = 0;

        emit errorMessage("readData", "tmpBuf Full");

        qDebug() << "tmpBuf Full";

        return;
    }

    if (r) {
        tmpBufIdx += r;
    }

//    qDebug() << "readData" << r << tmpBufIdx;

    switch (state) {
        case STATE_CMD:
            if (tmpBufIdx == CMDBUF_SIZE) {
                memcpy(cmdBuf, tmpBuf, CMDBUF_SIZE);
                cmd = (fdc_command_t *) cmdBuf;

                if (cmd->checksum == checkSum(cmd->asBytes, CMD_LEN)) {
                    if (memcmp(cmd->command, "STAT", 4) == 0) {
                        statPkts++;

                        quint8 newDrive = cmd->param1 & 0xff;  // LSB

                        if (newDrive <= MAX_DRIVE && driveSelected != newDrive) {
                            if (headStatus[driveSelected]) {
                                headStatus[driveSelected] = false;
                                emit headChanged(driveSelected, headStatus[driveSelected]);
                            }

                            emit driveChanged(newDrive);
                        }

                        if (newDrive <= MAX_DRIVE) {
                            if (headStatus[newDrive] != (cmd->param1 >> 8)) {
                                headStatus[newDrive] = cmd->param1 >> 8; // MSB
                                emit headChanged(newDrive, headStatus[newDrive]);
                            }

                            updateTrack(newDrive, cmd->param2);
                        }

                        driveSelected = newDrive;

                        statResponse(cmd);

                        if ((statPkts) % 10 == 0) {
                            qDebug() << "statPkts" << statPkts << "readPkts" << readPkts << "writePkts" << writePkts << "outPkts" << outPkts << "crcErrs" << crcErrs;
                        }
                    }
                    else if (memcmp(cmd->command, "READ", 4) == 0) {
                        readPkts++;
                        readTrack(cmd);
                    }
                    else if (memcmp(cmd->command, "WRIT", 4) == 0) {
                        writeResponse(cmd);
                        writePkts++;
                        state = STATE_WRIT;
                    }
                }
                else {
                    crcErrs++;

                    qDebug() << "CRC Error";
                }

                tmpBufIdx = 0;
            }
            break;

        case STATE_WRIT:
            if (tmpBufIdx == TRKBUF_SIZE + CRC_LEN) {
                cmd = (fdc_command_t *) cmdBuf;
                memcpy(inBuf, tmpBuf, sizeof(inBuf));

                writeTrack(cmd);

                tmpBufIdx = 0;
                state = STATE_CMD;
            }

            break;
    }
}

quint16 FDC::checkSum(quint8 *data, quint16 size)
{
    quint16 checksum = 0;

    for (quint16 i = 0; i < size; i++) {
        checksum += data[i];
    }

//    qDebug() << "checksum calc" << checksum;

    return checksum;
}

bool FDC::statResponse(fdc_command_t *cmd)
{
    cmd->rdata = 0;

    for (int drive = 0; drive < MAX_DRIVE; drive++) {
        cmd->rdata |= (driveFile[drive]->isOpen() << drive);
    }
    cmd->rcode = STAT_OK;
    cmd->checksum = checkSum(cmd->asBytes, CMD_LEN);

    serialPort->write((char *) cmd, CMDBUF_SIZE);

//    qDebug() << "STAT RESP rdata" << cmd->rdata;
    timeoutTimer->start(FDC_TIMEOUT);	// restart timeout timer

    if (connected == false) {
        connected = true;

        emit statusChanged(tr("Connected"));
    }

    outPkts++;

    return false;
}

bool FDC::readTrack(fdc_command_t *cmd) {
    qint8 driveNum = cmd->param1 >> 12;
    qint16 trackNum = cmd->param1 & 0x0fff;
    qint16 trackLen = cmd->param2;
    quint16 checksum = 0;
    qint64 bytesRead;

    qDebug() << "READ TRACK" << driveNum << trackNum << trackLen;

    if (driveNum >= MAX_DRIVE) {
        emit errorMessage("READ", QString("Drive number %1 is out of range").arg(driveNum));
        return true;
    }

    /* Bounds checking */
    if (trackLen > sizeof(outBuf)) {
        qDebug() << "trackLen" << trackLen << "> outBuf" << sizeof(outBuf);

        trackLen = sizeof(outBuf);
    }

    trackNum = updateTrack(driveNum, trackNum);

    /* Seek to track */
    driveFile[driveNum]->seek(trackNum * trackLen);

    /* Read track into outBuf */
    if ((bytesRead = driveFile[driveNum]->read((char *) outBuf, trackLen)) != trackLen) {
        qDebug() << QString("read() failed - read %1 of %2 bytes").arg(bytesRead).arg(trackLen);
    }
    else {
        checksum = checkSum(outBuf, trackLen);
    }

    serialPort->write((char *) outBuf, trackLen);
    serialPort->write((char *) &checksum, sizeof(checksum));

    outPkts++;

    timeoutTimer->start(FDC_TIMEOUT);	// restart timeout timer

    return false;
}

bool FDC::writeResponse(fdc_command_t *cmd) {
    qint8 driveNum = cmd->param1 >> 12;
    qint16 trackNum = cmd->param1 & 0x0fff;
    qint16 trackLen = cmd->param2;
    qint64 bytesWritten;

    if (driveNum >= MAX_DRIVE) {
        emit errorMessage("WRIT", QString("Drive number %1 is out of range").arg(driveNum));
        return true;
    }

    /* Save command buffer for write track */
    memcpy(outBuf, cmd, CMD_LEN);
    cmd = (fdc_command_t *) outBuf;

    cmd->rcode = (driveFile[driveNum]->isOpen()) ? STAT_OK : STAT_NOT_READY;
    cmd->checksum = checkSum(cmd->asBytes, CMD_LEN);

    serialPort->write((char *) cmd, CMDBUF_SIZE);

    timeoutTimer->start(FDC_TIMEOUT);	// restart timeout timer

    qDebug() << "WRIT RESP" << driveNum << trackNum << trackLen << cmd->rcode;

    return false;
}

bool FDC::writeTrack(fdc_command_t *cmd) {
    qint8 driveNum = cmd->param1 >> 12;
    qint16 trackNum = cmd->param1 & 0x0fff;
    qint16 trackLen = cmd->param2;
    quint16 checksum = inBuf[trackLen] | ((quint16) inBuf[trackLen+1] << 8);
    qint64 bytesWritten;

    qDebug() << QString().asprintf("WRIT TRACK %d %d %d %04X %02X %02X", driveNum, trackNum, trackLen, checksum, inBuf[trackLen], inBuf[trackLen+1]);

    if (driveNum >= MAX_DRIVE) {
        emit errorMessage("WRIT", QString("Drive number %1 is out of range").arg(driveNum));
        return true;
    }


    if (driveFile[driveNum]->isOpen() == false) {
        cmd->rcode = STAT_NOT_READY;
    }
    else if (checksum != checkSum((quint8 *) inBuf, trackLen)) {
        cmd->rcode = STAT_CHECKSUM_ERR;

        crcErrs++;

        qDebug() << "CRC Error";
    }
    else {
        /* Bounds checking */
        if (trackLen > sizeof(inBuf)) {
            qDebug() << "trackLen" << trackLen << "> inBuf" << sizeof(inBuf);

            trackLen = sizeof(inBuf);
        }

        trackNum = updateTrack(driveNum, trackNum);

        /* Seek to track */
        driveFile[driveNum]->seek(trackNum * trackLen);

        /* Write inBuf onto track */
        if ((bytesWritten = driveFile[driveNum]->write((char *) inBuf, trackLen)) != trackLen) {
            cmd->rcode = STAT_WRITE_ERR;
            qDebug() << QString("write() failed - read %1 of %2 bytes").arg(bytesWritten).arg(trackLen);
        }
        else {
            cmd->rcode = STAT_OK;
        }
    }

    cmd->command[0] = 'W';
    cmd->command[1] = 'S';
    cmd->command[2] = 'T';
    cmd->command[3] = 'A';
    cmd->checksum = checkSum(cmd->asBytes, CMD_LEN);

    memcpy(outBuf, cmd, CMDBUF_SIZE);

    serialPort->write((char *) outBuf, CMDBUF_SIZE);

    qDebug() << "WSTA" << driveNum << trackNum << trackLen << cmd->rcode;

    outPkts++;

    return (cmd->rcode != STAT_OK);
}

qint16 FDC::updateTrack(qint16 drive, qint16 track)
{
    if (drive >= MAX_DRIVE) {
        emit errorMessage("updateTrack", QString("Drive number %1 is out of range").arg(drive));
        return track;
    }

    if (driveFile[drive]->isOpen() == false) {
        track = 0;
    }

    if (track != curTrack[drive]) {
        curTrack[drive] = track;

        //Send seek signal
        emit trackChanged(drive, track);
    }

    return track;
}

bool FDC::mountDisk(qint8 drive, QString filename)
{
    bool r;
    QString size;

    qDebug() << "mount" << drive << curTrack[drive] << driveFile[drive]->isOpen() << driveFile[drive]->openMode();

    if (driveFile[drive]->isOpen()) {
        driveFile[drive]->close();
    }

    driveFile[drive]->setFileName(filename);

    if ((r = driveFile[drive]->open(QIODeviceBase::ReadWrite)) == true) {
        qint64 filesize = driveFile[drive]->size();

        if (filesize == 76800) {
            maxTrack[drive] = 34;
            size = "75K";
        }
        else if (filesize == 337664) {
            maxTrack[drive] = 76;
            size = "330K";
        }
        else if (filesize == 8978432){
            maxTrack[drive] = 2047;
            size = "8MB";
        }
        else {
            maxTrack[drive] = 2047;
            size = "???";
        }

        updateTrack(drive, 0);

        emit mountChanged(drive, true, filename, maxTrack[drive], size);
    }

    return r;
}

void FDC::unmountDisk(qint8 drive)
{
    qDebug() << "umount" << drive << curTrack[drive] << driveFile[drive]->isOpen();

    if (driveFile[drive]->isOpen()) {
        updateTrack(drive, 0);

        driveFile[drive]->close();
    }

    emit mountChanged(drive, false, nullptr, 0, nullptr);

    qDebug() << "umount" << drive << curTrack[drive] << driveFile[drive]->isOpen();
}
