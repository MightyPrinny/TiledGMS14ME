#ifndef GAMEMAKEROBJECTIMPORTER_H
#define GAMEMAKEROBJECTIMPORTER_H

#pragma once
#include <QObject>
#include <qxmlstream.h>
#include "tiled.h"
#include <QFileDialog>
#include <QProgressDialog>
#include <QThread>
#include <unordered_map>
#include <string>
using namespace Tiled;



class imageEntry
{
public:
    imageEntry(QString npath,QString nRPath,int nimageWidth, int nimageHeigth):
          path(npath),
          imageWidth(nimageWidth),
          imageHeigth(nimageHeigth),
          rPath(nRPath)
    {}
    QString path;
    QString rPath;
    int imageWidth=0;
    int imageHeigth=0;
};

class GameMakerObjectImporter : public QThread
{
    Q_OBJECT
public:
    GameMakerObjectImporter();
    void generateTemplates();
    void generateTemplatesInThread();
protected:
    void run() override;
private:
    std::unique_ptr<QProgressDialog>progress = nullptr;
    QFile *findSprite(QString filename, QDir* dir);
    GameMakerObjectImporter *myThread = nullptr;
    int addImage(QString &fileDir, int width, int heigth, QVector<imageEntry *> *list);
private slots:
    void finnishThread();
};


#endif // GAMEMAKEROBJECTIMPORTER_H
