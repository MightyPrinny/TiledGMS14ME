#pragma once
#include "gamemakerobjectimporter.h"
#include <iostream>
#include <Qt>
#include <memory>
#include <fstream>
#include <sstream>
#include "rapidxml.hpp"
#include "rapidxml_print.hpp"
#include "rapidxml_utils.hpp"
#include "rapidxml_iterators.hpp"
#include <QDebug>
#include <stack>
GameMakerObjectImporter::GameMakerObjectImporter(QWidget *wd)
{
    this->prtWidget = wd;
}

void GameMakerObjectImporter::run()
{
    this->generateTemplates();
}

void GameMakerObjectImporter::generateTemplatesInThread()
{
    myThread = new GameMakerObjectImporter(prtWidget);
    connect(myThread, &GameMakerObjectImporter::finished, myThread, &GameMakerObjectImporter::finnishThread);
    myThread->start(QThread::HighPriority);
}

void GameMakerObjectImporter::finnishThread()
{
    myThread->deleteLater();
    delete myThread;

}

static rapidxml::xml_node<> *nodeAppendChild(rapidxml::xml_document<> *doc, rapidxml::xml_node<> *node, char* name, char* value = 0)
{
    rapidxml::xml_node<> *nnode = doc->allocate_node(rapidxml::node_element,name,value);
    node->append_node(nnode);
    return node;
}

static void nodeAppendAtribute(rapidxml::xml_document<> *doc, rapidxml::xml_node<>* node, char* name, char* value=0)
{
    node->append_attribute(doc->allocate_attribute(name,value));
}

static rapidxml::xml_node<> *nodeAppendChild(rapidxml::xml_document<> *doc, rapidxml::xml_node<> *node,const char* name,const char* value = 0)
{
    rapidxml::xml_node<> *nnode = doc->allocate_node(rapidxml::node_element,name,value);
    node->append_node(nnode);
    return node;
}

static void nodeAppendAtribute(rapidxml::xml_document<> *doc, rapidxml::xml_node<>* node,const char* name,const char* value=0)
{
    node->append_attribute(doc->allocate_attribute(name,value));
}

static QString str(char* chars)
{
    return QString(QLatin1String(chars));
}

QString GameMakerObjectImporter::getNodePath(rapidxml::xml_node<>* node)
{
    QString path = str("");
    using namespace std;
    using namespace rapidxml;
    xml_node<>* itNode = node->parent();
    if(itNode->parent())
    {
        stack<QString> pathStack;
        while(itNode->parent() && str(itNode->parent()->name())==str("objects"))
        {

            pathStack.push(str("/").append(str(itNode->first_attribute("name")->value())));
            itNode = itNode->parent();
        }
        while(!pathStack.empty())
        {
            path.append(pathStack.top());
            pathStack.pop();
        }
    }
    return path;
}

void GameMakerObjectImporter::mapChilds(rapidxml::xml_node<>* node, std::unordered_map<std::string,std::string> *objectFolderMap)
{
    using namespace rapidxml;
    using namespace std;
    xml_node<>* itNode = node->first_node("object");
    while(itNode)
    {
        //Map childs
        string path = getNodePath(itNode).toStdString();
        QString objName = str(itNode->value());
        objName.remove(str("objects\\"));
        string objectName = objName.toStdString();
        objectFolderMap->emplace(make_pair(objectName,path));
        itNode = itNode->next_sibling("object");
    }
    itNode = node->first_node("objects");
    while(itNode)
    {
        mapChilds(itNode,objectFolderMap);
        itNode=itNode->next_sibling("objects");
    }
}


void GameMakerObjectImporter::mapObjectsToFolders(QString &projectFilePath, std::unordered_map<std::string,std::string> *objectFolderMap)
{
    using namespace rapidxml;
    using namespace std;
    xml_document<> doc;
    ifstream file(projectFilePath.toStdString().c_str());
    stringstream buffer;
    buffer << file.rdbuf();
    string content(buffer.str());
    doc.parse<0>(&content[0]);

    xml_node<> *root = doc.first_node("assets")->first_node("objects");

    mapChilds(root,objectFolderMap);

    file.close();
    doc.clear();

}

void GameMakerObjectImporter::generateTemplates()
{
    QString dir = QFileDialog::getExistingDirectory(prtWidget, QLatin1String("Open a Directory with the Objects and Sprites folders from the game maker project"),
                                                      QLatin1String(""),
                                                      QFileDialog::ShowDirsOnly
                                                      | QFileDialog::DontUseCustomDirectoryIcons);
    if(dir.isEmpty())
    {
        printf("empty dir");
        return;
    }


    using namespace rapidxml;
    using namespace std;

    //Directories
    QDir rootDir =  QDir(dir);
    rootDir.cd(QLatin1String("objects"));
    QDir objectDir =  QDir(rootDir.path());
    rootDir.cdUp();
    rootDir.cd(QLatin1String("sprites"));
    QDir spriteDir =  QDir(rootDir.path());
    rootDir.cd(QLatin1String("images"));
    QDir imageDir =  QDir(rootDir.path());
    rootDir.cdUp();
    rootDir.cdUp();
    rootDir.mkdir(QLatin1String("templates"));
    rootDir.cd(QLatin1String("templates"));

    QDir templateDir = QDir(rootDir.path());
    rootDir.cdUp();

    objectDir.setFilter(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    objectDir.setSorting(QDir::Size | QDir::Reversed);

    //File info
    QFileInfoList objectFileInfo = objectDir.entryInfoList();
    int totalObjects =objectFileInfo.size();

    //Progress Bar
    QProgressDialog *progress = new QProgressDialog(prtWidget);
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setLabelText(QLatin1String(""));
    progress->setCancelButtonText(QLatin1String("Abort"));
    progress->setMinimum(0);
    progress->setMaximum(totalObjects);
    progress->setWindowFlags(Qt::WindowMinMaxButtonsHint);
    progress->show();

    QString projectFilePath = str("");
    unordered_map<string,string> *objectFolderMap = new unordered_map<string,string>();
    unordered_map<string,int> *imageIDMap = new unordered_map<string,int>();
    bool useObjectFolders = false;

    {
        QFileInfoList rootInfo = rootDir.entryInfoList();
        int size = rootInfo.size();
        for(int a = 0; a < size; ++a)
        {
            QFileInfo finfo = rootInfo.at(a);
            if(finfo.isFile())
            {
                if(finfo.fileName().endsWith(str(".project.gmx")))
                {
                    projectFilePath = finfo.filePath();
                    useObjectFolders = true;
                    break;
                }
            }
        }
    }
    if(useObjectFolders)
    {
        mapObjectsToFolders(projectFilePath,objectFolderMap);
    }

    QString undefined = QLatin1String("&lt;undefined&gt;");

    QFile *imageCollection = new QFile(rootDir.path().append(QLatin1String("/images.tsx")));
    imageCollection->open(QIODevice::WriteOnly | QIODevice::Text);
    imageCollection->flush();

    QString typesDir = rootDir.path().append(QLatin1String("/types.xml"));
    QString imageCollectionPath = templateDir.relativeFilePath(rootDir.path().append(QLatin1String("/images.tsx")));

    QVector<imageEntry*>* imageList = new QVector<imageEntry*>();
    imageList->reserve(totalObjects);
    int maxWidth = 0;
    int maxHeigth = 0;


    QFile *typesFile = new QFile(typesDir);
    if(!typesFile->open(QIODevice::WriteOnly | QIODevice::Text))
    {
        delete typesFile;
        delete imageList;
        delete imageCollection;
        return;
    }

    QXmlStreamWriter typesWriter;
    QXmlStreamWriter templateWriter;
    templateWriter.setAutoFormatting(true);
    typesWriter.setAutoFormatting(true);
    typesWriter.setDevice(typesFile);
    typesWriter.writeStartDocument();
    typesWriter.writeStartElement(str("objecttypes"));


    //Loop through object files
    for (int i = 0; i < totalObjects; ++i) {
        progress->setValue(i);
        if(progress->wasCanceled())
            break;

        QFileInfo fileInfo = objectFileInfo.at(i);


        xml_document<> auxDoc;
        ifstream theFile(fileInfo.filePath().toStdString().c_str());
        vector<char> buffer((istreambuf_iterator<char>(theFile)), istreambuf_iterator<char>());
        buffer.push_back('\0');

        try {
            auxDoc.parse<0>(&buffer[0]);
        } catch (parse_error e) {
            qDebug()<<"error parsing object file";
            continue;
        }


        if(!auxDoc.first_node("object"))
            qDebug()<<"critical error";
        xml_node<> *auxNode = auxDoc.first_node("object")->first_node("spriteName");
        //Object info
        QString objectName = fileInfo.baseName();//objectDir.relativeFilePath(fileInfo.filePath()).chopped(11);

        //Sprite info
        QString spriteName = undefined;

        if(auxNode)
        {
            spriteName = QString(QLatin1String(auxNode->value()));

        }
        int originX=0;
        int originY=0;
        int imageWidth=1;

        int imageHeigth=1;

        if(spriteName == QLatin1String("<undefined>") || spriteName==undefined || spriteName.isEmpty())
        {
            qDebug()<<"Object doesn't have a sprite, skipping...";
            auxDoc.clear();
            continue;
        }
        QFile *spriteFile = findSprite(spriteName,&spriteDir);

        if(spriteFile==nullptr){
            continue;
        }
        QString spr = QString(spriteName);

        QString imageFileDir = imageDir.relativeFilePath(spriteName.append(QLatin1String("_0.png")));
        theFile.close();
        auxDoc.clear();
        theFile = ifstream(spriteFile->fileName().toStdString().c_str());

        buffer = vector<char>((istreambuf_iterator<char>(theFile)), istreambuf_iterator<char>());
        buffer.push_back('\0');
        try {
            auxDoc.parse<0>(&buffer[0]);
        } catch (parse_error e) {
            qDebug()<<"Error parsing sprite file";
            delete spriteFile;
            continue;
        }

        {
            xml_node<> *spriteNode = auxDoc.first_node("sprite");
            if(spriteNode)
            {
                auxNode = spriteNode->first_node("xorig");
                if(auxNode)
                    originX = QString(QLatin1String(auxNode->value())).toInt();
                auxNode = spriteNode->first_node("yorigin");
                if(auxNode)
                    originY = QString(QLatin1String(auxNode->value())).toInt();
                auxNode = spriteNode->first_node("width");
                if(auxNode)
                    imageWidth = QString(QLatin1String(auxNode->value())).toInt();
                auxNode = spriteNode->first_node("height");
                if(auxNode)
                    imageHeigth = QString(QLatin1String(auxNode->value())).toInt();
            }

        }

        if(imageWidth>maxWidth)
            maxWidth=imageWidth;
        if(imageHeigth>maxHeigth)
            maxHeigth=imageHeigth;

        //Add image to the image list
        int gid = addImage(spr,imageFileDir,imageWidth,imageHeigth,imageList,imageIDMap);

        //Make template
        QString subFolders = str("");
        if(useObjectFolders)
        {
            unordered_map<string,string>::iterator it;
            it = objectFolderMap->find(objectName.toStdString());
            if(it!=objectFolderMap->end())
            {
                subFolders = str((char*)it->second.c_str());

            }
        }
        QString templatePoth = templateDir.path().append(subFolders);

        //QString templatePoth=templateDir.path().append(QLatin1String("/")).append(objectName).append(QLatin1String(".tx"));
        imageCollectionPath = QDir(templatePoth).relativeFilePath(rootDir.path().append(QLatin1String("/images.tsx")));
        templatePoth = templatePoth.append(str("/").append(objectName).append(str(".tx")));
        auxDoc.clear();
        templateDir.mkpath(QFileInfo(templatePoth).absolutePath());
        QFile templateFile(templatePoth);
        if(true)
        {

            if(!templateFile.open(QIODevice::WriteOnly | QIODevice::Text ))
            {
                qDebug()<<"\nError creating template file";
                delete spriteFile;
                continue;
            }
            templateWriter.setDevice(&templateFile);
            templateWriter.writeStartDocument();

            templateWriter.writeStartElement(str("template"));
            templateWriter.writeStartElement(str("tileset"));
            templateWriter.writeAttribute(str("firstgid"),str("0"));
            templateWriter.writeAttribute(str("source"),imageCollectionPath);
            templateWriter.writeEndElement();

            templateWriter.writeStartElement(str("object"));
            templateWriter.writeAttribute(str("type"),objectName);
            templateWriter.writeAttribute(str("gid"),QString::number(gid));
            templateWriter.writeAttribute(str("width"),QString::number(imageWidth));
            templateWriter.writeAttribute(str("height"),QString::number(imageHeigth));
            templateWriter.writeEndElement();

            templateWriter.writeStartElement(str("properties"));

            templateWriter.writeStartElement(str("property"));
            templateWriter.writeAttribute(str("name"),str("offsetX"));
            templateWriter.writeAttribute(str("type"),str("int"));
            templateWriter.writeAttribute(str("value"),QString::number(originX));
            templateWriter.writeEndElement();

            templateWriter.writeStartElement(str("property"));
            templateWriter.writeAttribute(str("name"),str("originX"));
            templateWriter.writeAttribute(str("type"),str("int"));
            templateWriter.writeAttribute(str("value"),QString::number(originX));
            templateWriter.writeEndElement();

            templateWriter.writeStartElement(str("property"));
            templateWriter.writeAttribute(str("name"),str("offsetY"));
            templateWriter.writeAttribute(str("type"),str("int"));
            templateWriter.writeAttribute(str("value"),QString::number(originY));
            templateWriter.writeEndElement();

            templateWriter.writeStartElement(str("property"));
            templateWriter.writeAttribute(str("name"),str("originY"));
            templateWriter.writeAttribute(str("type"),str("int"));
            templateWriter.writeAttribute(str("value"),QString::number(originY));
            templateWriter.writeEndElement();

            templateWriter.writeStartElement(str("property"));
            templateWriter.writeAttribute(str("name"),str("imageWidth"));
            templateWriter.writeAttribute(str("type"),str("int"));
            templateWriter.writeAttribute(str("value"),QString::number(imageWidth));
            templateWriter.writeEndElement();

            templateWriter.writeStartElement(str("property"));
            templateWriter.writeAttribute(str("name"),str("imageHeight"));
            templateWriter.writeAttribute(str("type"),str("int"));
            templateWriter.writeAttribute(str("value"),QString::number(imageHeigth));
            templateWriter.writeEndElement();

            templateWriter.writeStartElement(str("property"));
            templateWriter.writeAttribute(str("name"),str("code"));
            templateWriter.writeAttribute(str("type"),str("string"));
            templateWriter.writeAttribute(str("value"),str(""));
            templateWriter.writeEndElement();

            templateWriter.writeEndElement();
            templateWriter.writeEndDocument();

            templateFile.close();
        }

        typesWriter.writeStartElement(str("objecttype"));
        typesWriter.writeAttribute(str("name"),objectName);
        typesWriter.writeAttribute(str("color"),str("#ffffff"));

        typesWriter.writeStartElement(str("property"));
        typesWriter.writeAttribute(str("name"),str("offsetX"));
        typesWriter.writeAttribute(str("type"),str("int"));
        typesWriter.writeAttribute(str("default"),QString::number(originX));
        typesWriter.writeEndElement();

        typesWriter.writeStartElement(str("property"));
        typesWriter.writeAttribute(str("name"),str("originX"));
        typesWriter.writeAttribute(str("type"),str("int"));
        typesWriter.writeAttribute(str("default"),QString::number(originX));
        typesWriter.writeEndElement();

        typesWriter.writeStartElement(str("property"));
        typesWriter.writeAttribute(str("name"),str("offsetY"));
        typesWriter.writeAttribute(str("type"),str("int"));
        typesWriter.writeAttribute(str("default"),QString::number(originY));
        typesWriter.writeEndElement();

        typesWriter.writeStartElement(str("property"));
        typesWriter.writeAttribute(str("name"),str("originY"));
        typesWriter.writeAttribute(str("type"),str("int"));
        typesWriter.writeAttribute(str("default"),QString::number(originY));
        typesWriter.writeEndElement();

        typesWriter.writeStartElement(str("property"));
        typesWriter.writeAttribute(str("name"),str("imageWidth"));
        typesWriter.writeAttribute(str("type"),str("int"));
        typesWriter.writeAttribute(str("default"),QString::number(imageWidth));
        typesWriter.writeEndElement();

        typesWriter.writeStartElement(str("property"));
        typesWriter.writeAttribute(str("name"),str("imageHeight"));
        typesWriter.writeAttribute(str("type"),str("int"));
        typesWriter.writeAttribute(str("default"),QString::number(imageHeigth));
        typesWriter.writeEndElement();

        typesWriter.writeStartElement(str("property"));
        typesWriter.writeAttribute(str("name"),str("code"));
        typesWriter.writeAttribute(str("type"),str("string"));
        typesWriter.writeAttribute(str("default"),str(""));
        typesWriter.writeEndElement();

        typesWriter.writeEndElement();

        delete spriteFile;
     }


    typesWriter.writeStartElement(str("objecttype"));
    typesWriter.writeAttribute(str("name"),str("view"));
    typesWriter.writeAttribute(str("color"),str("#9c48a4"));

    typesWriter.writeStartElement(str("property"));
    typesWriter.writeAttribute(str("name"),str("hborder"));
    typesWriter.writeAttribute(str("type"),str("int"));
    typesWriter.writeAttribute(str("default"),str("9999"));
    typesWriter.writeEndElement();

    typesWriter.writeStartElement(str("property"));
    typesWriter.writeAttribute(str("name"),str("hport"));
    typesWriter.writeAttribute(str("type"),str("int"));
    typesWriter.writeAttribute(str("default"),str("224"));
    typesWriter.writeEndElement();

    typesWriter.writeStartElement(str("property"));
    typesWriter.writeAttribute(str("name"),str("hview"));
    typesWriter.writeAttribute(str("type"),str("int"));
    typesWriter.writeAttribute(str("default"),str("224"));
    typesWriter.writeEndElement();

    typesWriter.writeStartElement(str("property"));
    typesWriter.writeAttribute(str("name"),str("vborder"));
    typesWriter.writeAttribute(str("type"),str("int"));
    typesWriter.writeAttribute(str("default"),str("9999"));
    typesWriter.writeEndElement();

    typesWriter.writeStartElement(str("property"));
    typesWriter.writeAttribute(str("name"),str("wport"));
    typesWriter.writeAttribute(str("type"),str("int"));
    typesWriter.writeAttribute(str("default"),str("256"));
    typesWriter.writeEndElement();

    typesWriter.writeStartElement(str("property"));
    typesWriter.writeAttribute(str("name"),str("wview"));
    typesWriter.writeAttribute(str("type"),str("int"));
    typesWriter.writeAttribute(str("default"),str("256"));
    typesWriter.writeEndElement();

    typesWriter.writeStartElement(str("property"));
    typesWriter.writeAttribute(str("name"),str("xport"));
    typesWriter.writeAttribute(str("type"),str("int"));
    typesWriter.writeAttribute(str("default"),str("0"));
    typesWriter.writeEndElement();

    typesWriter.writeStartElement(str("property"));
    typesWriter.writeAttribute(str("name"),str("xview"));
    typesWriter.writeAttribute(str("type"),str("int"));
    typesWriter.writeAttribute(str("default"),str("0"));
    typesWriter.writeEndElement();

    typesWriter.writeStartElement(str("property"));
    typesWriter.writeAttribute(str("name"),str("yport"));
    typesWriter.writeAttribute(str("type"),str("int"));
    typesWriter.writeAttribute(str("default"),str("0"));
    typesWriter.writeEndElement();

    typesWriter.writeStartElement(str("property"));
    typesWriter.writeAttribute(str("name"),str("yview"));
    typesWriter.writeAttribute(str("type"),str("int"));
    typesWriter.writeAttribute(str("default"),str("0"));
    typesWriter.writeEndElement();

    typesWriter.writeEndElement();

    typesWriter.writeEndElement();
    typesWriter.writeEndDocument();

    typesFile->close();

    //Write image collection
    int total = imageList->size();

    typesWriter.setDevice(imageCollection);
    typesWriter.writeStartDocument();
    typesWriter.writeStartElement(QLatin1String("tileset"));
    typesWriter.writeAttribute(QLatin1String("name"),QLatin1String("images"));
    typesWriter.writeAttribute(QLatin1String("tilewidth"),QString::number(maxWidth));
    typesWriter.writeAttribute(QLatin1String("tileheight"),QString::number(maxHeigth));
    typesWriter.writeAttribute(QLatin1String("tileCount"),QString::number(total));
    typesWriter.writeAttribute(QLatin1String("columns"),QString::number(0));
    typesWriter.writeStartElement(QLatin1String("grid"));
    typesWriter.writeAttribute(QLatin1String("orientation"),QLatin1String("orthogonal"));
    typesWriter.writeAttribute(QLatin1String("width"),QLatin1String("1"));
    typesWriter.writeAttribute(QLatin1String("heigth"),QLatin1String("1"));
    typesWriter.writeEndElement();
    for(int i=0;i<total;++i)
    {
        imageEntry* image = imageList->at(i);
        typesWriter.writeStartElement(QLatin1String("tile"));
        typesWriter.writeAttribute(QLatin1String("id"),QString::number(i));
        typesWriter.writeStartElement(QLatin1String("image"));
        typesWriter.writeAttribute(QLatin1String("width"),QString::number(image->imageWidth));
        typesWriter.writeAttribute(QLatin1String("height"),QString::number(image->imageHeigth));
        typesWriter.writeAttribute(QLatin1String("source"),image->rPath);
        typesWriter.writeEndElement();
        typesWriter.writeEndElement();
    }

    typesWriter.writeEndElement();
    typesWriter.writeEndDocument();

    imageCollection->close();

    delete imageCollection;
    delete typesFile;
    delete imageList;
    delete objectFolderMap;
    delete imageIDMap;
    progress->close();
    delete progress;

}

QFile* GameMakerObjectImporter::findSprite(QString filename, QDir* dir)
{
    QString fname = filename.append(QLatin1String(".sprite.gmx"));
    QFile * newFile = new QFile(dir->absoluteFilePath(fname));
    if(newFile->exists())
        return newFile;
    else
    {
        delete newFile;
    }

    return nullptr;
}
int GameMakerObjectImporter::addImage(QString &filename,QString &fileDir,int width, int heigth, QVector<imageEntry*> *list, std::unordered_map<std::string,int> *idmap)
{
    using namespace std;
    int rval = -1;
    int total = list->size();
    auto find = idmap->find(filename.toStdString());
    if(find==idmap->end())
    {
        imageEntry *img = new imageEntry(fileDir,QString(QLatin1String("sprites/images/")).append(fileDir),width,heigth);
        list->append(img);
        rval = list->size()-1;
        idmap->emplace(make_pair(filename.toStdString(),rval));
    }
    else
    {
        rval = find->second;
    }
    return rval;
}
