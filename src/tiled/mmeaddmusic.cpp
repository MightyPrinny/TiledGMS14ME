#include "mmeaddmusic.h"
#include "ui_mmeaddmusic.h"

MMEAddMusic::MMEAddMusic(QWidget *parent, MusicInfo* musicInfo):
    QDialog(parent),
    mUi(new Ui::MMEAddMusic)
{
    mMusicInfo = musicInfo;
    mUi->setupUi(this);
    mUi->comboBox->addItem(QString(QLatin1String("VGM")));
    mUi->comboBox->addItem(QString(QLatin1String("OGG")));
}

MMEAddMusic::~MMEAddMusic()
{
    delete mUi;
}

void MMEAddMusic::on_buttonBox_accepted()
{
    //Update music info
    if(mMusicInfo != nullptr)
    {
        mMusicInfo->filename = mUi->lineEdit->text();
        mMusicInfo->type = mUi->comboBox->currentText();
        mMusicInfo->volume = mUi->volumeSpin->value();
        mMusicInfo->ok=true;

        if(mMusicInfo->type != QString(QLatin1String("VGM")))
        {
            mMusicInfo->track = 0;
            mMusicInfo->duration = mUi->durationSpin->value();
            mMusicInfo->loops = mUi->loopCb->isChecked();
            if(mMusicInfo->loops)
                mMusicInfo->loopPoint = mUi->loopSpin->value();
            else
                mMusicInfo->loopPoint = 0;
        }
        else
        {
            mMusicInfo->track = mUi->trackSpin->value();
            mMusicInfo->duration = 0;
            mMusicInfo->loops = 0;
            mMusicInfo->loopPoint = 0;
        }

    }
    close();
}

void MMEAddMusic::on_comboBox_currentIndexChanged(const QString &arg1)
{
    bool isVGM = false;
    if(arg1 == QString(QLatin1String("VGM")))
    {
        isVGM = true;
    }
    mUi->durationSpin->setDisabled(isVGM);
    mUi->loopSpin->setDisabled(isVGM);
    mUi->loopCb->setDisabled(isVGM);
    mUi->trackSpin->setDisabled(!isVGM);
}

void MMEAddMusic::on_loopCb_toggled(bool checked)
{
    mUi->loopSpin->setDisabled(!checked);
}

void MMEAddMusic::on_buttonBox_rejected()
{
    if(mMusicInfo != nullptr)
    {
        mMusicInfo->ok=false;
    }
    close();
}
