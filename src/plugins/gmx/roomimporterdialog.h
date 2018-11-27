#pragma once

#include <QDialog>
#include <QAction>

namespace Ui {
    class RoomImporterDialog;
}

namespace Gmx{
    struct ImporterSettings
    {
        int tileWidth;
        int tileHeigth;
        int quadWidth;
        int quadHeigth;
        QString templatePath;
        QString imagesPath;
    };
    class RoomImporterDialog : public QDialog
    {
    Q_OBJECT

    public:
        RoomImporterDialog(QWidget *parent = nullptr, bool *oK = nullptr, ImporterSettings *settings = nullptr);
        ~RoomImporterDialog();
        ImporterSettings getSettings();
    private slots:
        void on_buttonBox_accepted();

        void on_buttonBox_rejected();

        void on_imageDir_clicked();

        void on_templateDir_clicked();

    private:
        Ui::RoomImporterDialog *mUi;
        bool *mOk;
        ImporterSettings *mSettings;
    };
}
