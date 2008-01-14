#ifndef VCARDPLUGIN_H
#define VCARDPLUGIN_H

#include "../../definations/initorders.h"
#include "../../definations/namespaces.h"
#include "../../definations/actiongroups.h"
#include "../../definations/rosterindextyperole.h"
#include "../../interfaces/ipluginmanager.h"
#include "../../interfaces/ivcard.h"
#include "../../interfaces/ixmppstreams.h"
#include "../../interfaces/irostersview.h"
#include "../../interfaces/istanzaprocessor.h"
#include "../../interfaces/isettings.h"
#include "../../utils/stanza.h"
#include "../../utils/action.h"
#include "vcard.h"
#include "vcarddialog.h"

class VCardPlugin : 
  public QObject,
  public IPlugin,
  public IVCardPlugin,
  public IIqStanzaOwner
{
  Q_OBJECT;
  Q_INTERFACES(IPlugin IVCardPlugin IIqStanzaOwner);
  friend class VCard;
public:
  VCardPlugin();
  ~VCardPlugin();
  //IPlugin
  virtual QObject *instance() { return this; }
  virtual QUuid pluginUuid() const { return VCARD_UUID; }
  virtual void pluginInfo(PluginInfo *APluginInfo);
  virtual bool initConnections(IPluginManager *APluginManager, int &AInitOrder);
  virtual bool initObjects();
  virtual bool initSettings() { return true; }
  virtual bool startPlugin();
  //IIqStanzaOwner
  virtual void iqStanza(const Jid &AStreamJid, const Stanza &AStanza);
  virtual void iqStanzaTimeOut(const QString &AId);
  //IVCardPlugin
  virtual QString vcardFileName(const Jid &AContactJid) const;
  virtual bool hasVCard(const Jid &AContactJid) const;
  virtual IVCard *vcard(const Jid &AContactJid);
  virtual bool requestVCard(const Jid &AStreamJid,const Jid &AContactJid);
  virtual bool publishVCard(IVCard *AVCard, const Jid &AStreamJid);
  virtual void showVCardDialog(const Jid &AStreamJid, const Jid &AContactJid);
signals:
  virtual void vcardReceived(const Jid &AContactJid);
  virtual void vcardPublished(const Jid &AContactJid);
  virtual void vcardError(const Jid &AContactJid, const QString &AError);
protected:
  void unlockVCard(const Jid &AContactJid);
  void saveVCardFile(const QDomElement &AElem, const Jid &AContactJid) const;
protected slots:
  void onRostersViewContextMenu(IRosterIndex *AIndex, Menu *AMenu);
  void onShowVCardDialogByAction(bool);
  void onVCardDialogDestroyed(QObject *ADialog);
  void onXmppStreamRemoved(IXmppStream *AXmppStream);
private:
  IXmppStreams *FXmppStreams;
  IRostersView *FRostersView;
  IRostersViewPlugin *FRostersViewPlugin;
  IStanzaProcessor *FStanzaProcessor;
  ISettingsPlugin *FSettingsPlugin;
private:
  struct VCardItem 
  {
    VCardItem() { vcard = NULL; locks = 0; }
    VCard *vcard;
    int locks;
  };
  QHash<QString,VCardItem> FVCards;
  QHash<QString,QString> FVCardRequestId;
  QHash<QString,QString> FVCardPublishId;
  QHash<Jid,VCardDialog *> FVCardDialogs;
};

#endif // VCARDPLUGIN_H