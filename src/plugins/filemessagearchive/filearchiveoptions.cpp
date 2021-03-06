#include "filearchiveoptions.h"

#include <QFileDialog>
#include <definitions/optionvalues.h>
#include <utils/options.h>

FileArchiveOptions::FileArchiveOptions(IPluginManager *APluginManager, QWidget *AParent) : QWidget(AParent)
{
	ui.setupUi(this);
	FPluginManager = APluginManager;

	connect(ui.chbLocation,SIGNAL(toggled(bool)),SIGNAL(modified()));
	connect(ui.lneLocation,SIGNAL(textChanged(const QString &)),SIGNAL(modified()));
	connect(ui.chbForceDatabaseSync,SIGNAL(toggled(bool)),SIGNAL(modified()));

	connect(ui.tlbLocation,SIGNAL(clicked()),SLOT(onSelectLocationFolder()));

	reset();
}

FileArchiveOptions::~FileArchiveOptions()
{

}

void FileArchiveOptions::apply()
{
	Options::node(OPV_FILEARCHIVE_HOMEPATH).setValue(ui.chbLocation->isChecked() ? ui.lneLocation->text() : QString(""));
	Options::node(OPV_FILEARCHIVE_FORCEDATABASESYNC).setValue(ui.chbForceDatabaseSync->isChecked());
	emit childApply();
}

void FileArchiveOptions::reset()
{
	QString path = Options::node(OPV_FILEARCHIVE_HOMEPATH).value().toString();
	ui.chbLocation->setChecked(!path.isEmpty());
	ui.lneLocation->setText(!path.isEmpty() ? path : FPluginManager->homePath());
	ui.chbForceDatabaseSync->setChecked(Options::node(OPV_FILEARCHIVE_FORCEDATABASESYNC).value().toBool());
	emit childReset();
}

void FileArchiveOptions::onSelectLocationFolder()
{
	QString path = QFileDialog::getExistingDirectory(this,tr("Select the location for the file archive"));
	if (!path.isEmpty())
		ui.lneLocation->setText(path);
}
