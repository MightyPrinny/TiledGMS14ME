#ifndef MMGENERATETEMPLATESDIALOG_H
#define MMGENERATETEMPLATESDIALOG_H

#include <QDialog>
#include <QAbstractButton>
class GameMakerObjectImporter;

namespace Ui {
class MMGenerateTemplatesDialog;
}

class MMGenerateTemplatesDialog : public QDialog
{
	Q_OBJECT

public:
	explicit MMGenerateTemplatesDialog(QWidget *parent = nullptr);
	~MMGenerateTemplatesDialog();

	GameMakerObjectImporter *importer;

public slots:


	void on_buttonBox_accepted();

	void on_buttonBox_clicked(QAbstractButton *button);

private slots:
	void on_gmProjectBrowse_triggered(QAction *arg1);

	void on_outputDirBrowse_triggered(QAction *arg1);

	void on_outputDirBrowse_clicked();

	void on_gmProjectBrowse_clicked();

private:
	Ui::MMGenerateTemplatesDialog *ui;
};

#endif // MMGENERATETEMPLATESDIALOG_H
