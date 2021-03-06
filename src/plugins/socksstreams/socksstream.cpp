#include "socksstream.h"

#include <QEvent>
#include <QReadLocker>
#include <QWriteLocker>
#include <QAuthenticator>
#include <QCoreApplication>
#include <QNetworkInterface>
#include <definitions/namespaces.h>
#include <definitions/internalerrors.h>
#include <definitions/statisticsparams.h>
#include <utils/stanza.h>
#include <utils/logger.h>

#define HOST_REQUEST_TIMEOUT      120000
#define PROXY_REQUEST_TIMEOUT     10000
#define ACTIVATE_REQUEST_TIMEOUT  10000

#define TCP_CLOSE_TIMEOUT         200

#define BUFFER_INCREMENT_SIZE     5120
#define MAX_BUFFER_SIZE           51200

#define SHC_HOSTS                 "/iq[@type='set']/query[@xmlns='" NS_SOCKS5_BYTESTREAMS "']"

enum NegotiationCommands {
	NCMD_START_NEGOTIATION,
	NCMD_REQUEST_PROXY_ADDRESS,
	NCMD_SEND_AVAIL_HOSTS,
	NCMD_CONNECT_TO_HOST,
	NCMD_CHECK_NEXT_HOST,
	NCMD_ACTIVATE_STREAM,
	NCMD_START_STREAM
};

// DataEvent
class DataEvent :
	public QEvent
{
public:
	DataEvent(bool ARead, bool AWrite, bool AFlush) : QEvent(FEventType) {
		FRead=ARead;
		FWrite=AWrite;
		FFlush = AFlush;
	}
	inline bool isRead() { return FRead; }
	inline bool isWrite() {return FWrite; }
	inline bool isFlush() { return FFlush; }
	static int registeredType() { return FEventType; }
private:
	bool FRead;
	bool FWrite;
	bool FFlush;
	static QEvent::Type FEventType;
};
QEvent::Type DataEvent::FEventType = static_cast<QEvent::Type>(QEvent::registerEventType());

// SocksStream
SocksStream::SocksStream(ISocksStreams *ASocksStreams, IStanzaProcessor *AStanzaProcessor, const QString &AStreamId, const Jid &AStreamJid, const Jid &AContactJid, int AKind, QObject *AParent) 
	: QIODevice(AParent), FReadBuffer(BUFFER_INCREMENT_SIZE), FWriteBuffer(BUFFER_INCREMENT_SIZE,MAX_BUFFER_SIZE)
{
	FSocksStreams = ASocksStreams;
	FStanzaProcessor = AStanzaProcessor;

	FStreamId = AStreamId;
	FStreamJid = AStreamJid;
	FContactJid = AContactJid;
	FStreamKind = AKind;
	FStreamState = IDataStreamSocket::Closed;

	FTcpSocket = NULL;
	FConnectTimeout = 10000;
	FDirectConnectDisabled = false;

	FSHIHosts= -1;

	FCloseTimer.setSingleShot(true);
	connect(&FCloseTimer,SIGNAL(timeout()),SLOT(onCloseTimerTimeout()));

	connect(FSocksStreams->instance(),SIGNAL(localConnectionAccepted(const QString &, QTcpSocket *)),SLOT(onLocalConnectionAccepted(const QString &, QTcpSocket *)));

	LOG_STRM_INFO(AStreamJid,QString("Socks stream created, with=%1, kind=%2, sid=%3").arg(AContactJid.full()).arg(FStreamKind).arg(FStreamId));
}

SocksStream::~SocksStream()
{
	abort(XmppError(IERR_SOCKS5_STREAM_DESTROYED));
	delete FTcpSocket;

	LOG_STRM_INFO(FStreamJid,QString("Socks stream destroyed, sid=%1").arg(FStreamId));
}

bool SocksStream::stanzaReadWrite(int AHandleId, const Jid &AStreamJid, Stanza &AStanza, bool &AAccept)
{
	QDomElement queryElem = AStanza.firstElement("query",NS_SOCKS5_BYTESTREAMS);
	if (AHandleId==FSHIHosts && queryElem.attribute("sid")==FStreamId)
	{
		AAccept = true;
		if (streamState()==IDataStreamSocket::Opening && queryElem.attribute("mode")!="udp")
		{
			FHosts.clear();
			FHostIndex = 0;
			FHostRequest = AStanza.id();

			if (queryElem.hasAttribute("dstaddr"))
				FConnectKey = queryElem.attribute("dstaddr");

			QDomElement hostElem = queryElem.firstChildElement("streamhost");
			while (!hostElem.isNull())
			{
				HostInfo info;
				info.jid = hostElem.attribute("jid");
				info.name = hostElem.attribute("host");
				info.port = hostElem.attribute("port").toInt();
				if (info.jid.isValid() && !info.name.isEmpty() && info.port>0)
					FHosts.append(info);
				else
					LOG_STRM_WARNING(FStreamJid,QString("Failed to append socks stream host info, sid=%1, host=%2, name=%3, port=%4: Invalid params").arg(FStreamId,info.jid.full(),info.name).arg(info.port));
				hostElem = hostElem.nextSiblingElement("streamhost");
			}

			LOG_STRM_DEBUG(FStreamJid,QString("Socks stream host list received, count=%1, sid=%2").arg(FHosts.count()).arg(FStreamId));
			negotiateConnection(NCMD_CHECK_NEXT_HOST);
		}
		else
		{
			LOG_STRM_WARNING(FStreamJid,QString("Failed to receive socks stream host list, sid=%1: UDP mode is not supported").arg(FStreamId));
			Stanza error = FStanzaProcessor->makeReplyError(AStanza,XmppStanzaError::EC_NOT_ACCEPTABLE);
			error.element().removeChild(error.firstElement("query"));
			FStanzaProcessor->sendStanzaOut(AStreamJid, error);
			abort(XmppError(IERR_SOCKS5_STREAM_INVALID_MODE));
		}
		removeStanzaHandle(FSHIHosts);
	}
	return false;
}

void SocksStream::stanzaRequestResult(const Jid &AStreamJid, const Stanza &AStanza)
{
	Q_UNUSED(AStreamJid);
	if (FProxyRequests.contains(AStanza.id()))
	{
		if (AStanza.type() == "result")
		{
			QDomElement hostElem = AStanza.firstElement("query",NS_SOCKS5_BYTESTREAMS).firstChildElement("streamhost");
			if (!hostElem.isNull())
			{
				HostInfo info;
				info.jid = hostElem.attribute("jid");
				info.name = hostElem.attribute("host");
				info.port = hostElem.attribute("port").toInt();
				if (info.jid.isValid() && !info.name.isEmpty() && info.port>0)
					FHosts.append(info);
				else
					LOG_STRM_WARNING(FStreamJid,QString("Failed to append socks stream proxy info, sid=%1, proxy=%2, name=%3, port=%4: Invalid params").arg(FStreamId,info.jid.full(),info.name).arg(info.port));
			}
			LOG_STRM_DEBUG(FStreamJid,QString("Received socks stream proxy info from=%1, sid=%2").arg(AStanza.from(),FStreamId));
		}
		else
		{
			LOG_STRM_WARNING(FStreamJid,QString("Failed to load socks stream proxy info from=%1, sid=%2: %3").arg(AStanza.from(),FStreamId,XmppStanzaError(AStanza).condition()));
		}
		FProxyRequests.removeAll(AStanza.id());
		if (FProxyRequests.isEmpty())
			negotiateConnection(NCMD_SEND_AVAIL_HOSTS);
	}
	else if (AStanza.id() == FHostRequest)
	{
		if (AStanza.type() == "result")
		{
			QDomElement hostElem = AStanza.firstElement("query",NS_SOCKS5_BYTESTREAMS).firstChildElement("streamhost-used");
			Jid hostJid = hostElem.attribute("jid");
			for (FHostIndex=0; FHostIndex<FHosts.count(); FHostIndex++)
			{
				if (FHosts.at(FHostIndex).jid == hostJid)
				{
					LOG_STRM_DEBUG(FStreamJid,QString("Received used socks stream host, host=%1, sid=%2").arg(hostJid.full(),FStreamId));
					break;
				}
			}
			negotiateConnection(NCMD_CONNECT_TO_HOST);
		}
		else
		{
			LOG_STRM_WARNING(FStreamJid,QString("Failed to receive used socks stream host, sid=%1: %2").arg(FStreamId,XmppStanzaError(AStanza).condition()));
			abort(XmppError(IERR_SOCKS5_STREAM_HOSTS_REJECTED));
		}
	}
	else if (AStanza.id() == FActivateRequest)
	{
		if (AStanza.type() == "result")
		{
			LOG_STRM_DEBUG(FStreamJid,QString("Socks stream activated, sid=%1").arg(FStreamId));
			negotiateConnection(NCMD_START_STREAM);
		}
		else
		{
			LOG_STRM_WARNING(FStreamJid,QString("Failed to activate socks stream, sid=%1: %2").arg(FStreamId,XmppStanzaError(AStanza).condition()));
			abort(XmppError(IERR_SOCKS5_STREAM_NOT_ACTIVATED));
		}
	}
}

bool SocksStream::isSequential() const
{
	return true;
}

qint64 SocksStream::bytesAvailable() const
{
	QReadLocker locker(&FThreadLock);
	return FReadBuffer.size();
}

qint64 SocksStream::bytesToWrite() const
{
	QReadLocker locker(&FThreadLock);
	return FWriteBuffer.size();
}

bool SocksStream::waitForBytesWritten(int AMsecs)
{
	bool isWritten = false;
	if (streamState() != IDataStreamSocket::Closed)
	{
		FThreadLock.lockForWrite();
		isWritten = FBytesWrittenCondition.wait(&FThreadLock, AMsecs>=0 ? (unsigned long)AMsecs : ULONG_MAX);
		FThreadLock.unlock();
	}
	return isWritten && isOpen();
}

bool SocksStream::waitForReadyRead(int AMsecs)
{
	if (streamState()!=IDataStreamSocket::Closed && bytesAvailable()==0)
	{
		FThreadLock.lockForWrite();
		FReadyReadCondition.wait(&FThreadLock, AMsecs>=0 ? (unsigned long)AMsecs : ULONG_MAX);
		FThreadLock.unlock();
	}
	return bytesAvailable()>0;
}

QString SocksStream::methodNS() const
{
	return NS_SOCKS5_BYTESTREAMS;
}

QString SocksStream::streamId() const
{
	return FStreamId;
}

Jid SocksStream::streamJid() const
{
	return FStreamJid;
}

Jid SocksStream::contactJid() const
{
	return FContactJid;
}

int SocksStream::streamKind() const
{
	return FStreamKind;
}

int SocksStream::streamState() const
{
	QReadLocker locker(&FThreadLock);
	return FStreamState;
}

XmppError SocksStream::error() const
{
	QReadLocker locker(&FThreadLock);
	return FError;
}

bool SocksStream::isOpen() const
{
	QReadLocker locker(&FThreadLock);
	return FStreamState==IDataStreamSocket::Opened;
}

bool SocksStream::open(QIODevice::OpenMode AMode)
{
	if (streamState() == IDataStreamSocket::Closed)
	{
		Logger::startTiming(STMP_SOCKSSTREAM_CONNECTED,FStreamId);
		LOG_STRM_INFO(FStreamJid,QString("Opening socks stream, sid=%1").arg(FStreamId));

		setStreamError(XmppError::null);
		if (negotiateConnection(NCMD_START_NEGOTIATION))
		{
			setOpenMode(AMode);
			setStreamState(IDataStreamSocket::Opening);
			return true;
		}
		else
		{
			LOG_STRM_WARNING(FStreamJid,QString("Failed to open socks stream, sid=%1").arg(FStreamId));
		}
	}
	return false;
}

bool SocksStream::flush()
{
	if (isOpen() && bytesToWrite()>0)
	{
		DataEvent *dataEvent = new DataEvent(true,true,true);
		QCoreApplication::postEvent(this,dataEvent);
		return true;
	}
	return false;
}

void SocksStream::close()
{
	int state = streamState();
	if (FTcpSocket && state==IDataStreamSocket::Opened)
	{
		LOG_STRM_INFO(FStreamJid,QString("Closing socks stream, sid=%1").arg(FStreamId));
		emit aboutToClose();
		writeBufferedData(true);
		setStreamState(IDataStreamSocket::Closing);
		FTcpSocket->disconnectFromHost();
	}
	else if (state != IDataStreamSocket::Closing)
	{
		setStreamState(IDataStreamSocket::Closed);
	}
}

void SocksStream::abort(const XmppError &AError)
{
	if (streamState() != IDataStreamSocket::Closed)
	{
		LOG_STRM_INFO(FStreamJid,QString("Socks stream aborted, sid=%1: %2").arg(FStreamId,AError.condition()));
		setStreamError(AError);
		close();
		setStreamState(IDataStreamSocket::Closed);
	}
}

int SocksStream::connectTimeout() const
{
	return FConnectTimeout;
}

void SocksStream::setConnectTimeout(int ATimeout)
{
	if (ATimeout>100 && ATimeout!=FConnectTimeout)
	{
		FConnectTimeout = ATimeout;
		emit propertiesChanged();
	}
}

bool SocksStream::isDirectConnectionsDisabled() const
{
	return FDirectConnectDisabled;
}

void SocksStream::setDirectConnectionsDisabled(bool ADisable)
{
	if (FDirectConnectDisabled != ADisable)
	{
		FDirectConnectDisabled = ADisable;
		emit propertiesChanged();
	}
}

QString SocksStream::forwardHost() const
{
	return FForwardHost;
}

quint16 SocksStream::forwardPort() const
{
	return FForwardPort;
}

void SocksStream::setForwardAddress(const QString &AHost, quint16 APort)
{
	if (FForwardHost!=AHost || FForwardPort!=APort)
	{
		FForwardHost = AHost;
		FForwardPort = APort;
		emit propertiesChanged();
	}
}

QNetworkProxy SocksStream::networkProxy() const
{
	return FNetworkProxy;
}

void SocksStream::setNetworkProxy(const QNetworkProxy &AProxy)
{
	if (FNetworkProxy != AProxy)
	{
		FNetworkProxy = AProxy;
		emit propertiesChanged();
	}
}

QList<QString> SocksStream::proxyList() const
{
	return FProxyList;
}

void SocksStream::setProxyList(const QList<QString> &AProxyList)
{
	if (FProxyList != AProxyList)
	{
		FProxyList = AProxyList;
		emit propertiesChanged();
	}
}

qint64 SocksStream::readData(char *AData, qint64 AMaxSize)
{
	FThreadLock.lockForWrite();
	qint64 bytes = FTcpSocket!=NULL || FReadBuffer.size()>0 ? FReadBuffer.read(AData,AMaxSize) : -1;
	if (FTcpSocket==NULL && FReadBuffer.size()==0)
		FCloseTimer.start(0);
	FThreadLock.unlock();

	if (bytes > 0)
	{
		DataEvent *dataEvent = new DataEvent(true,false,false);
		QCoreApplication::postEvent(this,dataEvent);
	}

	return bytes;
}

qint64 SocksStream::writeData(const char *AData, qint64 AMaxSize)
{
	FThreadLock.lockForWrite();
	qint64 bytes = FTcpSocket!=NULL ? FWriteBuffer.write(AData,AMaxSize) : -1;
	FThreadLock.unlock();

	if (bytes > 0)
	{
		DataEvent *dataEvent = new DataEvent(false,true,false);
		QCoreApplication::postEvent(this,dataEvent);
	}

	return bytes;
}

void SocksStream::setOpenMode(OpenMode AMode)
{
	QWriteLocker locker(&FThreadLock);
	QIODevice::setOpenMode(AMode);
}

bool SocksStream::event(QEvent *AEvent)
{
	if (AEvent->type() == DataEvent::registeredType())
	{
		DataEvent *dataEvent = static_cast<DataEvent *>(AEvent);
		if (dataEvent->isRead())
			readBufferedData(dataEvent->isFlush());
		if (dataEvent->isWrite())
			writeBufferedData(dataEvent->isFlush());
		return true;
	}
	return QIODevice::event(AEvent);
}

void SocksStream::setStreamState(int AState)
{
	if (streamState() != AState)
	{
		if (AState == IDataStreamSocket::Opened)
		{
			FThreadLock.lockForWrite();
			QIODevice::open(openMode());
			FThreadLock.unlock();

			LOG_STRM_INFO(FStreamJid,QString("Socks stream opened, sid=%1").arg(FStreamId));
			REPORT_TIMING(STMP_SOCKSSTREAM_CONNECTED,Logger::finishTiming(STMP_SOCKSSTREAM_CONNECTED,FStreamId));
		}
		else if (AState == IDataStreamSocket::Closed)
		{
			removeStanzaHandle(FSHIHosts);
			FSocksStreams->removeLocalConnection(FConnectKey);
			emit readChannelFinished();

			FThreadLock.lockForWrite();
			QString saveError = QIODevice::errorString();
			QIODevice::close();
			QIODevice::setErrorString(saveError);
			FReadBuffer.clear();
			FWriteBuffer.clear();
			FThreadLock.unlock();

			FReadyReadCondition.wakeAll();
			FBytesWrittenCondition.wakeAll();

			LOG_STRM_INFO(FStreamJid,QString("Socks stream closed, sid=%1").arg(FStreamId));
		}

		FThreadLock.lockForWrite();
		FStreamState = AState;
		FThreadLock.unlock();

		emit stateChanged(AState);
	}
}

void SocksStream::setStreamError(const XmppError &AError)
{
	if (AError.isNull() != FError.isNull())
	{
		QWriteLocker locker(&FThreadLock);
		FError = AError;
		setErrorString(AError.errorString());
	}
}

void SocksStream::setTcpSocket(QTcpSocket *ASocket)
{
	if (ASocket)
	{
		LOG_STRM_DEBUG(FStreamJid,QString("Socks stream data socket selected, address=%1, sid=%2").arg(ASocket->peerAddress().toString(),FStreamId));
		connect(ASocket,SIGNAL(readyRead()), SLOT(onTcpSocketReadyRead()));
		connect(ASocket,SIGNAL(bytesWritten(qint64)),SLOT(onTcpSocketBytesWritten(qint64)));
		connect(ASocket,SIGNAL(error(QAbstractSocket::SocketError)),SLOT(onTcpSocketError(QAbstractSocket::SocketError)));
		connect(ASocket,SIGNAL(disconnected()),SLOT(onTcpSocketDisconnected()));
		QWriteLocker locker(&FThreadLock);
		FTcpSocket = ASocket;
	}
}

void SocksStream::writeBufferedData(bool AFlush)
{
	if (FTcpSocket && isOpen())
	{
		FThreadLock.lockForRead();
		qint64 dataSize = AFlush ? FWriteBuffer.size() : qMin((qint64)FWriteBuffer.size(), qint64(MAX_BUFFER_SIZE)-FTcpSocket->bytesToWrite());
		FThreadLock.unlock();

		if (dataSize > 0)
		{
			FThreadLock.lockForWrite();
			QByteArray data = FWriteBuffer.read(dataSize);
			FThreadLock.unlock();
			FBytesWrittenCondition.wakeAll();

			if (FTcpSocket->write(data) != data.size())
				abort(XmppError(IERR_SOCKS5_STREAM_DATA_NOT_SENT));
			else if (AFlush)
				FTcpSocket->flush();

			emit bytesWritten(data.size());
		}
	}
}

void SocksStream::readBufferedData(bool AFlush)
{
	if (FTcpSocket && isOpen())
	{
		FThreadLock.lockForRead();
		qint64 dataSize = AFlush ? FTcpSocket->bytesAvailable() : qMin(FTcpSocket->bytesAvailable(), qint64(MAX_BUFFER_SIZE)-FReadBuffer.size());
		FThreadLock.unlock();

		if (dataSize > 0)
		{
			FThreadLock.lockForWrite();
			FReadBuffer.write(FTcpSocket->read(dataSize));
			FThreadLock.unlock();

			FReadyReadCondition.wakeAll();
			emit readyRead();
		}
	}
}

int SocksStream::insertStanzaHandle(const QString &ACondition)
{
	if (FStanzaProcessor)
	{
		IStanzaHandle shandle;
		shandle.handler = this;
		shandle.order = SHO_DEFAULT;
		shandle.direction = IStanzaHandle::DirectionIn;
		shandle.streamJid = FStreamJid;
		shandle.conditions.append(ACondition);
		return FStanzaProcessor->insertStanzaHandle(shandle);
	}
	return -1;
}

void SocksStream::removeStanzaHandle(int &AHandleId)
{
	if (FStanzaProcessor && AHandleId>0)
	{
		FStanzaProcessor->removeStanzaHandle(AHandleId);
		AHandleId = -1;
	}
}

bool SocksStream::negotiateConnection(int ACommand)
{
	if (ACommand == NCMD_START_NEGOTIATION)
	{
		FHosts.clear();
		FHostIndex = INT_MAX;
		if (streamKind() == IDataStreamSocket::Initiator)
		{
			FConnectKey = FSocksStreams->connectionKey(FStreamId, FStreamJid, FContactJid);
			if (requestProxyAddress() || sendAvailHosts())
				return true;
		}
		else
		{
			FSHIHosts = insertStanzaHandle(SHC_HOSTS);
			if (FSHIHosts >= 0)
			{
				FConnectKey = FSocksStreams->connectionKey(FStreamId, FContactJid, FStreamJid);
				return true;
			}
		}
	}
	else if (streamState() == IDataStreamSocket::Opening)
	{
		if (ACommand == NCMD_SEND_AVAIL_HOSTS)
		{
			if (sendAvailHosts())
				return true;
			abort(XmppError(IERR_SOCKS5_STREAM_HOSTS_NOT_CREATED));
		}
		else if (ACommand == NCMD_CONNECT_TO_HOST)
		{
			if (FHostIndex < FHosts.count())
			{
				HostInfo info = FHosts.value(FHostIndex);
				if (info.jid == FStreamJid)
				{
					if (FTcpSocket != NULL)
					{
						setStreamState(IDataStreamSocket::Opened);
						return true;
					}
					abort(XmppError(IERR_SOCKS5_STREAM_NO_DIRECT_CONNECTION));
				}
				else
				{
					if (connectToHost())
						return true;

					abort(XmppError(IERR_SOCKS5_STREAM_INVALID_HOST_ADDRESS));
					FSocksStreams->removeLocalConnection(FConnectKey);
				}
			}
			abort(XmppError(IERR_SOCKS5_STREAM_INVALID_HOST));
		}
		else if (ACommand == NCMD_CHECK_NEXT_HOST)
		{
			if (connectToHost())
				return true;

			sendFailedHosts();
			abort(XmppError(IERR_SOCKS5_STREAM_HOSTS_UNREACHABLE));
		}
		else if (ACommand == NCMD_ACTIVATE_STREAM)
		{
			if (streamKind() == IDataStreamSocket::Initiator)
			{
				if (activateStream())
					return true;
				abort(XmppError(IERR_SOCKS5_STREAM_NOT_ACTIVATED));
			}
			else
			{
				if (sendUsedHost())
				{
					setStreamState(IDataStreamSocket::Opened);
					return true;
				}
				abort(XmppError(IERR_SOCKS5_STREAM_NOT_ACTIVATED));
			}
		}
		else if (ACommand == NCMD_START_STREAM)
		{
			setStreamState(IDataStreamSocket::Opened);
			return true;
		}
	}
	return false;
}

bool SocksStream::requestProxyAddress()
{
	bool requested = false;
	foreach(const Jid &proxy, FProxyList)
	{
		Stanza stanza("iq");
		stanza.setType("get").setTo(proxy.full()).setId(FStanzaProcessor->newId());
		stanza.addElement("query",NS_SOCKS5_BYTESTREAMS);
		if (FStanzaProcessor->sendStanzaRequest(this,FStreamJid,stanza,PROXY_REQUEST_TIMEOUT))
		{
			requested = true;
			FProxyRequests.append(stanza.id());
			LOG_STRM_DEBUG(FStreamJid,QString("Proxy info request sent to=%1, sid=%2").arg(stanza.to(),FStreamId));
		}
		else
		{
			LOG_STRM_WARNING(FStreamJid,QString("Failed to send proxy info request to=%1, sid=%2").arg(stanza.to(),FStreamId));
		}
	}
	return requested;
}

bool SocksStream::sendAvailHosts()
{
	Stanza stanza("iq");
	stanza.setType("set").setTo(FContactJid.full()).setId(FStanzaProcessor->newId());

	QDomElement queryElem = stanza.addElement("query",NS_SOCKS5_BYTESTREAMS);
	queryElem.setAttribute("sid",FStreamId);
	queryElem.setAttribute("mode","tcp");
	queryElem.setAttribute("dstaddr",FConnectKey);

	if (!isDirectConnectionsDisabled() && FSocksStreams->appendLocalConnection(FConnectKey))
	{
		if (!FForwardHost.isEmpty() && FForwardPort>0)
		{
			HostInfo info;
			info.jid = FStreamJid;
			info.name = FForwardHost;
			info.port = FForwardPort;
			FHosts.prepend(info);
		}
		else foreach(const QHostAddress &address, QNetworkInterface::allAddresses())
		{
			if (address.protocol()!=QAbstractSocket::IPv6Protocol && address!=QHostAddress::LocalHost)
			{
				HostInfo info;
				info.jid = FStreamJid;
				info.name = address.toString();
				info.port = FSocksStreams->listeningPort();
				FHosts.prepend(info);
			}
		}
	}

	foreach(const HostInfo &info, FHosts)
	{
		QDomElement hostElem = queryElem.appendChild(stanza.createElement("streamhost")).toElement();
		hostElem.setAttribute("jid",info.jid.full());
		hostElem.setAttribute("host",info.name);
		hostElem.setAttribute("port",info.port);
	}

	if (FStanzaProcessor->sendStanzaRequest(this,FStreamJid,stanza,HOST_REQUEST_TIMEOUT))
	{
		FHostRequest = stanza.id();
		LOG_STRM_DEBUG(FStreamJid,QString("Socks stream avail hosts sent, count=%1, sid=%2").arg(FHosts.count()).arg(FStreamId));
		return !FHosts.isEmpty();
	}
	else
	{
		LOG_STRM_WARNING(FStreamJid,QString("Failed to send socks stream avail hosts, sid=%1").arg(FStreamId));
	}

	return false;
}

bool SocksStream::connectToHost()
{
	if (FHostIndex < FHosts.count())
	{
		if (!FTcpSocket)
		{
			FTcpSocket = new QTcpSocket(this);
			connect(FTcpSocket, SIGNAL(proxyAuthenticationRequired(const QNetworkProxy &, QAuthenticator *)),
				SLOT(onHostSocketProxyAuthenticationRequired(const QNetworkProxy &, QAuthenticator *)));
			connect(FTcpSocket,SIGNAL(connected()),SLOT(onHostSocketConnected()));
			connect(FTcpSocket,SIGNAL(readyRead()), SLOT(onHostSocketReadyRead()));
			connect(FTcpSocket,SIGNAL(error(QAbstractSocket::SocketError)), SLOT(onHostSocketError(QAbstractSocket::SocketError)));
			connect(FTcpSocket,SIGNAL(disconnected()),SLOT(onHostSocketDisconnected()));
			FTcpSocket->setProxy(FNetworkProxy);
		}

		HostInfo info = FHosts.value(FHostIndex);
		LOG_STRM_DEBUG(FStreamJid,QString("Connecting to socks stream host, name=%1, port=%2, sid=%3").arg(info.name).arg(info.port).arg(FStreamId));

		FCloseTimer.start(connectTimeout());
		FTcpSocket->connectToHost(info.name, info.port);
		return true;
	}
	return false;
}

bool SocksStream::sendUsedHost()
{
	if (FHostIndex < FHosts.count())
	{
		Stanza stanza("iq");
		stanza.setType("result").setId(FHostRequest).setTo(FContactJid.full());

		const HostInfo &info = FHosts.at(FHostIndex);
		QDomElement query =  stanza.addElement("query",NS_SOCKS5_BYTESTREAMS);
		query.setAttribute("sid",FStreamId);

		QDomElement hostElem = query.appendChild(stanza.addElement("streamhost-used")).toElement();
		hostElem.setAttribute("jid",info.jid.full());

		if (FStanzaProcessor->sendStanzaOut(FStreamJid, stanza))
		{
			LOG_STRM_DEBUG(FStreamJid,QString("Socks stream used host sent, jid=%1, sid=%2").arg(info.jid.full(),FStreamId));
			return true;
		}
		else
		{
			LOG_STRM_WARNING(FStreamJid,QString("Failed to send socks stream used host, sid=%1").arg(FStreamId));
		}
	}
	return false;
}

bool SocksStream::sendFailedHosts()
{
	Stanza stanza("iq");
	stanza.setType("error").setTo(FContactJid.full()).setId(FHostRequest);

	QDomElement errElem = stanza.addElement("error");
	errElem.setAttribute("code", 404);
	errElem.setAttribute("type","cancel");
	errElem.appendChild(stanza.createElement("item-not-found", NS_XMPP_STANZA_ERROR));

	if (FStanzaProcessor->sendStanzaOut(FStreamJid, stanza))
	{
		LOG_STRM_DEBUG(FStreamJid,QString("Socks stream hosts not found notify sent, sid=%1").arg(FStreamId));
		return true;
	}
	else
	{
		LOG_STRM_WARNING(FStreamJid,QString("Failed to send socks stream hosts not found notify, sid=%1").arg(FStreamId));
	}

	return false;
}

bool SocksStream::activateStream()
{
	if (FHostIndex < FHosts.count())
	{
		Stanza stanza("iq");
		stanza.setType("set").setTo(FHosts.at(FHostIndex).jid.full()).setId(FStanzaProcessor->newId());
		QDomElement queryElem = stanza.addElement("query",NS_SOCKS5_BYTESTREAMS);
		queryElem.setAttribute("sid",FStreamId);
		queryElem.appendChild(stanza.createElement("activate")).appendChild(stanza.createTextNode(FContactJid.full()));
		if (FStanzaProcessor->sendStanzaRequest(this,FStreamJid,stanza,ACTIVATE_REQUEST_TIMEOUT))
		{
			FActivateRequest = stanza.id();
			LOG_STRM_DEBUG(FStreamJid,QString("Socks stream activate request sent, sid=%1").arg(FStreamId));
			return true;
		}
		else
		{
			LOG_STRM_WARNING(FStreamJid,QString("Failed to send socks stream activate request, sid=%1").arg(FStreamId));
		}
	}
	return false;
}

void SocksStream::onHostSocketProxyAuthenticationRequired(const QNetworkProxy &AProxy, QAuthenticator *AAuth)
{
	AAuth->setUser(AProxy.user());
	AAuth->setPassword(AProxy.password());
}

void SocksStream::onHostSocketConnected()
{
	FCloseTimer.stop();

	QByteArray outData;
	outData += (char)5;   // Socks version
	outData += (char)1;   // Number of possible authentication methods
	outData += (char)0;   // No-auth
	FTcpSocket->write(outData);
	LOG_STRM_DEBUG(FStreamJid,QString("Socks stream connected to host, address=%1, sid=%2").arg(FTcpSocket->peerAddress().toString(),FStreamId));
}

void SocksStream::onHostSocketReadyRead()
{
	QByteArray inData = FTcpSocket->read(FTcpSocket->bytesAvailable());
	if (inData.size() < 10)
	{
		QByteArray outData;
		outData += (char)5;                     // socks version
		outData += (char)1;                     // connect method
		outData += (char)0;                     // reserved
		outData += (char)3;                     // address type (domain)
		outData += (char)FConnectKey.length();  // domain length
		outData += FConnectKey.toLatin1();      // domain
		outData += (char)0;                     // port
		outData += (char)0;                     // port
		FTcpSocket->write(outData);
		LOG_STRM_DEBUG(FStreamJid,QString("Socks stream authentication key sent to host, sid=%1").arg(FStreamId));
	}
	else if (inData.at(0)==5 && inData.at(1)==0)
	{
		LOG_STRM_DEBUG(FStreamJid,QString("Socks stream authentication key accepted by host, sid=%1").arg(FStreamId));
		FTcpSocket->disconnect(this);
		setTcpSocket(FTcpSocket);
		negotiateConnection(NCMD_ACTIVATE_STREAM);
	}
	else
	{
		LOG_STRM_WARNING(FStreamJid,QString("Socks stream authentication key rejected by host, sid=%1").arg(FStreamId));
		FTcpSocket->disconnectFromHost();
	}
}

void SocksStream::onHostSocketError(QAbstractSocket::SocketError AError)
{
	Q_UNUSED(AError);
	if (FTcpSocket->state() != QAbstractSocket::ConnectedState)
	{
		LOG_STRM_DEBUG(FStreamJid,QString("Failed to connect to socks stream host, address=%1, sid=%2: %3").arg(FTcpSocket->peerAddress().toString(),FStreamId,FTcpSocket->errorString()));
		onHostSocketDisconnected();
	}
	else
	{
		LOG_STRM_DEBUG(FStreamJid,QString("Socks stream host droped connection, address=%1, sid=%2: %3").arg(FTcpSocket->peerAddress().toString(),FStreamId,FTcpSocket->errorString()));
	}
}

void SocksStream::onHostSocketDisconnected()
{
	FCloseTimer.stop();
	LOG_STRM_DEBUG(FStreamJid,QString("Socks stream disconnected from host, address=%1, sid=%2").arg(FTcpSocket->peerAddress().toString(),FStreamId));

	FHostIndex++;
	if (streamKind() == IDataStreamSocket::Initiator)
		abort(XmppError(IERR_SOCKS5_STREAM_HOST_NOT_CONNECTED));
	else
		negotiateConnection(NCMD_CHECK_NEXT_HOST);
}

void SocksStream::onTcpSocketReadyRead()
{
	readBufferedData(false);
}

void SocksStream::onTcpSocketBytesWritten(qint64 ABytes)
{
	Q_UNUSED(ABytes);
	writeBufferedData(false);
}

void SocksStream::onTcpSocketError(QAbstractSocket::SocketError AError)
{
	if (AError != QAbstractSocket::RemoteHostClosedError)
	{
		LOG_STRM_WARNING(FStreamJid,QString("Socks stream connection aborted, sid=%1: %2").arg(FStreamId,FTcpSocket->errorString()));
		setStreamError(XmppError(IERR_SOCKS5_STREAM_HOST_DISCONNECTED,FTcpSocket->errorString()));
	}
}

void SocksStream::onTcpSocketDisconnected()
{
	readBufferedData(true);
	LOG_STRM_DEBUG(FStreamJid,QString("Socks stream connection disconnected, sid=%1").arg(FStreamId));

	QWriteLocker locker(&FThreadLock);
	FCloseTimer.start(FReadBuffer.size()>0 ? TCP_CLOSE_TIMEOUT : 0);
	FTcpSocket->deleteLater();
	FTcpSocket = NULL;
}

void SocksStream::onLocalConnectionAccepted(const QString &AKey, QTcpSocket *ATcpSocket)
{
	if (FConnectKey == AKey)
		setTcpSocket(ATcpSocket);
}

void SocksStream::onCloseTimerTimeout()
{
	if (FTcpSocket)
	{
		FTcpSocket->abort();
		onHostSocketDisconnected();
	}
	else
	{
		setStreamState(IDataStreamSocket::Closed);
	}
}
