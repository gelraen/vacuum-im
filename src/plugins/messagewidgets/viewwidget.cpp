#include "viewwidget.h"

#include <QTextFrame>
#include <QTextTable>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QTextDocumentFragment>
#include <utils/textmanager.h>

ViewWidget::ViewWidget(IMessageWidgets *AMessageWidgets, IMessageWindow *AWindow, QWidget *AParent) : QWidget(AParent)
{
	ui.setupUi(this);
	setAcceptDrops(true);

	QVBoxLayout *layout = new QVBoxLayout(ui.wdtViewer);
	layout->setMargin(0);

	FMessageStyle = NULL;
	FMessageProcessor = NULL;

	FWindow = AWindow;
	FMessageWidgets = AMessageWidgets;

	FStyleWidget = NULL;

	initialize();
}

ViewWidget::~ViewWidget()
{

}

bool ViewWidget::isVisibleOnWindow() const
{
	return isVisibleTo(FWindow->instance());
}

IMessageWindow *ViewWidget::messageWindow() const
{
	return FWindow;
}

void ViewWidget::clearContent()
{
	if (FMessageStyle)
		FMessageStyle->changeOptions(FStyleWidget,FStyleOptions,true);
}

QWidget *ViewWidget::styleWidget() const
{
	return FStyleWidget;
}

IMessageStyle *ViewWidget::messageStyle() const
{
	return FMessageStyle;
}

void ViewWidget::setMessageStyle(IMessageStyle *AStyle, const IMessageStyleOptions &AOptions)
{
	if (FMessageStyle != AStyle)
	{
		if (FMessageStyle)
		{
			disconnect(FMessageStyle->instance(),SIGNAL(urlClicked(QWidget *, const QUrl &)),
				this,SLOT(onMessageStyleUrlClicked(QWidget *, const QUrl &)));
			disconnect(FStyleWidget,SIGNAL(customContextMenuRequested(const QPoint &)),
				this,SLOT(onMessageStyleWidgetCustomContextMenuRequested(const QPoint &)));
			disconnect(FMessageStyle->instance(),SIGNAL(contentAppended(QWidget *, const QString &, const IMessageContentOptions &)),
				this, SLOT(onMessageStyleContentAppended(QWidget *, const QString &, const IMessageContentOptions &)));
			disconnect(FMessageStyle->instance(),SIGNAL(optionsChanged(QWidget *, const IMessageStyleOptions &, bool)),
				this,SLOT(onMessageStyleOptionsChanged(QWidget *, const IMessageStyleOptions &, bool)));

			ui.wdtViewer->layout()->removeWidget(FStyleWidget);
			FStyleWidget->deleteLater();
			FStyleWidget = NULL;
		}

		IMessageStyle *before = FMessageStyle;
		FMessageStyle = AStyle;

		if (FMessageStyle)
		{
			FStyleWidget = FMessageStyle->createWidget(AOptions,ui.wdtViewer);
			FStyleWidget->setContextMenuPolicy(Qt::CustomContextMenu);
			ui.wdtViewer->layout()->addWidget(FStyleWidget);

			connect(FMessageStyle->instance(),SIGNAL(urlClicked(QWidget *, const QUrl &)),
				SLOT(onMessageStyleUrlClicked(QWidget *, const QUrl &)));
			connect(FStyleWidget,SIGNAL(customContextMenuRequested(const QPoint &)),
				SLOT(onMessageStyleWidgetCustomContextMenuRequested(const QPoint &)));
			connect(FMessageStyle->instance(),SIGNAL(contentAppended(QWidget *, const QString &, const IMessageContentOptions &)),
				SLOT(onMessageStyleContentAppended(QWidget *, const QString &, const IMessageContentOptions &)));
			connect(FMessageStyle->instance(),SIGNAL(optionsChanged(QWidget *, const IMessageStyleOptions &, bool)),
				SLOT(onMessageStyleOptionsChanged(QWidget *, const IMessageStyleOptions &, bool)));
		}

		FStyleOptions = AOptions;
		emit messageStyleChanged(before,AOptions);
	}
}

void ViewWidget::appendHtml(const QString &AHtml, const IMessageContentOptions &AOptions)
{
	if (FMessageStyle)
		FMessageStyle->appendContent(FStyleWidget,AHtml,AOptions);
}

void ViewWidget::appendText(const QString &AText, const IMessageContentOptions &AOptions)
{
	Message message;
	message.setBody(AText);
	appendMessage(message,AOptions);
}

void ViewWidget::appendMessage(const Message &AMessage, const IMessageContentOptions &AOptions)
{
	QTextDocument doc;
	if (FMessageProcessor)
		FMessageProcessor->messageToText(&doc,AMessage);
	else
		doc.setPlainText(AMessage.body());

	// "/me" command
	IMessageContentOptions options = AOptions;
	if (AOptions.kind==IMessageContentOptions::KindMessage && !AOptions.senderName.isEmpty())
	{
		QTextCursor cursor(&doc);
		cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor,4);
		if (cursor.selectedText() == "/me ")
		{
			options.kind = IMessageContentOptions::KindMeCommand;
			cursor.removeSelectedText();
		}
	}

	appendHtml(TextManager::getDocumentBody(doc),options);
}

void ViewWidget::contextMenuForView(const QPoint &APosition, Menu *AMenu)
{
	emit viewContextMenu(APosition, AMenu);
}

QTextDocumentFragment ViewWidget::selection() const
{
	return FMessageStyle!=NULL ? FMessageStyle->selection(FStyleWidget) : QTextDocumentFragment();
}

QTextCharFormat ViewWidget::textFormatAt(const QPoint &APosition) const
{
	if (FMessageStyle && FStyleWidget)
		return FMessageStyle->textFormatAt(FStyleWidget,FStyleWidget->mapFromGlobal(mapToGlobal(APosition)));
	return QTextCharFormat();
}

QTextDocumentFragment ViewWidget::textFragmentAt(const QPoint &APosition) const
{
	if (FMessageStyle && FStyleWidget)
		return FMessageStyle->textFragmentAt(FStyleWidget,FStyleWidget->mapFromGlobal(mapToGlobal(APosition)));
	return QTextDocumentFragment();
}

void ViewWidget::initialize()
{
	IPlugin *plugin = FMessageWidgets->pluginManager()->pluginInterface("IMessageProcessor").value(0,NULL);
	if (plugin)
		FMessageProcessor = qobject_cast<IMessageProcessor *>(plugin->instance());
}

void ViewWidget::dropEvent(QDropEvent *AEvent)
{
	Menu *dropMenu = new Menu(this);

	bool accepted = false;
	foreach(IMessageViewDropHandler *handler, FActiveDropHandlers)
		if (handler->messageViewDropAction(this, AEvent, dropMenu))
			accepted = true;

	if (accepted && !dropMenu->isEmpty())
	{
		if (dropMenu->exec(mapToGlobal(AEvent->pos())))
			AEvent->acceptProposedAction();
		else
			AEvent->ignore();
	}
	else
	{
		AEvent->ignore();
	}
	delete dropMenu;
}

void ViewWidget::dragEnterEvent(QDragEnterEvent *AEvent)
{
	FActiveDropHandlers.clear();
	foreach(IMessageViewDropHandler *handler, FMessageWidgets->viewDropHandlers())
		if (handler->messagaeViewDragEnter(this, AEvent))
			FActiveDropHandlers.append(handler);

	if (!FActiveDropHandlers.isEmpty())
		AEvent->acceptProposedAction();
	else
		AEvent->ignore();
}

void ViewWidget::dragMoveEvent(QDragMoveEvent *AEvent)
{
	bool accepted = false;
	foreach(IMessageViewDropHandler *handler, FActiveDropHandlers)
		if (handler->messageViewDragMove(this, AEvent))
			accepted = true;

	if (accepted)
		AEvent->acceptProposedAction();
	else
		AEvent->ignore();
}

void ViewWidget::dragLeaveEvent(QDragLeaveEvent *AEvent)
{
	foreach(IMessageViewDropHandler *handler, FActiveDropHandlers)
		handler->messageViewDragLeave(this, AEvent);
}

void ViewWidget::onMessageStyleUrlClicked(QWidget *AWidget, const QUrl &AUrl)
{
	if (AWidget == FStyleWidget)
	{
		QMap<int,IMessageViewUrlHandler *> handlers = FMessageWidgets->viewUrlHandlers();
		for (QMap<int,IMessageViewUrlHandler *>::const_iterator it = handlers.constBegin(); it!=handlers.constEnd(); ++it)
			if (it.value()->messageViewUrlOpen(it.key(),this,AUrl))
				break;
		emit urlClicked(AUrl);
	}
}

void ViewWidget::onMessageStyleWidgetCustomContextMenuRequested(const QPoint &APosition)
{
	Menu *menu = new Menu(this);
	menu->setAttribute(Qt::WA_DeleteOnClose, true);

	contextMenuForView(APosition, menu);

	if (!menu->isEmpty())
		menu->popup(FStyleWidget->mapToGlobal(APosition));
	else
		delete menu;
}

void ViewWidget::onMessageStyleOptionsChanged(QWidget *AWidget, const IMessageStyleOptions &AOptions, bool AClean)
{
	if (AWidget == FStyleWidget)
	{
		FStyleOptions = AOptions;
		emit messageStyleOptionsChanged(AOptions,AClean);
	}
}

void ViewWidget::onMessageStyleContentAppended(QWidget *AWidget, const QString &AHtml, const IMessageContentOptions &AOptions)
{
	if (AWidget == FStyleWidget)
		emit contentAppended(AHtml,AOptions);
}
