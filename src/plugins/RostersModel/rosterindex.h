#ifndef ROSTERINDEX_H
#define ROSTERINDEX_H

#include <QHash>
#include "interfaces/irostersmodel.h"

class RosterIndex : 
  public QObject,
  public IRosterIndex
{
  Q_OBJECT;
  Q_INTERFACES(IRosterIndex);

public:
  RosterIndex(int AType, const QString &AId);
  ~RosterIndex();

  QObject *instance() { return this; }

  //IRosterIndex
  virtual int type() const { return data(DR_Type).toInt(); }
  virtual QString id() const { return data(DR_Id).toString(); }
  virtual void setParentIndex(IRosterIndex *AIndex);
  virtual IRosterIndex *parentIndex() const { return FParentIndex; } 
  virtual int row() const;
  virtual void appendChild(IRosterIndex *AIndex);
  virtual bool removeChild(IRosterIndex *Aindex, bool ARecurse = false);
  virtual int childCount() const { return FChilds.count(); }
  virtual IRosterIndex *child(int ARow) const { return FChilds.value(ARow,0); }
  virtual int childRow(const IRosterIndex *AIndex) const; 
  virtual IRosterIndexDataHolder *setDataHolder(int ARole, IRosterIndexDataHolder *ADataHolder);
  virtual void setFlags(const Qt::ItemFlags &AFlags) { FFlags = AFlags; } 
  virtual Qt::ItemFlags flags() const { return FFlags; }
  virtual bool setData(int ARole, const QVariant &AData);
  virtual QVariant data(int ARole) const;
  virtual IRosterIndexList findChild(const QHash<int, QVariant> AData, bool ARecurse = false) const;
  virtual void setRemoveOnLastChildRemoved(bool ARemove) { FRemoveOnLastChildRemoved = ARemove; }
signals:
  virtual void dataChanged(IRosterIndex *);
  virtual void childAboutToBeInserted(IRosterIndex *);
  virtual void childInserted(IRosterIndex *);
  virtual void childAboutToBeRemoved(IRosterIndex *);
  virtual void childRemoved(IRosterIndex *);
protected slots:
  virtual void onChildIndexDestroyed(QObject *AIndex);
  virtual void onDataHolderChanged();
private:
  IRosterIndex *FParentIndex;
  QList<IRosterIndex *> FChilds;
  QHash<int, IRosterIndexDataHolder *> FDataHolders;
  QHash<int, QVariant> FData;
  Qt::ItemFlags FFlags;
  bool FRemoveOnLastChildRemoved;
};

#endif // ROSTERINDEX_H
