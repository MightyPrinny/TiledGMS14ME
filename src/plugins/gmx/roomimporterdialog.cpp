#include "roomimporterdialog.h"
#include "ui_roomimporterdialog.h"
#include <QFileDialog>

using namespace Gmx;

RoomImporterDialog::RoomImporterDialog(QWidget* parent , bool *oK, ImporterSettings *settings):
    QDialog(parent),
    mUi(new Ui::RoomImporterDialog())
{
    mOk = oK;
    mSettings = settings;
    mUi->setupUi(this);

	mUi->imagesLabel->setText(settings->imagesPath);
	mUi->templateLabel->setText(settings->templatePath);
    this->setWindowTitle(QString("Game Maker Room Importer"));

}

RoomImporterDialog::~RoomImporterDialog()
{
    delete mUi;
}

ImporterSettings RoomImporterDialog::getSettings()
{
    ImporterSettings settings =
    {
        mUi->tileWidth->value(),
        mUi->tileHeight->value(),
        mUi->quadWidth->value(),
        mUi->quadHeight->value(),
        mUi->templateLabel->text(),
        mUi->imagesLabel->text()
    };

	return settings;
}

void RoomImporterDialog::setDefaultPaths(QSettings *appSettings)
{
	QVariant val = appSettings->value("Interface/GMProjectPath");
	if(val.canConvert(QVariant::String))
	{
		QString str = val.toString();
		QDir projDir(str);
		projDir.cd("background/images");
		mUi->imagesLabel->setText(projDir.path());
	}

	val = appSettings->value("Interface/GenTemplatesOutDir");

	if(val.canConvert(QVariant::String))
	{
		QString str = val.toString();
		QDir outPath(str);
		outPath.cd(QString("templates"));
		mUi->templateLabel->setText(outPath.path());
	}

}



void Gmx::RoomImporterDialog::on_imageDir_clicked()
{
    QString path = QFileDialog::getExistingDirectory(this,"Tileset image directory","../Backgrounds/images");
    mUi->imagesLabel->setText(path);
}

void Gmx::RoomImporterDialog::on_templateDir_clicked()
{
    QString path = QFileDialog::getExistingDirectory(this,"Templates directory","../Objects/templates");
    mUi->templateLabel->setText(path);
}

void Gmx::RoomImporterDialog::on_buttonBox_accepted()
{
    if(mSettings!=nullptr)
        *mSettings = getSettings();
    if(mOk!=nullptr)
        *mOk = true;
    this->close();
}

void Gmx::RoomImporterDialog::on_buttonBox_rejected()
{
    if(mOk!=nullptr)
        *mOk=false;
    this->close();
}

