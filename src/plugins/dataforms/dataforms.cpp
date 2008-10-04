#include "dataforms.h"

DataForms::DataForms()
{
  FDiscovery = NULL;
}

DataForms::~DataForms()
{

}

void DataForms::pluginInfo(IPluginInfo *APluginInfo)
{
  APluginInfo->author = tr("Potapov S.A. aka Lion");
  APluginInfo->description = tr("Implements data forms and generic data description");
  APluginInfo->homePage = "http://jrudevels.org";
  APluginInfo->name = "Data Forms";
  APluginInfo->uid = DATAFORMS_UUID;
  APluginInfo->version = "0.1";
}

bool DataForms::initConnections(IPluginManager *APluginManager, int &/*AInitOrder*/)
{
  IPlugin *plugin = APluginManager->getPlugins("IServiceDiscovery").value(0,NULL);
  if (plugin)
  {
    FDiscovery = qobject_cast<IServiceDiscovery *>(plugin->instance());
  }
  return true;
}

bool DataForms::initObjects()
{
  if (FDiscovery)
  {
    registerDiscoFeatures();
  }
  return true;
}

IDataValidate DataForms::dataValidate(const QDomElement &AValidateElem) const
{
  IDataValidate validate;
  if (!AValidateElem.isNull())
  {
    validate.type = AValidateElem.attribute("datatype",DATAVALIDATE_TYPE_STRING);
    if (!AValidateElem.firstChildElement(DATAVALIDATE_METHOD_LIST_RANGE).isNull())
    {
      QDomElement methodElem = AValidateElem.firstChildElement(DATAVALIDATE_METHOD_LIST_RANGE);
      validate.listMin = methodElem.attribute("min");
      validate.listMax = methodElem.attribute("max");
    }
    if (!AValidateElem.firstChildElement(DATAVALIDATE_METHOD_RANGE).isNull())
    {
      QDomElement methodElem = AValidateElem.firstChildElement(DATAVALIDATE_METHOD_RANGE);
      validate.method = DATAVALIDATE_METHOD_RANGE;
      validate.min = methodElem.attribute("min");
      validate.max = methodElem.attribute("max");
    }
    else if (!AValidateElem.firstChildElement(DATAVALIDATE_METHOD_REGEXP).isNull())
    {
      QDomElement methodElem = AValidateElem.firstChildElement(DATAVALIDATE_METHOD_REGEXP);
      validate.method = DATAVALIDATE_METHOD_REGEXP;
      validate.regexp.setPattern(methodElem.text());
    }
    else if (!AValidateElem.firstChildElement(DATAVALIDATE_METHOD_OPEN).isNull())
    {
      validate.method = DATAVALIDATE_METHOD_OPEN;
    }
    else
    {
      validate.method = DATAVALIDATE_METHOD_BASIC;
    }
  }
  return validate;
}

IDataField DataForms::dataField(const QDomElement &AFieldElem) const
{
  IDataField field;
  if (!AFieldElem.isNull())
  {
    field.required = !AFieldElem.firstChildElement("required").isNull();
    field.var = AFieldElem.attribute("var");
    field.type = AFieldElem.attribute("type",DATAFIELD_TYPE_TEXTSINGLE);
    field.label = AFieldElem.attribute("label");
    field.desc = AFieldElem.firstChildElement("desc").text();

    QStringList valueList;
    QDomElement valueElem = AFieldElem.firstChildElement("value");
    while (!valueElem.isNull())
    {
      valueList.append(valueElem.text());
      valueElem = valueElem.nextSiblingElement("value");
    }

    if (field.type==DATAFIELD_TYPE_JIDMULTI || field.type==DATAFIELD_TYPE_LISTMULTI || field.type==DATAFIELD_TYPE_TEXTMULTI)
      field.value = valueList;
    else
      field.value = valueList.value(0);

    QDomElement optionElem = AFieldElem.firstChildElement("option");
    while (!optionElem.isNull())
    {
      IDataOption option;
      option.label = optionElem.attribute("label");
      option.value = optionElem.firstChildElement("value").text();
      field.options.append(option);
      optionElem = optionElem.nextSiblingElement("option");
    }

    QDomElement validateElem = AFieldElem.firstChildElement("validate");
    if (!validateElem.isNull() && validateElem.namespaceURI()==NS_JABBER_XDATAVALIDATE)
    {
      field.validate = dataValidate(validateElem);
    }

    field.required = !AFieldElem.firstChildElement("required").isNull();
  }
  return field;
}

IDataTable DataForms::dataTable(const QDomElement &ATableElem) const
{
  IDataTable table;
  if (!ATableElem.isNull())
  {
    QStringList columnVars;
    QDomElement columnElem = ATableElem.firstChildElement("field");
    while (!columnElem.isNull())
    {
      if (!columnElem.attribute("var").isEmpty())
      {
        IDataField field = dataField(columnElem);
        table.columns.append(field);
        columnVars.append(field.var);
      }
      columnElem = columnElem.nextSiblingElement("field");
    }

    int row = 0;
    QDomElement itemElem = ATableElem.parentNode().toElement().firstChildElement("item");
    while (!itemElem.isNull())
    {
      QStringList rowValues;
      for (int i=0;i<columnVars.count();rowValues.append(""),i++);
      QDomElement fieldElem = itemElem.firstChildElement("field");
      while (!fieldElem.isNull())
      {
        QString var = fieldElem.attribute("var");
        int column = columnVars.indexOf(var);
        if (column>=0)
          rowValues[column] = fieldElem.firstChildElement("value").text();
        fieldElem = fieldElem.nextSiblingElement("field");
      }
      table.rows.insert(row++,rowValues);
      itemElem = itemElem.nextSiblingElement("item");
    }
  }
  return table;
}

IDataLayout DataForms::dataLayout(const QDomElement &ALayoutElem) const
{
  IDataLayout section;
  if (!ALayoutElem.isNull())
  {
    section.label = ALayoutElem.attribute("label");

    QDomElement childElem = ALayoutElem.firstChildElement();
    while (!childElem.isNull())
    {
      QString childName = childElem.tagName();
      if (childName == "text")
      {
        section.text.append(childElem.text());
      }
      else if (childName == "section")
      {
        section.sections.append(dataLayout(childElem));
      }
      else if (childName == "fieldref")
      {
        section.fieldrefs.append(childElem.attribute("var"));
      }
      section.childOrder.append(childName);
      childElem = childElem.nextSiblingElement();
    }
  }
  return section;
}

IDataForm DataForms::dataForm(const QDomElement &AFormElem) const
{
  IDataForm form;
  if (!AFormElem.isNull())
  {
    form.type = AFormElem.attribute("type",DATAFORM_TYPE_FORM);
    form.title = AFormElem.firstChildElement("title").text();

    QDomElement instrElem = AFormElem.firstChildElement("instructions");
    while (!instrElem.isNull())
    {
      form.instructions.append(instrElem.text());
      instrElem = instrElem.nextSiblingElement("instructions");
    }

    QDomElement fieldElem = AFormElem.firstChildElement("field");
    while(!fieldElem.isNull())
    {
      form.fields.append(dataField(fieldElem));
      fieldElem = fieldElem.nextSiblingElement("field");
    }

    QDomElement tableElem = AFormElem.firstChildElement("reported");
    if (!tableElem.isNull())
    {
      form.tabel = dataTable(tableElem);
    }

    QDomElement pageElem = AFormElem.firstChildElement("page");
    while (!pageElem.isNull())
    {
      if (pageElem.namespaceURI() == NS_JABBER_XDATALAYOUT)
      {
        IDataLayout page = dataLayout(pageElem);
        form.pages.append(page);
      }
      pageElem = pageElem.nextSiblingElement("page");
    }
  }
  return form;
}

void DataForms::xmlValidate(const IDataValidate &AValidate, QDomElement &AFieldElem) const
{
  QDomDocument doc = AFieldElem.ownerDocument();
  QDomElement validateElem = AFieldElem.appendChild(doc.createElementNS(NS_JABBER_XDATAVALIDATE,"validate")).toElement();
  validateElem.setAttribute("datatype",AValidate.type);

  QString method = !AValidate.method.isEmpty() ? AValidate.method : DATAVALIDATE_METHOD_BASIC;
  QDomElement methodElem = validateElem.appendChild(doc.createElement(method)).toElement();
  if (method == DATAVALIDATE_METHOD_RANGE)
  {
    if (!AValidate.min.isEmpty())
      methodElem.setAttribute("min",AValidate.min);
    if (!AValidate.max.isEmpty())
      methodElem.setAttribute("max",AValidate.max);
  }
  else if (method == DATAVALIDATE_METHOD_REGEXP)
  {
    methodElem.appendChild(doc.createTextNode(AValidate.regexp.pattern()));
  }

  if (!AValidate.listMin.isEmpty() || !AValidate.listMax.isEmpty())
  {
    QDomElement listElem = validateElem.appendChild(doc.createElement(DATAVALIDATE_METHOD_LIST_RANGE)).toElement();
    if (!AValidate.listMin.isEmpty())
      listElem.setAttribute("min",AValidate.listMin);
    if (!AValidate.listMax.isEmpty())
      listElem.setAttribute("max",AValidate.listMax);
  }
}

void DataForms::xmlField(const IDataField &AField, QDomElement &AFormElem, FieldWriteMode AMode) const
{
  QDomDocument doc = AFormElem.ownerDocument();
  QDomElement fieldElem = AFormElem.appendChild(doc.createElement("field")).toElement();
 
  if (!AField.var.isEmpty())
    fieldElem.setAttribute("var",AField.var);

  if (!AField.type.isEmpty())
    fieldElem.setAttribute("type",AField.type);

  if (AField.value.type()==QVariant::StringList && !AField.value.toStringList().isEmpty())
  {
    foreach(QString value, AField.value.toStringList()) {
      fieldElem.appendChild(doc.createElement("value")).appendChild(doc.createTextNode(value)); }
  }
  else if (AField.value.type() == QVariant::Bool)
  {
    fieldElem.appendChild(doc.createElement("value")).appendChild(doc.createTextNode(AField.value.toBool() ? "1" : "0"));
  }
  else if (!AField.value.toString().isEmpty())
  {
    fieldElem.appendChild(doc.createElement("value")).appendChild(doc.createTextNode(AField.value.toString()));
  }

  if (AMode != IDataForms::FWM_SUBMIT)
  {
    if (!AField.label.isEmpty())
      fieldElem.setAttribute("label",AField.label);
  }

  if (AMode == IDataForms::FWM_FORM)
  {
    if (!AField.validate.type.isEmpty())
      xmlValidate(AField.validate,fieldElem);

    if (!AField.desc.isEmpty())
      fieldElem.appendChild(doc.createElement("desc")).appendChild(doc.createTextNode(AField.desc));

    foreach(IDataOption option, AField.options)
    {
      QDomElement optionElem = fieldElem.appendChild(doc.createElement("option")).toElement();
      if (!option.label.isEmpty())
        optionElem.setAttribute("label",option.label);
      optionElem.appendChild(doc.createElement("value")).appendChild(doc.createTextNode(option.value));
    }
    
    if (AField.required)
      fieldElem.appendChild(doc.createElement("required"));
  }
}

void DataForms::xmlTable(const IDataTable &ATable, QDomElement &AFormElem) const
{
  QDomDocument doc = AFormElem.ownerDocument();
  QDomElement reportElem = AFormElem.appendChild(doc.createElement("reported")).toElement();
  
  foreach(IDataField column, ATable.columns) {
    xmlField(column,reportElem,IDataForms::FWM_TABLE); }

  foreach(QStringList rowValues,ATable.rows)
  {
    QDomElement itemElem = AFormElem.appendChild(doc.createElement("item")).toElement();
    for (int col=0;col<rowValues.count();col++)
    {
      QDomElement fieldElem = itemElem.appendChild(doc.createElement("field")).toElement();
      fieldElem.setAttribute("var",ATable.columns.value(col).var);
      fieldElem.appendChild(doc.createElement("value")).appendChild(doc.createTextNode(rowValues.at(col)));
    }
  }
}

void DataForms::xmlSection(const IDataLayout &ALayout, QDomElement &AParentElem) const
{
  QDomDocument doc = AParentElem.ownerDocument();
  QDomElement sectionElem = AParentElem.appendChild(doc.createElement("section")).toElement();
  xmlLayout(ALayout,sectionElem);
}

void DataForms::xmlPage(const IDataLayout &ALayout, QDomElement &AParentElem) const
{
  QDomDocument doc = AParentElem.ownerDocument();
  QDomElement pageElem = AParentElem.appendChild(doc.createElementNS(NS_JABBER_XDATALAYOUT,"page")).toElement();
  xmlLayout(ALayout,pageElem);
}

void DataForms::xmlForm(const IDataForm &AForm, QDomElement &AParentElem) const
{
  QDomDocument doc = AParentElem.ownerDocument();
  QDomElement formElem = AParentElem.appendChild(doc.createElementNS(NS_JABBER_DATA,"x")).toElement();
  formElem.setAttribute("type",AForm.type);
  
  if (!AForm.title.isEmpty())
    formElem.appendChild(doc.createElement("title")).appendChild(doc.createTextNode(AForm.title));

  foreach(QString instruction, AForm.instructions) {
    formElem.appendChild(doc.createElement("instructions")).appendChild(doc.createTextNode(instruction)); }

  foreach(IDataLayout layout, AForm.pages) {
    xmlPage(layout,AParentElem); }

  if (!AForm.tabel.columns.isEmpty()) {
    xmlTable(AForm.tabel,formElem); }

  IDataForms::FieldWriteMode mode = IDataForms::FWM_FORM;
  if (AForm.type == DATAFORM_TYPE_SUBMIT)
    mode = IDataForms::FWM_SUBMIT;
  else if (AForm.type == DATAFORM_TYPE_RESULT)
    mode = IDataForms::FWM_RESULT;
  else if (AForm.type == DATAFORM_TYPE_CANCEL)
    mode = IDataForms::FWM_SUBMIT;

  foreach(IDataField field, AForm.fields) {
    xmlField(field,formElem,mode); }
}

bool DataForms::isDataValid(const IDataValidate &AValidate, const QString &AValue) const
{
  bool valid = true;
  if (AValidate.type == DATAVALIDATE_TYPE_INTEGER || AValidate.type == DATAVALIDATE_TYPE_LONG)
  {
    int value = AValue.toInt(&valid);
    if (valid && AValidate.method == DATAVALIDATE_METHOD_RANGE)
    {
      valid &= !AValidate.min.isEmpty() ? value>=AValidate.min.toInt() : true;
      valid &= !AValidate.max.isEmpty() ? value<=AValidate.max.toInt() : true;
    }
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_BYTE)
  {
    int value = AValue.toInt(&valid);
    valid &= -128<=value && value<=127;
    if (valid && AValidate.method == DATAVALIDATE_METHOD_RANGE)
    {
      valid &= !AValidate.min.isEmpty() ? value>=AValidate.min.toInt() : true;
      valid &= !AValidate.max.isEmpty() ? value<=AValidate.max.toInt() : true;
    }
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_SHORT)
  {
    int value = AValue.toInt(&valid);
    valid &= -32768<=value && value<=32767;
    if (valid && AValidate.method == DATAVALIDATE_METHOD_RANGE)
    {
      valid &= !AValidate.min.isEmpty() ? value>=AValidate.min.toInt() : true;
      valid &= !AValidate.max.isEmpty() ? value<=AValidate.max.toInt() : true;
    }
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_INT)
  {
    int value = AValue.toInt(&valid);
    valid &= -2147483647<=value && value<=2147483647;
    if (valid && AValidate.method == DATAVALIDATE_METHOD_RANGE)
    {
      valid &= !AValidate.min.isEmpty() ? value>=AValidate.min.toInt() : true;
      valid &= !AValidate.max.isEmpty() ? value<=AValidate.max.toInt() : true;
    }
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_DECIMAL)
  {
    float value = AValue.toFloat(&valid);
    if (valid && AValidate.method == DATAVALIDATE_METHOD_RANGE)
    {
      valid &= !AValidate.min.isEmpty() ? value>=AValidate.min.toFloat() : true;
      valid &= !AValidate.max.isEmpty() ? value<=AValidate.max.toFloat() : true;
    }
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_DOUBLE)
  {
    double value = AValue.toDouble(&valid);
    if (valid && AValidate.method == DATAVALIDATE_METHOD_RANGE)
    {
      valid &= !AValidate.min.isEmpty() ? value>=AValidate.min.toDouble() : true;
      valid &= !AValidate.max.isEmpty() ? value<=AValidate.max.toDouble() : true;
    }
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_DATE)
  {
    QDate value = QDate::fromString(AValue,Qt::ISODate);
    valid = value.isValid();
    if (valid && AValidate.method == DATAVALIDATE_METHOD_RANGE)
    {
      valid &= !AValidate.min.isEmpty() ? value>=QDate::fromString(AValidate.min,Qt::ISODate) : true;
      valid &= !AValidate.max.isEmpty() ? value<=QDate::fromString(AValidate.max,Qt::ISODate) : true;
    }
    else if (valid && AValidate.method == DATAVALIDATE_METHOD_REGEXP)
    {
      valid &= AValidate.regexp.indexIn(AValue) >= 0;
    }
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_TIME)
  {
    QTime value = QTime::fromString(AValue,Qt::ISODate);
    valid = value.isValid();
    if (valid && AValidate.method == DATAVALIDATE_METHOD_RANGE)
    {
      valid &= !AValidate.min.isEmpty() ? value>=QTime::fromString(AValidate.min,Qt::ISODate) : true;
      valid &= !AValidate.max.isEmpty() ? value<=QTime::fromString(AValidate.max,Qt::ISODate) : true;
    }
    else if (valid && AValidate.method == DATAVALIDATE_METHOD_REGEXP)
    {
      valid &= AValidate.regexp.indexIn(AValue) >= 0;
    }
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_DATETIME)
  {
    QDateTime value = QDateTime::fromString(AValue,Qt::ISODate);
    valid = value.isValid();
    if (valid && AValidate.method == DATAVALIDATE_METHOD_RANGE)
    {
      valid &= !AValidate.min.isEmpty() ? value>=QDateTime::fromString(AValidate.min,Qt::ISODate) : true;
      valid &= !AValidate.max.isEmpty() ? value<=QDateTime::fromString(AValidate.max,Qt::ISODate) : true;
    }
    else if (valid && AValidate.method == DATAVALIDATE_METHOD_REGEXP)
    {
      valid &= AValidate.regexp.indexIn(AValue) >= 0;
    }
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_URI)
  {
    valid = QUrl(AValue).isValid();
    if (valid && AValidate.method == DATAVALIDATE_METHOD_REGEXP)
    {
      valid &= AValidate.regexp.indexIn(AValue) >= 0;
    }
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_LANGUAGE)
  {
    if (AValidate.method == DATAVALIDATE_METHOD_REGEXP)
    {
      valid &= AValidate.regexp.indexIn(AValue) >= 0;
    }
  }
  else 
  {
    if (AValidate.method == DATAVALIDATE_METHOD_REGEXP)
    {
      valid &= AValidate.regexp.indexIn(AValue) >= 0;
    }
  }
  return valid;
}

bool DataForms::isOptionValid(const QList<IDataOption> &AOptions, const QString &AValue) const
{
  bool valid = AOptions.isEmpty();
  for (int i=0; !valid && i<AOptions.count(); i++)
    valid |= AOptions.at(i).value == AValue;
  return valid;
}

bool DataForms::isFieldEmpty(const IDataField &AField) const
{
  return AField.value.type()==QVariant::StringList ? AField.value.toStringList().isEmpty() : AField.value.toString().isEmpty();
}

bool DataForms::isFieldValid(const IDataField &AField) const
{
  bool valid = !AField.var.isEmpty() || AField.type==DATAFIELD_TYPE_FIXED;
  valid &= !AField.required || !isFieldEmpty(AField);
  if (AField.type == DATAFIELD_TYPE_BOOLEAN)
  {
    static const QStringList boolValues = QStringList() << "0" << "false" << "1" << "true";
    QString value = AField.value.toString();
    valid = valid && boolValues.contains(AField.value.toString());
  }
  else if (AField.type == DATAFIELD_TYPE_JIDSINGLE)
  {
    QString value = AField.value.toString();
    valid &= value.isEmpty() || Jid(value).isValid();
    valid &= isDataValid(AField.validate,value);
  }
  else if (AField.type == DATAFIELD_TYPE_JIDMULTI)
  {
    QStringList value = AField.value.toStringList();
    valid &= AField.validate.listMin.isEmpty() || value.count()<=AField.validate.listMin.toInt();
    valid &= AField.validate.listMax.isEmpty() || value.count()>=AField.validate.listMax.toInt();
    for (int i=0; valid && i<value.count(); i++)
    {
      valid &= Jid(value.at(i)).isValid();
      valid &= isDataValid(AField.validate,value.at(i));
    }
  }
  else if (AField.type == DATAFIELD_TYPE_LISTSINGLE)
  {
    QString value = AField.value.toString();
    valid &= AField.validate.method == DATAVALIDATE_METHOD_OPEN || isOptionValid(AField.options, value);
    valid &= isDataValid(AField.validate,value);
  }
  else if (AField.type == DATAFIELD_TYPE_LISTMULTI)
  {
    QStringList value = AField.value.toStringList();
    valid &= AField.validate.listMin.isEmpty() || value.count()<=AField.validate.listMin.toInt();
    valid &= AField.validate.listMax.isEmpty() || value.count()>=AField.validate.listMax.toInt();
    for (int i=0; valid && i<value.count(); i++)
    {
      valid &= AField.validate.method == DATAVALIDATE_METHOD_OPEN || isOptionValid(AField.options, value.at(i));
      valid &= isDataValid(AField.validate,value.at(i));
    }
  }
  else if (AField.type == DATAFIELD_TYPE_TEXTMULTI)
  {
    QStringList value = AField.value.toStringList();
    valid &= AField.validate.listMin.isEmpty() || value.count()<=AField.validate.listMin.toInt();
    valid &= AField.validate.listMax.isEmpty() || value.count()>=AField.validate.listMax.toInt();
    for (int i=0; valid && i<value.count(); i++)
      valid &= isDataValid(AField.validate,value.at(i));
  }
  else 
  {
    QString value = AField.value.toString();
    valid &= isDataValid(AField.validate,value);
  }
  return valid;
}

bool DataForms::isFormValid(const IDataForm &AForm) const
{
  bool valid = !AForm.type.isEmpty();

  for (int i=0; valid && i<AForm.fields.count();i++)
    valid &= isFieldValid(AForm.fields.at(i));

  return valid;
}

bool DataForms::isSubmitValid(const IDataForm &AForm, const IDataForm &ASubmit) const
{
  bool valid = true;
  for (int i=0; valid && i<AForm.fields.count(); i++)
  {
    const IDataField &formField = AForm.fields.at(i);
    if (!formField.var.isEmpty())
    {
      int sumbIndex = fieldIndex(formField.var,ASubmit.fields);
      if (sumbIndex>=0)
      {
        IDataField submField = ASubmit.fields.at(sumbIndex);
        if (!isFieldEmpty(formField))
        {
          submField.type = formField.type;
          submField.options = formField.options;
          submField.validate = formField.validate;
          valid &= isFieldValid(submField);
        }
        else
          valid &= !formField.required;
      }
      else
        valid &= !formField.required;
    }
  }
  return valid;
}

int DataForms::fieldIndex(const QString &AVar, const QList<IDataField> &AFields) const
{
  for(int index=0; index<AFields.count(); index++)
    if (AFields.at(index).var == AVar)
      return index;
  return -1;
}

QVariant DataForms::fieldValue(const QString &AVar, const QList<IDataField> &AFields) const
{
  int index = fieldIndex(AVar,AFields);
  return index >= 0 ? AFields.at(index).value : QVariant();
}

IDataForm DataForms::dataSubmit(const IDataForm &AForm) const
{
  IDataForm form;
  form.type = DATAFORM_TYPE_SUBMIT;
  foreach(IDataField field, AForm.fields)
  {
    if (!field.var.isEmpty() && field.type!=DATAFIELD_TYPE_FIXED && !isFieldEmpty(field))
    {
      IDataField submField;
      submField.type = field.type;
      submField.var = field.var;
      submField.value = field.value;
      submField.required = false;
      form.fields.append(submField);
    }
  }
  return form;
}

QValidator *DataForms::dataValidator(const IDataValidate &AValidate, QObject *AParent) const
{
  QValidator *validator = NULL;
  if (AValidate.type == DATAVALIDATE_TYPE_INTEGER || AValidate.type == DATAVALIDATE_TYPE_LONG)
  {
    QIntValidator *intValidator = new QIntValidator(AParent);
    if (!AValidate.min.isEmpty())
      intValidator->setBottom(AValidate.min.toInt());
    if (!AValidate.max.isEmpty())
      intValidator->setTop(AValidate.max.toInt());
    validator = intValidator;
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_BYTE)
  {
    QIntValidator *intValidator = new QIntValidator(AParent);
    intValidator->setBottom(AValidate.min.isEmpty() ? -128 : AValidate.min.toInt());
    intValidator->setTop(AValidate.max.isEmpty() ? 127 : AValidate.max.toInt());
    validator = intValidator;
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_SHORT)
  {
    QIntValidator *intValidator = new QIntValidator(AParent);
    intValidator->setBottom(AValidate.min.isEmpty() ? -32768 : AValidate.min.toInt());
    intValidator->setTop(AValidate.max.isEmpty() ? -32767 : AValidate.max.toInt());
    validator = intValidator;
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_INT)
  {
    QIntValidator *intValidator = new QIntValidator(AParent);
    intValidator->setBottom(AValidate.min.isEmpty() ? -2147483647 : AValidate.min.toInt());
    intValidator->setTop(AValidate.max.isEmpty() ? 2147483647 : AValidate.max.toInt());
    validator = intValidator;
  }
  else if (AValidate.type == DATAVALIDATE_TYPE_DOUBLE || AValidate.type == DATAVALIDATE_TYPE_DECIMAL)
  {
    QDoubleValidator *doubleValidator = new QDoubleValidator(AParent);
    if (!AValidate.min.isEmpty())
      doubleValidator->setBottom(AValidate.min.toFloat());
    if (!AValidate.max.isEmpty())
      doubleValidator->setTop(AValidate.max.toFloat());
    validator = doubleValidator;
  }
  else if (AValidate.method == DATAVALIDATE_METHOD_REGEXP)
  {
    QRegExpValidator *regexpValidator = new QRegExpValidator(AParent);
    regexpValidator->setRegExp(AValidate.regexp);
    validator = regexpValidator;
  }
  return validator;
}

IDataTableWidget *DataForms::tableWidget(const IDataTable &ATable, QWidget *AParent)
{
  IDataTableWidget *table = new DataTableWidget(this,ATable,AParent);
  emit tableWidgetCreated(table);
  return table;
}

IDataFieldWidget *DataForms::fieldWidget(const IDataField &AField, bool AReadOnly, QWidget *AParent)
{
  IDataFieldWidget *field = new DataFieldWidget(this,AField,AReadOnly,AParent);
  emit fieldWidgetCreated(field);
  return field;
}

IDataFormWidget *DataForms::formWidget(const IDataForm &AForm, QWidget *AParent)
{
  IDataFormWidget *form = new DataFormWidget(this,AForm,AParent);
  emit formWidgetCreated(form);
  return form;
}

IDataDialogWidget *DataForms::dialogWidget(const IDataForm &AForm, QWidget *AParent)
{
  IDataDialogWidget *dialog = new DataDialogWidget(this,AForm,AParent);
  emit dialogWidgetCreated(dialog);
  return dialog;
}

void DataForms::xmlLayout(const IDataLayout &ALayout, QDomElement &ALayoutElem) const
{
  QDomDocument doc = ALayoutElem.ownerDocument();
  if (!ALayout.label.isEmpty())
    ALayoutElem.setAttribute("label",ALayout.label);

  int textCounter = 0;
  int fieldCounter = 0;
  int sectionCounter = 0;
  foreach(QString childName, ALayout.childOrder)
  {
    if (childName == "text")
    {
      ALayoutElem.appendChild(doc.createElement(childName)).appendChild(doc.createTextNode(ALayout.text.value(textCounter++)));
    }
    else if (childName == "fieldref")
    {
      QDomElement fieldElem = ALayoutElem.appendChild(doc.createElement(childName)).toElement();
      fieldElem.setAttribute("var",ALayout.fieldrefs.value(fieldCounter++));
    }
    else if (childName == "reportedref")
    {
      ALayoutElem.appendChild(doc.createElement(childName));
    }
    else if (childName == "section")
    {
      QDomElement sectionElem = ALayoutElem.appendChild(doc.createElement("section")).toElement();
      xmlSection(ALayout.sections.value(sectionCounter++),sectionElem);
    }
  }
}

void DataForms::registerDiscoFeatures()
{
  IDiscoFeature dfeature;
  dfeature.active = true;
  dfeature.icon = QIcon();
  dfeature.var = NS_JABBER_DATA;
  dfeature.name = tr("Data Forms");
  dfeature.actionName = "";
  dfeature.description = tr("Implements data forms and generic data description");
  FDiscovery->insertDiscoFeature(dfeature);

  dfeature.active = true;
  dfeature.icon = QIcon();
  dfeature.var = NS_JABBER_XDATAVALIDATE;
  dfeature.name = tr("Data Forms Validation");
  dfeature.actionName = "";
  dfeature.description = tr("Enables to specify additional validation guidelines related to a data form");
  FDiscovery->insertDiscoFeature(dfeature);

  dfeature.active = true;
  dfeature.icon = QIcon();
  dfeature.var = NS_JABBER_XDATALAYOUT;
  dfeature.name = tr("Data Forms Layout");
  dfeature.actionName = "";
  dfeature.description = tr("Enables to specify form layouts, including the layout of form fields, pages and sections");
  FDiscovery->insertDiscoFeature(dfeature);
}

Q_EXPORT_PLUGIN2(DataFormsPlugin, DataForms)