#include "starttls.h"

#include "../../utils/stanza.h"

StartTLS::StartTLS(IXmppStream *AXmppStream) 
  : QObject(AXmppStream->instance())
{
  FConnection = NULL;
  FNeedHook = false;
  FXmppStream = AXmppStream;
  connect(FXmppStream->instance(),SIGNAL(closed(IXmppStream *)), SLOT(onStreamClosed(IXmppStream *)));
}

StartTLS::~StartTLS()
{

}

bool StartTLS::start(const QDomElement &/*AElem*/)
{
  FConnection = qobject_cast<IDefaultConnection *>(FXmppStream->connection()->instance());
  if (FConnection)
  {
    Stanza request("starttls");
    request.setAttribute("xmlns",NS_FEATURE_STARTTLS);
    FNeedHook = true;
    FXmppStream->sendStanza(request);
    return true;
  }
  return false;
}

bool StartTLS::needHook(Direction ADirection) const
{
  if (ADirection == DirectionIn)
    return FNeedHook;
  return false;
}

bool StartTLS::hookElement(QDomElement *AElem, Direction ADirection)
{
  if (ADirection != DirectionIn || AElem->namespaceURI() != NS_FEATURE_STARTTLS)
    return false;

  FNeedHook = false;
  if (AElem->tagName() == "proceed")
  {
    FConnection->startClientEncryption();
    emit finished(true);
  }
  else if (AElem->tagName() == "failure")
    emit error(tr("StartTLS negotiation failed.")); 
  else
    emit error(tr("Wrong StartTLS negotiation answer.")); 
  return true;
}

void StartTLS::onStreamClosed(IXmppStream * /*AXmppStream*/)
{
  FConnection = NULL;
  FNeedHook = false;
}