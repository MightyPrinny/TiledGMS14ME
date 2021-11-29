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
#include <QCoreApplication>

GameMakerObjectImporter::GameMakerObjectImporter(QWidget *wd)
{
    this->prtWidget = wd;
}

void GameMakerObjectImporter::run()
{
	this->showGenerateTemplatesDialog(prtWidget);
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
	QString path = QStringLiteral("");
    using namespace std;
    using namespace rapidxml;
    xml_node<>* itNode = node->parent();
    if(itNode->parent())
    {
        stack<QString> pathStack;
		while(itNode->parent() && str(itNode->parent()->name())==QStringLiteral("objects"))
        {

			pathStack.push( QStringLiteral("/").append(str(itNode->first_attribute("name")->value())));
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
		objName.remove(QStringLiteral("objects\\"));
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

void GameMakerObjectImporter::showGenerateTemplatesDialog(QWidget* prt)
{
	auto diag = new MMGenerateTemplatesDialog(prt);
	diag->importer = this;
	prtWidget = diag;
	//diag->setWindowModality(Qt::WindowModal);
	//diag->setModal(true);
	diag->exec();
	delete diag;
}

void GameMakerObjectImporter::generateTemplates(QString dir, QString outputDirPath)
{
	if(dir.isEmpty() || outputDirPath.isEmpty())
    {
		printf("Generate Templates: a path was empty");
        return;
    }


    using namespace rapidxml;
    using namespace std;
	bool valid = true;
    //Directories
    QDir rootDir =  QDir(dir);
	valid &= rootDir.cd(QStringLiteral("objects"));
    QDir objectDir =  QDir(rootDir.path());
	rootDir.cdUp();
	valid &= rootDir.cd(QStringLiteral("sprites"));
    QDir spriteDir =  QDir(rootDir.path());
	valid &= rootDir.cd(QStringLiteral("images"));
    QDir imageDir =  QDir(rootDir.path());
    rootDir.cdUp();
    rootDir.cdUp();

	QDir outputDir = QDir(outputDirPath);
	outputDir .mkdir(QStringLiteral("templates"));
	valid &= outputDir .cd(QStringLiteral("templates"));
	QDir templateDir = QDir(outputDir.path());
	outputDir.cdUp();

	if(!valid)
	{
		qDebug() << "Invalid directory";
		return;
	}
	if(!outputDir.exists(QStringLiteral("gmDefaultImage.png")))
	{
		qDebug() << "No default image file at " << outputDir.filePath(QStringLiteral("gmDefaultImage.png"));
		QFile::copy(QStringLiteral(":/GMTemplateGeneration/DefaultTemplate.png"), outputDir.filePath(QStringLiteral("gmDefaultImage.png")));
	}

    objectDir.setFilter(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    objectDir.setSorting(QDir::Size | QDir::Reversed);

    //File info
    QFileInfoList objectFileInfo = objectDir.entryInfoList();
    int totalObjects =objectFileInfo.size();

    //Progress Bar
	QProgressDialog progress(nullptr);

	progress.setLabelText(QStringLiteral("Generating Templates"));
	progress.setCancelButtonText(QStringLiteral("Abort"));
	progress.setMinimum(0);
	progress.setMaximum(totalObjects);
	progress.setWindowFlags(Qt::WindowMinMaxButtonsHint);
	progress.setWindowModality(Qt::ApplicationModal);
	progress.show();
	progress.setValue(0);
	QCoreApplication::processEvents();
	//progress->repaint();

	QString projectFilePath = QStringLiteral("");
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
				if(finfo.fileName().endsWith(QStringLiteral(".project.gmx")))
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

	QString undefined = QStringLiteral("&lt;undefined&gt;");

	QFile *imageCollection = new QFile(outputDir.path().append(QStringLiteral("/images.tsx")));
    imageCollection->open(QIODevice::WriteOnly | QIODevice::Text);
    imageCollection->flush();

	QString typesDir = outputDir.path().append(QStringLiteral("/types.xml"));
	QString imageCollectionPath = templateDir.relativeFilePath(outputDir.path().append(QStringLiteral("/images.tsx")));

    QVector<imageEntry*>* imageList = new QVector<imageEntry*>();
	imageList->reserve(totalObjects+1);

	int defaultImageId = -1;
	if(outputDir.exists(QStringLiteral("gmDefaultImage.png")))
	{
		auto defaultImgName = QStringLiteral("gmDefaultImage.png");
		auto defaultImgPath = outputDir.relativeFilePath(QStringLiteral("gmDefaultImage.png"));
		defaultImageId = addImage(defaultImgName,
								  defaultImgPath, 8, 8, imageList, imageIDMap);
	}

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
	typesWriter.writeStartElement(QStringLiteral("objecttypes"));


    //Loop through object files
    for (int i = 0; i < totalObjects; ++i) {
		if((i%32) == 0)
		{
			progress.setValue(i);
		}
		if((i%64) == 0)
		{
			QCoreApplication::processEvents();
		}
		if(progress.wasCanceled())
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


		if(!auxDoc.first_node("object")) {

            qDebug()<<"critical error";
			break;
		}
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
		int gid;
		if(spriteName == QStringLiteral("<undefined>") || spriteName==undefined || spriteName.isEmpty())
        {
			//qDebug()<<"Object doesn't have a sprite, skipping...";
			if(defaultImageId < 1)
			{
				auxDoc.clear();
				continue;
			}
			gid = defaultImageId;
			imageWidth = 8;
			imageHeigth = 8;
        }
		else
		{
			QFile *spriteFile = findSprite(spriteName,&spriteDir);

			if(spriteFile==nullptr){
				continue;
			}
			QString spr = QString(spriteName);

			QString imageFilePath = outputDir.relativeFilePath( imageDir.filePath(spriteName.append(QStringLiteral("_0.png"))));
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
			gid = addImage(spr,imageFilePath,imageWidth,imageHeigth,imageList,imageIDMap);

			delete spriteFile;
		}


        //Make template
		QString subFolders = QStringLiteral("");
        if(useObjectFolders)
        {
            unordered_map<string,string>::iterator it;
            it = objectFolderMap->find(objectName.toStdString());
            if(it!=objectFolderMap->end())
            {
				subFolders = str((char*)it->second.c_str());

            }
        }
		QString templatePath = templateDir.path().append(subFolders);

        //QString templatePoth=templateDir.path().append(QLatin1String("/")).append(objectName).append(QLatin1String(".tx"));
		imageCollectionPath = QDir(templatePath).relativeFilePath(outputDir.filePath(QStringLiteral("images.tsx")));
		templatePath = templatePath.append(QStringLiteral("/").append(objectName).append(QStringLiteral(".tx")));
        auxDoc.clear();
		templateDir.mkpath(QFileInfo(templatePath).absolutePath());
		QFile templateFile(templatePath);
        if(true)
        {

            if(!templateFile.open(QIODevice::WriteOnly | QIODevice::Text ))
            {
                qDebug()<<"\nError creating template file";
                continue;
            }
            templateWriter.setDevice(&templateFile);
            templateWriter.writeStartDocument();

			templateWriter.writeStartElement(QStringLiteral("template"));
				templateWriter.writeStartElement(QStringLiteral("tileset"));
				templateWriter.writeAttribute(QStringLiteral("firstgid"),QStringLiteral("0"));
				templateWriter.writeAttribute(QStringLiteral("source"),imageCollectionPath);
            templateWriter.writeEndElement();

			templateWriter.writeStartElement(QStringLiteral("object"));
				templateWriter.writeAttribute(QStringLiteral("type"),objectName);
				templateWriter.writeAttribute(QStringLiteral("gid"),QString::number(gid));
				templateWriter.writeAttribute(QStringLiteral("width"),QString::number(imageWidth));
				templateWriter.writeAttribute(QStringLiteral("height"),QString::number(imageHeigth));


				templateWriter.writeStartElement(QStringLiteral("properties"));

					templateWriter.writeStartElement(QStringLiteral("property"));
						templateWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("offsetX"));
						templateWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
						templateWriter.writeAttribute(QStringLiteral("value"),QString::number(originX));
					templateWriter.writeEndElement();

					templateWriter.writeStartElement(QStringLiteral("property"));
						templateWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("originX"));
						templateWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
						templateWriter.writeAttribute(QStringLiteral("value"),QString::number(originX));
					templateWriter.writeEndElement();

					templateWriter.writeStartElement(QStringLiteral("property"));
						templateWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("offsetY"));
						templateWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
						templateWriter.writeAttribute(QStringLiteral("value"),QString::number(originY));
					templateWriter.writeEndElement();

					templateWriter.writeStartElement(QStringLiteral("property"));
						templateWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("originY"));
						templateWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
						templateWriter.writeAttribute(QStringLiteral("value"),QString::number(originY));
					templateWriter.writeEndElement();

					templateWriter.writeStartElement(QStringLiteral("property"));
						templateWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("imageWidth"));
						templateWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
						templateWriter.writeAttribute(QStringLiteral("value"),QString::number(imageWidth));
					templateWriter.writeEndElement();

					templateWriter.writeStartElement(QStringLiteral("property"));
						templateWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("imageHeight"));
						templateWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
						templateWriter.writeAttribute(QStringLiteral("value"),QString::number(imageHeigth));
					templateWriter.writeEndElement();

					templateWriter.writeStartElement(QStringLiteral("property"));
						templateWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("code"));
						templateWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("string"));
						templateWriter.writeAttribute(QStringLiteral("value"),QStringLiteral(""));
					templateWriter.writeEndElement();

				templateWriter.writeEndElement();



			templateWriter.writeEndElement();

            templateWriter.writeEndDocument();

            templateFile.close();
        }

		typesWriter.writeStartElement(QStringLiteral("objecttype"));
			typesWriter.writeAttribute(QStringLiteral("name"),objectName);
			typesWriter.writeAttribute(QStringLiteral("color"),QStringLiteral("#ffffff"));

			typesWriter.writeStartElement(QStringLiteral("property"));
				typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("offsetX"));
				typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
				typesWriter.writeAttribute(QStringLiteral("default"),QString::number(originX));
			typesWriter.writeEndElement();

			typesWriter.writeStartElement(QStringLiteral("property"));
				typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("originX"));
				typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
				typesWriter.writeAttribute(QStringLiteral("default"),QString::number(originX));
			typesWriter.writeEndElement();

			typesWriter.writeStartElement(QStringLiteral("property"));
				typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("offsetY"));
				typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
				typesWriter.writeAttribute(QStringLiteral("default"),QString::number(originY));
			typesWriter.writeEndElement();

			typesWriter.writeStartElement(QStringLiteral("property"));
				typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("originY"));
				typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
				typesWriter.writeAttribute(QStringLiteral("default"),QString::number(originY));
			typesWriter.writeEndElement();

			typesWriter.writeStartElement(QStringLiteral("property"));
				typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("imageWidth"));
				typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
				typesWriter.writeAttribute(QStringLiteral("default"),QString::number(imageWidth));
			typesWriter.writeEndElement();

			typesWriter.writeStartElement(QStringLiteral("property"));
				typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("imageHeight"));
				typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
				typesWriter.writeAttribute(QStringLiteral("default"),QString::number(imageHeigth));
			typesWriter.writeEndElement();

			typesWriter.writeStartElement(QStringLiteral("property"));
				typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("code"));
				typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("string"));
				typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral(""));
			typesWriter.writeEndElement();

        typesWriter.writeEndElement();

     }

	//DEFAULT TYPES

	//ROOM VIEW
	typesWriter.writeStartElement(QStringLiteral("objecttype"));
		typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("t_gmRoomView"));
		typesWriter.writeAttribute(QStringLiteral("color"),QStringLiteral("#9c48a4"));

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("viewId"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("0"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("visible"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("bool"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("false"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("xview"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("0"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("yview"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("0"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("wview"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("640"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("hview"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("480"));
		typesWriter.writeEndElement();


		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("xport"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("0"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("yport"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("0"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("wport"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("640"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("hport"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("480"));
		typesWriter.writeEndElement();


		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("hborder"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("32"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("vborder"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("32"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("vborder"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("32"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("hspeed"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("float"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("-1"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("vspeed"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("float"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("-1"));
		typesWriter.writeEndElement();

	typesWriter.writeEndElement();//ROOM VIEW

	//ROOM BACKGROUND
	typesWriter.writeStartElement(QStringLiteral("objecttype"));
		typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("t_gmRoomBackground"));
		typesWriter.writeAttribute(QStringLiteral("color"),QStringLiteral("#0048a4"));

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("bgId"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("0"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("visible"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("bool"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("false"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("foreground"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("bool"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("false"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("name"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("string"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral(""));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("x"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("0"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("y"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("int"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("0"));
		typesWriter.writeEndElement();


		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("htiled"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("bool"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("true"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("vtiled"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("bool"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("true"));
		typesWriter.writeEndElement();


		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("hspeed"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("float"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("0"));
		typesWriter.writeEndElement();

		typesWriter.writeStartElement(QStringLiteral("property"));
			typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("vspeed"));
			typesWriter.writeAttribute(QStringLiteral("type"),QStringLiteral("float"));
			typesWriter.writeAttribute(QStringLiteral("default"),QStringLiteral("0"));
		typesWriter.writeEndElement();

	typesWriter.writeEndElement();//ROOM BACKGROUND



    typesWriter.writeEndElement();
    typesWriter.writeEndDocument();

    typesFile->close();

    //Write image collection
    int total = imageList->size();

    typesWriter.setDevice(imageCollection);
    typesWriter.writeStartDocument();
	typesWriter.writeStartElement(QStringLiteral("tileset"));
	typesWriter.writeAttribute(QStringLiteral("name"),QStringLiteral("images"));
	typesWriter.writeAttribute(QStringLiteral("tilewidth"),QString::number(maxWidth));
	typesWriter.writeAttribute(QStringLiteral("tileheight"),QString::number(maxHeigth));
	typesWriter.writeAttribute(QStringLiteral("tileCount"),QString::number(total));
	typesWriter.writeAttribute(QStringLiteral("columns"),QString::number(0));
	typesWriter.writeStartElement(QStringLiteral("grid"));
	typesWriter.writeAttribute(QStringLiteral("orientation"),QStringLiteral("orthogonal"));
	typesWriter.writeAttribute(QStringLiteral("width"),QStringLiteral("1"));
	typesWriter.writeAttribute(QStringLiteral("heigth"),QStringLiteral("1"));
    typesWriter.writeEndElement();
	for(int i=0;i<total;++i)
    {
		imageEntry* image = imageList->at(i);
		typesWriter.writeStartElement(QStringLiteral("tile"));
		typesWriter.writeAttribute(QStringLiteral("id"),QString::number(i+1));
		typesWriter.writeStartElement(QStringLiteral("image"));
		typesWriter.writeAttribute(QStringLiteral("width"),QString::number(image->imageWidth));
		typesWriter.writeAttribute(QStringLiteral("height"),QString::number(image->imageHeigth));
		typesWriter.writeAttribute(QStringLiteral("source"),image->rPath);
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
	progress.close();

}

QFile* GameMakerObjectImporter::findSprite(QString filename, QDir* dir)
{
	QString fname = filename.append(QStringLiteral(".sprite.gmx"));
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
	//int total = list->size();
    auto find = idmap->find(filename.toStdString());
    if(find==idmap->end())
    {
		imageEntry *img = new imageEntry(fileDir, fileDir,width,heigth);
        list->append(img);
		rval = list->size();
        idmap->emplace(make_pair(filename.toStdString(),rval));
    }
    else
    {
        rval = find->second;
    }
    return rval;
}
