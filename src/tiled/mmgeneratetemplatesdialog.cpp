#include "mmgeneratetemplatesdialog.h"
#include "ui_mmgeneratetemplatesdialog.h"
#include <preferences.h>
#include <QFileDialog>>
#include <gamemakerobjectimporter.h>
#include <QDebug>
#include <QAction>


using namespace Tiled;
using namespace Tiled::Internal;

MMGenerateTemplatesDialog::MMGenerateTemplatesDialog(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::MMGenerateTemplatesDialog)
{
	Preferences *prefs = Preferences::instance();
	ui->setupUi(this);

	connect(ui->gmProjectBrowse, &QToolButton::triggered, this, &MMGenerateTemplatesDialog::on_gmProjectBrowse_triggered);
	connect(ui->outputDirBrowse, &QToolButton::triggered, this, &MMGenerateTemplatesDialog::on_outputDirBrowse_triggered);

	ui->gmProjectLineEdit->setText(prefs->gmProjectPath());
	ui->outputDirLineEdit->setText(prefs->genTemplatesOutDir());
	succeeded = false;
}

MMGenerateTemplatesDialog::~MMGenerateTemplatesDialog()
{
	delete ui;
}

void MMGenerateTemplatesDialog::on_gmProjectBrowse_triggered(QAction *arg1)
{



}

void MMGenerateTemplatesDialog::on_outputDirBrowse_triggered(QAction *arg1)
{

}

void MMGenerateTemplatesDialog::on_buttonBox_accepted()
{

}

void MMGenerateTemplatesDialog::on_buttonBox_clicked(QAbstractButton *button)
{
	if(ui->buttonBox->standardButton(button) == QDialogButtonBox::Ok)
	{
		succeeded = importer->generateTemplates(ui->gmProjectLineEdit->text(), ui->outputDirLineEdit->text(), ui->deleteOldFilesCheckBox->isChecked(), true);
	}
}

void MMGenerateTemplatesDialog::on_outputDirBrowse_clicked()
{
	QString path = QFileDialog::getExistingDirectory(nullptr, QLatin1String("Choose an output directory for the templates folder"),
													 QLatin1String(""),
													 QFileDialog::ShowDirsOnly
													 | QFileDialog::DontUseCustomDirectoryIcons);
	if(!path.isEmpty())
	{
		ui->outputDirLineEdit->setText(path);
	}

}

void MMGenerateTemplatesDialog::on_gmProjectBrowse_clicked()
{
	QString path = QFileDialog::getExistingDirectory(nullptr, QLatin1String("Open a game maker project's directory"),
													 QLatin1String(""),
													 QFileDialog::ShowDirsOnly
													 | QFileDialog::DontUseCustomDirectoryIcons);
	if(!path.isEmpty())
	{
		ui->gmProjectLineEdit->setText(path);
	}


}
