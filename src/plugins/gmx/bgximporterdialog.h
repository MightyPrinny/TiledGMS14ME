#ifndef BGXIMPORTERDIALOG_H
#define BGXIMPORTERDIALOG_H

#include <QDialog>
#include <QSettings>

namespace Ui {
class BGXImporterDialog;
}

class BGXImporterDialog : public QDialog
{
	Q_OBJECT

public:
	explicit BGXImporterDialog(QWidget *parent = nullptr);
	void SetDefaultsFromSettings(const QSettings *settings);
	~BGXImporterDialog();
	QSize tileSize;
	bool loadFromBg;
	bool accepted;

private slots:
	void on_buttonBox_accepted();

private:
	Ui::BGXImporterDialog *ui;
};

#endif // BGXIMPORTERDIALOG_H
