/*
    SPDX-FileCopyrightText: 2016 Jasem Mutlaq <mutlaqja@ikarustech.com>

    Adapted from https://wiki.qt.io/Download_Data_from_URL

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "filedownloader.h"

#ifndef KSTARS_LITE
#include "kstars.h"
#endif

#include "kstars_debug.h"

#include <KLocalizedString>

#include <QFile>
#include <QProgressDialog>

FileDownloader::FileDownloader(QObject *parent) : QObject(parent)
{
    connect(&m_WebCtrl, SIGNAL(finished(QNetworkReply*)), this, SLOT(dataFinished(QNetworkReply*)));

    registerDataVerification([](const QByteArray &) { return true; });
    registerFileVerification([](const QString &) { return true;});
}

void FileDownloader::get(const QUrl &fileUrl)
{
    QNetworkRequest request(fileUrl);
    m_DownloadedData.clear();
    isCancelled = false;
    m_Reply     = m_WebCtrl.get(request);

    connect(m_Reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(slotError()));
    connect(m_Reply, SIGNAL(downloadProgress(qint64,qint64)), this, SIGNAL(downloadProgress(qint64,qint64)));
    connect(m_Reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(setDownloadProgress(qint64,qint64)));
    connect(m_Reply, SIGNAL(readyRead()), this, SLOT(dataReady()));

    setDownloadProgress(0, 0);
}

void FileDownloader::post(const QUrl &fileUrl, QByteArray &data)
{
    QNetworkRequest request(fileUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/x-www-form-urlencoded"));
    m_DownloadedData.clear();
    isCancelled = false;
    m_Reply     = m_WebCtrl.post(request, data);

    connect(m_Reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(slotError()));
    connect(m_Reply, SIGNAL(downloadProgress(qint64,qint64)), this, SIGNAL(downloadProgress(qint64,qint64)));
    connect(m_Reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(setDownloadProgress(qint64,qint64)));
    connect(m_Reply, SIGNAL(readyRead()), this, SLOT(dataReady()));

    setDownloadProgress(0, 0);
}

void FileDownloader::post(const QUrl &fileUrl, QHttpMultiPart *parts)
{
    QNetworkRequest request(fileUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/x-www-form-urlencoded"));
    m_DownloadedData.clear();
    isCancelled = false;
    m_Reply     = m_WebCtrl.post(request, parts);

    connect(m_Reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(slotError()));
    connect(m_Reply, SIGNAL(downloadProgress(qint64,qint64)), this, SIGNAL(downloadProgress(qint64,qint64)));
    connect(m_Reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(setDownloadProgress(qint64,qint64)));
    connect(m_Reply, SIGNAL(readyRead()), this, SLOT(dataReady()));

    setDownloadProgress(0, 0);
}

void FileDownloader::dataReady()
{
    if (m_downloadTemporaryFile.isOpen())
        m_downloadTemporaryFile.write(m_Reply->readAll());
    else
        m_DownloadedData += m_Reply->readAll();
}

void FileDownloader::dataFinished(QNetworkReply *pReply)
{
    if (pReply->error() != QNetworkReply::NoError)
        return;

    dataReady();

    if (m_verifyData(m_DownloadedData) == false)
    {
        emit error(i18n("Data verification failed"));
        pReply->deleteLater();
        return;
    }
    else if (m_downloadTemporaryFile.isOpen())
    {
        m_downloadTemporaryFile.flush();
        m_downloadTemporaryFile.close();

        if (m_verifyFile(m_downloadTemporaryFile.fileName()) == false)
        {
            emit error(i18n("File verification failed"));
            pReply->deleteLater();
            return;
        }
        else
        {
            QFile::remove(m_DownloadedFileURL.toLocalFile());
            m_downloadTemporaryFile.copy(m_DownloadedFileURL.toLocalFile());
        }
    }

    if (isCancelled == false)
        emit downloaded();

    pReply->deleteLater();
}

void FileDownloader::slotError()
{
    m_Reply->deleteLater();

#ifndef KSTARS_LITE
    if (progressDialog != nullptr)
        progressDialog->hide();
#endif

    if (isCancelled)
    {
        // Remove partially downloaded file, should we download to %tmp first?
        if (m_downloadTemporaryFile.isOpen())
        {
            m_downloadTemporaryFile.close();
            m_downloadTemporaryFile.remove();
        }
        emit canceled();
    }
    else
    {
        emit error(m_Reply->errorString());
    }
}

void FileDownloader::setProgressDialogEnabled(bool ShowProgressDialog, const QString &textTitle,
                                              const QString &textLabel)
{
    m_ShowProgressDialog = ShowProgressDialog;

    if (title.isEmpty())
        title = i18n("Downloading");
    else
        title = textTitle;

    if (textLabel.isEmpty())
        label = i18n("Downloading Data...");
    else
        label = textLabel;
}

QUrl FileDownloader::getDownloadedFileURL() const
{
    return m_DownloadedFileURL;
}

bool FileDownloader::setDownloadedFileURL(const QUrl &DownloadedFile)
{
    m_DownloadedFileURL = DownloadedFile;

    if (m_DownloadedFileURL.isEmpty() == false)
    {
        bool rc= m_downloadTemporaryFile.open();

        if (rc == false)
            qCWarning(KSTARS) << m_downloadTemporaryFile.errorString();
        else
            qCDebug(KSTARS) << "Opened" << m_downloadTemporaryFile.fileName() << "to download data into" << DownloadedFile.toLocalFile();

        return rc;
    }

    return true;
}

void FileDownloader::setDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
#ifndef KSTARS_LITE
    if (m_ShowProgressDialog)
    {
        if (progressDialog == nullptr)
        {
            isCancelled    = false;
            progressDialog = new QProgressDialog(KStars::Instance());
            progressDialog->setWindowTitle(title);
            progressDialog->setLabelText(i18n("Awaiting response from server..."));
            connect(progressDialog, SIGNAL(canceled()), this, SIGNAL(canceled()));
            connect(progressDialog, &QProgressDialog::canceled, this, [&]() {
                isCancelled = true;
                m_Reply->abort();
                progressDialog->close();
            });
            progressDialog->setMinimum(0);
            progressDialog->setMaximum(0);
            progressDialog->show();
            progressDialog->raise();
        }

        if (bytesReceived > 0)
        {
            progressDialog->setLabelText(label);
        }

        if (bytesTotal > 0)
        {
            progressDialog->setMaximum(bytesTotal);
            progressDialog->setValue(bytesReceived);
        }
        else
        {
            progressDialog->setMaximum(0);
        }
    }
#else
    Q_UNUSED(bytesReceived);
    Q_UNUSED(bytesTotal);
#endif
}

QByteArray FileDownloader::downloadedData() const
{
    return m_DownloadedData;
}
