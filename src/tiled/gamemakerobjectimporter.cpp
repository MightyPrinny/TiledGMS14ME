#pragma once
#include "gamemakerobjectimporter.h"
#include <iostream>
#include <Qt>
#include <memory>
#include "pugixml.hpp"
GameMakerObjectImporter::GameMakerObjectImporter()
{

}

void GameMakerObjectImporter::run()
{
    this->generateTemplates();
}

void GameMakerObjectImporter::generateTemplatesInThread()
{
    myThread = new GameMakerObjectImporter();
    connect(myThread, &GameMakerObjectImporter::finished, myThread, &GameMakerObjectImporter::finnishThread);
    myThread->start(QThread::HighPriority);
}

void GameMakerObjectImporter::finnishThread()
{
    myThread->deleteLater();
    delete myThread;
}

void GameMakerObjectImporter::generateTemplates()
{
    QString dir = QFileDialog::getExistingDirectory(nullptr, QLatin1String("Open Directory"),
                                                      QLatin1String(""),
                                                      QFileDialog::ShowDirsOnly
                                                      | QFileDialog::DontResolveSymlinks);
    if(dir.isEmpty())
    {
        printf("empty dir");
        return;
    }
    QDir* rootDir = new QDir(dir);
    rootDir->cd(QLatin1String("objects"));
    QDir* objectDir = new QDir(rootDir->path());
    rootDir->cdUp();
    rootDir->cd(QLatin1String("sprites"));
    QDir* spriteDir = new QDir(rootDir->path());
    rootDir->cd(QLatin1String("images"));
    QDir* imageDir = new QDir(rootDir->path());
    rootDir->cdUp();
    rootDir->cdUp();
    rootDir->mkdir(QLatin1String("templates"));
    rootDir->cd(QLatin1String("templates"));

    QDir* templateDir = new QDir(rootDir->path());
    rootDir->cdUp();

    objectDir->setFilter(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    objectDir->setSorting(QDir::Size | QDir::Reversed);

    QFileInfoList objectFileInfo = objectDir->entryInfoList();
    int totalObjects =objectFileInfo.size();

    QXmlStreamReader reader;
    QXmlStreamWriter writer;
    writer.setAutoFormatting(true);

    QString undefined = QLatin1String("&lt;undefined&gt;");

    QFile *imageCollection = new QFile(rootDir->path().append(QLatin1String("/images.tsx")));
    imageCollection->open(QIODevice::ReadWrite | QIODevice::Text);
    imageCollection->flush();

    QString typesDir = rootDir->path().append(QLatin1String("/types.xml"));
    QString imageCollectionPath = templateDir->relativeFilePath(rootDir->path().append(QLatin1String("/images.tsx")));

    QVector<imageEntry*>* imageList = new QVector<imageEntry*>();
    imageList->reserve(totalObjects);
    int maxWidth = 0;
    int maxHeigth = 0;
    //Loop through object files

    progress=std::unique_ptr<QProgressDialog>(new QProgressDialog(nullptr));
    progress->setWindowModality(Qt::WindowModal);
    progress->setLabelText(QLatin1String("Generating templates..."));
    progress->setAutoFillBackground(false);
    progress->setMaximum(objectFileInfo.size());
    progress->setMinimum(0);
    progress->setWindowFlags(Qt::WindowMinMaxButtonsHint);

    pugi::xml_document templateDoc;
    pugi::xml_document typesDoc;
    pugi::xml_node objecttypes = typesDoc.append_child("objecttypes");

    for (int i = 0; i < totalObjects; ++i) {
        progress->setValue(i);
        if(progress->wasCanceled())
            break;
        QDir* root =new QDir(QLatin1String(""));
        QFileInfo fileInfo = objectFileInfo.at(i);
        QString poth = root->relativeFilePath(fileInfo.filePath());
        QFile *objectFile = new QFile(poth);
        delete root;
        if(!objectFile->open(QIODevice::ReadOnly | QIODevice::Text))
        {
            objectFile->close();
            delete objectFile;
            continue;
        }

        reader.setDevice(objectFile);
        //Object info
        QString objectName = objectDir->relativeFilePath(fileInfo.filePath()).chopped(11);


        //Sprite info
        QString spriteName = undefined;
        int originX=0;
        int originY=0;
        int imageWidth=0;
        int imageHeigth=0;

        //Find sprite name
        while(reader.readNextStartElement())
        {
            if(reader.name()==QLatin1String("spriteName")){
                spriteName = reader.readElementText();
                break;
             }

        }
        if(spriteName == QLatin1String("<undefined>") || spriteName==undefined || spriteName.isEmpty())
        {
            std::cout<<"\n"<<spriteName.toStdString();
            objectFile->close();
            delete objectFile;
            continue;
        }

        QFile *spriteFile = findSprite(spriteName,spriteDir);
        if(spriteFile==nullptr){
            objectFile->close();
            spriteFile->close();
            delete objectFile;
            delete spriteFile;
            continue;
        }

        if(!spriteFile->open(QIODevice::ReadOnly | QIODevice::Text ))
        {
            std::cout<<"\nError opening sprite file";
            spriteFile->close();
            objectFile->close();
            delete objectFile;
            delete spriteFile;
            continue;
        }
        QString imageFileDir = imageDir->relativeFilePath(spriteName.append(QLatin1String("_0.png")));

        //Get information from the sprite file(the xml reader couldn't parse the sprites for some reason so we do it manually)
        {
                spriteFile->readLine();
                QString s = QLatin1String(spriteFile->readAll());
                int arr[4];
                arr[0] = s.indexOf(QLatin1String("xorig"));
                arr[1] = s.indexOf(QLatin1String("yorigin"));
                arr[2] = s.indexOf(QLatin1String("width"));
                arr[3] = s.indexOf(QLatin1String("height"));

                for(int i=0;i<4;i++)
                {
                    bool isValue=false;
                    QString val=QLatin1String("");
                    QString sub = QString(s);
                    sub.remove(0,arr[i]);
                    for(QChar c : sub)
                    {

                        if(!isValue)
                        {
                            if(c.toLatin1()=='>')
                                isValue=true;
                        }
                        else
                        {
                            if(c.toLatin1()=='<')
                            {
                                break;
                            }
                            else
                            {
                                val.append(c);
                            }
                        }

                    }
                    arr[i]=val.toInt();
                }
                originX = arr[0];
                originY = arr[1];
                imageWidth = arr[2];
                imageHeigth = arr[3];
        }
        if(imageWidth>maxWidth)
            maxWidth=imageWidth;
        if(imageHeigth>maxHeigth)
            maxHeigth=imageHeigth;



        //Add image to the image list
        int gid = addImage(imageFileDir,imageWidth,imageHeigth,imageList);

        //Make template
        QString templatePoth=templateDir->path().append(QLatin1String("/")).append(objectName).append(QLatin1String(".tx"));

        templateDoc.reset();
        pugi::xml_node templat = templateDoc.append_child("template");

        pugi::xml_node tileset = templat.append_child("tileset");
        tileset.append_attribute("firstgid") = "0";
        tileset.append_attribute("source") = imageCollectionPath.toStdString().c_str();

        pugi::xml_node object = templat.append_child("object");
        object.append_attribute("type") = objectName.toStdString().c_str();
        object.append_attribute("gid") = QString::number(gid).toStdString().c_str();
        object.append_attribute("width") = QString::number(imageWidth).toStdString().c_str();
        object.append_attribute("height") = QString::number(imageHeigth).toStdString().c_str();

        pugi::xml_node properties = templat.append_child("properties");
        pugi::xml_node property =properties.append_child("property");
        property.append_attribute("name") = "offsetX";
        property.append_attribute("type") = "int";
        property.append_attribute("value") = QString::number(originX).toStdString().c_str();
        property = properties.append_child("property");
        property.append_attribute("name") = "originX";
        property.append_attribute("type") = "int";
        property.append_attribute("value") = QString::number(originX).toStdString().c_str();
        property = properties.append_child("property");
        property.append_attribute("name") = "offsetY";
        property.append_attribute("type") = "int";
        property.append_attribute("value") = QString::number(originY).toStdString().c_str();
        property = properties.append_child("property");
        property.append_attribute("name") = "originY";
        property.append_attribute("type") = "int";
        property.append_attribute("value") = QString::number(originY).toStdString().c_str();

        property = properties.append_child("property");
        property.append_attribute("name") = "imageWidth";
        property.append_attribute("type") = "int";
        property.append_attribute("value") = QString::number(imageWidth).toStdString().c_str();
        property = properties.append_child("property");
        property.append_attribute("name") = "imageHeight";
        property.append_attribute("type") = "int";
        property.append_attribute("value") = QString::number(imageHeigth).toStdString().c_str();

        property = properties.append_child("property");
        property.append_attribute("name") = "code";
        property.append_attribute("type") = "string";
        property.append_attribute("value") = "";


        templateDoc.save_file(templatePoth.toStdString().c_str());
        templateDoc.reset();

        pugi::xml_node objecttype = objecttypes.append_child("objecttype");
        objecttype.append_attribute("name") = objectName.toStdString().c_str();
        objecttype.append_attribute("color") = "#ffffff";

        property = objecttype.append_child("property");
        property.append_attribute("name") = "offsetX";
        property.append_attribute("type") = "int";
        property.append_attribute("default") = QString::number(originX).toStdString().c_str();
        property = objecttype.append_child("property");
        property.append_attribute("name") = "originX";
        property.append_attribute("type") = "int";
        property.append_attribute("default") = QString::number(originX).toStdString().c_str();
        property = objecttype.append_child("property");
        property.append_attribute("name") = "offsetY";
        property.append_attribute("type") = "int";
        property.append_attribute("default") = QString::number(originY).toStdString().c_str();
        property = objecttype.append_child("property");
        property.append_attribute("name") = "originY";
        property.append_attribute("type") = "int";
        property.append_attribute("default") = QString::number(originY).toStdString().c_str();

        property = objecttype.append_child("property");
        property.append_attribute("name") = "imageWidth";
        property.append_attribute("type") = "int";
        property.append_attribute("default") = QString::number(imageWidth).toStdString().c_str();
        property = objecttype.append_child("property");

        property.append_attribute("name") = "imageHeight";
        property.append_attribute("type") = "int";
        property.append_attribute("default") = QString::number(imageHeigth).toStdString().c_str();

        property = objecttype.append_child("property");
        property.append_attribute("name") = "code";
        property.append_attribute("type") = "string";
        property.append_attribute("default") = "";

        objectFile->close();
        spriteFile->close();


        delete objectFile;
        delete spriteFile;

     }

    pugi::xml_node objecttype = objecttypes.append_child("objecttype");
    objecttype.append_attribute("name") = "view";
    objecttype.append_attribute("color") = "#9c48a4";
    pugi::xml_node property = objecttype.append_child("property");
    property.append_attribute("name") = "hborder";
    property.append_attribute("type") = "int";
    property.append_attribute("default") = "9999";
    property = objecttype.append_child("property");
    property.append_attribute("name") = "hport";
    property.append_attribute("type") = "int";
    property.append_attribute("default") = "224";
    property = objecttype.append_child("property");
    property.append_attribute("name") = "hview";
    property.append_attribute("type") = "int";
    property.append_attribute("default") = "224";
    property = objecttype.append_child("property");
    property.append_attribute("name") = "vborder";
    property.append_attribute("type") = "int";
    property.append_attribute("default") = "9999";
    property = objecttype.append_child("property");
    property.append_attribute("name") = "wport";
    property.append_attribute("type") = "int";
    property.append_attribute("default") = "256";
    property = objecttype.append_child("property");
    property.append_attribute("name") = "wview";
    property.append_attribute("type") = "int";
    property.append_attribute("default") = "256";
    property = objecttype.append_child("property");
    property.append_attribute("name") = "xport";
    property.append_attribute("type") = "int";
    property.append_attribute("default") = "0";
    property = objecttype.append_child("property");
    property.append_attribute("name") = "xview";
    property.append_attribute("type") = "int";
    property.append_attribute("default") = "0";
    property = objecttype.append_child("property");
    property.append_attribute("name") = "yport";
    property.append_attribute("type") = "int";
    property.append_attribute("default") = "0";
    property = objecttype.append_child("property");
    property.append_attribute("name") = "yview";
    property.append_attribute("type") = "int";
    property.append_attribute("default") = "0";

    typesDoc.save_file(typesDir.toStdString().c_str());

    //Write image collection
    int total = imageList->size();
    writer.setDevice(imageCollection);
    writer.writeStartDocument();
    writer.writeStartElement(QLatin1String("tileset"));
    writer.writeAttribute(QLatin1String("name"),QLatin1String("images"));
    writer.writeAttribute(QLatin1String("tilewidth"),QString::number(maxWidth));
    writer.writeAttribute(QLatin1String("tileheight"),QString::number(maxHeigth));
    writer.writeAttribute(QLatin1String("tileCount"),QString::number(total));
    writer.writeAttribute(QLatin1String("columns"),QString::number(0));
    writer.writeStartElement(QLatin1String("grid"));
    writer.writeAttribute(QLatin1String("orientation"),QLatin1String("orthogonal"));
    writer.writeAttribute(QLatin1String("width"),QLatin1String("1"));
    writer.writeAttribute(QLatin1String("heigth"),QLatin1String("1"));
    writer.writeEndElement();
    for(int i=0;i<total;++i)
    {
        imageEntry* image = imageList->at(i);
        writer.writeStartElement(QLatin1String("tile"));
        writer.writeAttribute(QLatin1String("id"),QString::number(i));
        writer.writeStartElement(QLatin1String("image"));
        writer.writeAttribute(QLatin1String("width"),QString::number(image->imageWidth));
        writer.writeAttribute(QLatin1String("height"),QString::number(image->imageHeigth));
        writer.writeAttribute(QLatin1String("source"),image->rPath);
        writer.writeEndElement();
        writer.writeEndElement();
    }

    writer.writeEndElement();
    writer.writeEndDocument();

    delete imageCollection;
    delete rootDir;
    delete imageDir;
    delete spriteDir;
    delete objectDir;
    delete templateDir;
    delete imageList;
    progress->close();
}

QFile* GameMakerObjectImporter::findSprite(QString filename, QDir* dir)
{
    dir->setFilter(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    dir->setSorting(QDir::Size | QDir::Reversed);

    QFileInfoList objectFileInfo = dir->entryInfoList();
    for (int i = 0; i < objectFileInfo.size(); ++i) {
        QFileInfo fileInfo = objectFileInfo.at(i);
        if(dir->relativeFilePath(fileInfo.filePath()).chopped(11)==filename)
        {
            QString poth = fileInfo.filePath();
            return new QFile(poth);
        }
     }
    return nullptr;
}
int GameMakerObjectImporter::addImage(QString &fileDir,int width, int heigth, QVector<imageEntry*> *list)
{
    int rval = -1;
    int total = list->size();
    for(int i=0;i<total;i++)
    {
        if(list->at(i)->path == fileDir)
        {
            rval=i;
            break;
        }
    }
    if(rval==-1)
    {
        imageEntry *img = new imageEntry(fileDir,QString(QLatin1String("sprites/images/")).append(fileDir),width,heigth);
        list->append(img);
        rval = list->size()-1;
    }
    return rval;
}
