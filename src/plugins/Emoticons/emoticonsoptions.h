#ifndef EMOTICONSOPTIONS_H
#define EMOTICONSOPTIONS_H

#include <QWidget>
#include "ui_emoticonsoptions.h"
#include "../../interfaces/iemoticons.h"
#include "../../utils/skin.h"
#include "../../utils/iconsetdelegate.h"

class EmoticonsOptions : 
  public QWidget
{
  Q_OBJECT;
public:
  EmoticonsOptions(IEmoticons *AEmoticons, QWidget *AParent = NULL);
  ~EmoticonsOptions();
  void apply() const;
protected:
  void init();
protected slots:
  void onUpButtonClicked();
  void onDownButtonClicked();
  void onIconsetsChangedBySkin();
private:
  Ui::EmoticonsOptionsClass ui;
private:
  IEmoticons *FEmoticons;
};

#endif // EMOTICONSOPTIONS_H