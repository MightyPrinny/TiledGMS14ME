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
#include "rapidxml.hpp"
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
    GameMakerObjectImporter(QWidget *wd);
    void generateTemplates();
    void generateTemplatesInThread();
protected:
    void run() override;
private:
    QWidget *prtWidget;
    QFile *findSprite(QString filename, QDir* dir);
    GameMakerObjectImporter *myThread = nullptr;
    void mapChilds(rapidxml::xml_node<>* node, std::unordered_map<std::string,std::string> *objectFolderMap);
    void mapObjectsToFolders(QString &projectFilePath, std::unordered_map<std::string, std::string> *objectFolderMap);
    int addImage(QString &filename, QString &fileDir, int width, int heigth, QVector<imageEntry *> *list, std::unordered_map<std::string, int> *idmap);
    QString getNodePath(rapidxml::xml_node<>* node);
private slots:
    void finnishThread();
};


#endif // GAMEMAKEROBJECTIMPORTER_H
