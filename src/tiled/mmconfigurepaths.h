#ifndef MMCONFIGUREPATHS_H
#define MMCONFIGUREPATHS_H

#include <QDialog>

namespace Ui {
class MMConfigurePaths;
}

class MMConfigurePaths : public QDialog
{
	Q_OBJECT

public:
	explicit MMConfigurePaths(QWidget *parent = nullptr);
	~MMConfigurePaths();

private:
	Ui::MMConfigurePaths *ui;
};

#endif // MMCONFIGUREPATHS_H
