#include "mmconfigurepaths.h"
#include "ui_mmconfigurepaths.h"

MMConfigurePaths::MMConfigurePaths(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::MMConfigurePaths)
{
	ui->setupUi(this);
}

MMConfigurePaths::~MMConfigurePaths()
{
	delete ui;
}
