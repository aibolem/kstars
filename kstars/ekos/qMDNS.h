/*
    SPDX-FileCopyrightText: 2016 Alex Spataru
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <QObject>

class QHostInfo;
class QUdpSocket;
class QHostAddress;

/**
 * \brief Implements a simple mDNS responder using Qt
 *
 * This implementation is able perform mDNS queries and mDNS responses in any
 * operating system supported by the Qt network module.
 *
 * You can obtain the IP of an mDNS device using the \c lookup() function,
 * the \c hostFound() signal will be emitted whenever this class interprets
 * a mDNS response packet and obtains valid information about a remote host.
 *
 * You can change the name that the local computer uses to identify itself
 * in the mDNS network using the \c setHostName() function.
 *
 * \todo Implement NSEC block code generation in the \c sendResponse() packet
 */
class qMDNS : public QObject {
    Q_OBJECT

  signals:
    void hostFound (const QHostInfo& info);

  public:
    static qMDNS* getInstance();

    QString hostName() const;
    QString getAddress (const QString& string);

  protected:
    explicit qMDNS();
    ~qMDNS();

  public slots:
    void setTTL (const quint32 ttl);
    void lookup (const QString& name);
    void setHostName (const QString& name);

  private slots:
    void onReadyRead();
    void readQuery (const QByteArray& data);
    void sendPacket (const QByteArray& data);
    void readResponse (const QByteArray& data);
    void sendResponse (const quint16 query_id);

  private:
    QString getHostNameFromResponse (const QByteArray& data);
    QString getIPv4FromResponse (const QByteArray& data, const QString& host);
    QStringList getIPv6FromResponse (const QByteArray& data, const QString& host);
    QList<QHostAddress> getAddressesFromResponse (const QByteArray& data,
                                                  const QString& host);

  private:
    quint32 m_ttl;
    QString m_hostName;
    QString m_serviceName;
    QUdpSocket* m_IPv4Socket;
    QUdpSocket* m_IPv6Socket;
};
