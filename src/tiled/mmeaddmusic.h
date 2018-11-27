#pragma once
#include <QObject>
#include <QDialog>

namespace Ui
{
    class MMEAddMusic;
}
struct MusicInfo
{
    QString filename;
    QString type;
    int track;
    double loopPoint;
    double duration;
    double volume;
    bool loops;
    bool ok;
};
class MMEAddMusic : public QDialog
{
    Q_OBJECT
public:
    MMEAddMusic(QWidget *parent = nullptr,MusicInfo* musicInfo = nullptr);
    ~MMEAddMusic();
private slots:
    void on_buttonBox_accepted();

    void on_comboBox_currentIndexChanged(const QString &arg1);

    void on_loopCb_toggled(bool checked);

    void on_buttonBox_rejected();

private:
    MusicInfo *mMusicInfo;
    Ui::MMEAddMusic *mUi;
};
