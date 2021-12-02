#include "bgximporterdialog.h"
#include "ui_bgximporterdialog.h"

BGXImporterDialog::BGXImporterDialog(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::BGXImporterDialog)
{
	ui->setupUi(this);
	ui->tileWidthSpin->setMinimum(0);
	ui->tileHeightSpin->setMinimum(0);
}

void BGXImporterDialog::SetDefaultsFromSettings(const QSettings *settings)
{
	auto size = settings->value(QStringLiteral("GMSMESizes/lastUsedMapTilesize"), QSize(16,16)).toSize();
	ui->tileWidthSpin->setValue(size.width());
	ui->tileHeightSpin->setValue(size.height());
}

BGXImporterDialog::~BGXImporterDialog()
{
	delete ui;
}

void BGXImporterDialog::on_buttonBox_accepted()
{
	tileSize = QSize(ui->tileWidthSpin->value(), ui->tileHeightSpin->value());
	loadFromBg = ui->loadFromBgCheckBox->isChecked();
	accepted = true;
}
