/*
 * GMX Tiled Plugin
 * Copyright 2016, Jones Blunt <mrjonesblunt@gmail.com>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "gmxplugin.h"

#include "map.h"
#include "savefile.h"
#include "tile.h"
#include "tilelayer.h"
#include "mapobject.h"
#include "objecttemplate.h"
#include "objecttemplateformat.h"
#include "tilesetmanager.h"
#include "templatemanager.h"
#include "objectgroup.h"
#include <QDebug>
#include <QDir>
#include <QBuffer>
#include <QFileInfo>
#include <QDirIterator>
#include "layer.h"

#include <QRegularExpression>
#include <QXmlStreamWriter>
#include <sstream>
#include <iostream>
#include <iomanip>
#include "qtcompat_p.h"
#include <vector>
#include <QTextCodec>
#include <QTextDocument>
#include "rapidxml.hpp"
#include "rapidxml_print.hpp"
#include "rapidxml_utils.hpp"
#include <fstream>
#include <QFileDialog>
#include <string.h>
#include <unordered_map>
#include <qmath.h>
#include <stdlib.h>
#include <QStringBuilder>

#include "roomimporterdialog.h"

using namespace Tiled;
using namespace Gmx;

template <typename T>
static T optionalProperty(const Object *object, const QString &name, const T &def)
{
    QVariant var = object->inheritedProperty(name);
    return var.isValid() ? var.value<T>() : def;
}

template <typename T>
static QString toString(T number)
{
    return QString::number(number);
}

static QString toString(bool b)
{
    return QString::number(b ? -1 : 0);
}

static QString toString(const QString &string)
{
    return string;
}

template <typename T>
static void writeProperty(QXmlStreamWriter &writer,
                          const Object *object,
                          const QString &name,
                          const T &def)
{
    const T value = optionalProperty(object, name, def);
    writer.writeTextElement(name, toString(value));
}

static QString sanitizeName(QString name)
{
    static const QRegularExpression regexp(QLatin1String("[^a-zA-Z0-9]"));
    return name.replace(regexp, QLatin1String("_"));
}

static QString colorToOLE(QColor &c)
{
    return toString(c.red() + (c.green() * 256) + (c.blue() * 256 * 256));
}
static void wrt(char *s, QIODevice *device,QTextCodec* codec)
{
    if (device)
            device->write(s, qint64(strlen(s)));
    else
        qWarning("QXmlStreamWriter: No device");
}

static void wrt(QString &s,QIODevice *device,QTextCodec* codec)
{
    QTextEncoder* encoder = codec->makeEncoder();
    const char *c =s.toStdString().c_str();
    QByteArray ba = QByteArray(c,strlen(c));
    QTextCodec* codec2 = QTextCodec::codecForUtfText(ba);
    encoder = codec2->makeEncoder();

    if (device)
        device->write(encoder->fromUnicode(s));
    else
        qWarning("QXmlStreamWriter: No device");
}
static QString getEscaped(const QString &s, bool escapeWhitespace)
{
    QString escaped;
    escaped.reserve(s.size());
    for ( int i = 0; i < s.size(); ++i ) {
        QChar c = s.at(i);
        if (c.unicode() == '<' )
            escaped.append(QLatin1String("&lt;"));
        else if (c.unicode() == '>' )
            escaped.append(QLatin1String("&gt;"));
        else if (c.unicode() == '&' )
            escaped.append(QLatin1String("&amp;"));
        else if (c.unicode() == '\"' )
            escaped.append(QLatin1String("&quot;"));
        else if (escapeWhitespace && c.isSpace()) {
            if (c.unicode() == '\n')
                escaped.append(QLatin1String("&#xA;"));
            else if (c.unicode() == '\r')
                escaped.append(QLatin1String("&#13;"));
            else if (c.unicode() == '\t')
                escaped.append(QLatin1String("&#9;"));
            else
                escaped += c;
        } else {
            escaped += QChar(c);
        }
    }
    return escaped;
}


static bool checkIfViewsDefined(const Map *map)
{
	bool enableViews = true;
	if(map->hasProperty("enableViews"))
	{
		auto enableViewsVar = map->property("enableViews");
		if(enableViewsVar.canConvert(QVariant::Bool))
		{
			enableViews = enableViewsVar.toBool();
		}
	}

	return enableViews;
}


//Tile animation stuff

struct AnimationLayer
{
    int depth;
    QVector<int> animSpeeds;
    int animLength;
};

static int indexOfAnim(int length, QVector<int> &speeds, QVector<AnimationLayer>* layers )
{
    if(layers->isEmpty())
        return -1;
    int len = layers->size();
    for (int i = 0; i<len; ++i)
    {
        if(layers->at(i).animLength==length && layers->at(i).animSpeeds == speeds)
        {
            return i;
        }

    }
    return -1;
}

static int indexOfAnim(int depth,int length, QVector<int> &speeds, QVector<AnimationLayer>* layers )
{
    if(layers->isEmpty())
        return -1;
    int len = layers->size();
    for (int i = 0; i<len; ++i)
    {
        if(layers->at(i).depth==depth&&layers->at(i).animLength==length && layers->at(i).animSpeeds == speeds)
        {
            return i;
        }

    }
    return -1;
}



GmxPlugin::GmxPlugin()
{
}

void GmxPlugin::mapTemplates(std::unordered_map<std::string,std::string> *map, QDir &root )
{
    QFileInfoList fileInfoList = root.entryInfoList();

    QDirIterator it(root.path(), QStringList() << "*.tx", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
		it.next();
		QFileInfo info = it.fileInfo();
        if(info.isFile())
        {
            map->emplace(make_pair(info.baseName().toStdString(),info.absoluteFilePath().toStdString()));
        }

    }
}


static TileLayer* tileLayerAtDepth(QVector<TileLayer*> &layers, int depth,int xo, int yo, Map *map)
{
    if(layers.isEmpty())
    {
        TileLayer* newLayer = new TileLayer(QString::number(depth).append(QString("_0")),0,0,map->width(),map->height());
        newLayer->setOffset(QPointF(xo,yo));
        newLayer->setProperty(QString("depth"),QVariant(depth));
        layers.append(newLayer);
        map->addLayer(newLayer);
        return newLayer;
    }
    else
    {
        for(TileLayer* tileLayer : layers)
        {
            QVariant lDepth = tileLayer->inheritedProperty(QString("depth"));
            if(lDepth.isValid())
            {
                int layerDepth = lDepth.toInt();
                if(layerDepth==depth && xo == tileLayer->offset().x() && yo == tileLayer->offset().y())
                {
                    return tileLayer;
                }
            }
        }

        TileLayer* newLayer = new TileLayer(QString::number(depth).append(QString("_")).append(QString::number(layers.size())),0,0,map->width(),map->height());
        newLayer->setOffset(QPointF(xo,yo));
        newLayer->setProperty(QString("depth"),QVariant(depth));
        layers.append(newLayer);
        map->addLayer(newLayer);
        return newLayer;
    }
}

static SharedTileset tilesetWithName(QVector<SharedTileset> &tilesets, QString bgName, Map *map, QDir &imageDir)
{

    for(SharedTileset tileset : tilesets)
    {
        if(tileset.get()->name() == bgName)
        {
            return tileset;
        }
    }
    SharedTileset newTileset = Tileset::create(bgName,map->tileWidth(),map->tileHeight(),0,0);
    if(newTileset->loadFromImage(imageDir.absoluteFilePath(bgName.append(".png"))))
    {
        tilesets.append(newTileset);
    }
    else
    {
        return nullptr;
    }
    map->addTileset(newTileset);
    return newTileset;

}

static bool lesThanLayer(Layer *lay1, Layer *lay2)
{
    int depth1 = 9999999;
    int depth2 = depth1;

    QVariant aux = lay1->inheritedProperty(QString("depth"));
    if(aux.isValid())
        depth1=aux.toInt();
    aux = lay2->inheritedProperty(QString("depth"));
    if(aux.isValid())
        depth2 = aux.toInt();
    return depth1>depth2;
}

void GmxPlugin::writeAttribute(QString &qualifiedName, QString &value, QIODevice* d, QTextCodec* codec)
{
    wrt(" ",d,codec);
    wrt(qualifiedName,d,codec);
    wrt("=\"",d,codec);
    QString escaped = getEscaped(value, true);
    wrt(escaped,d,codec);
    wrt("\"",d,codec);
}

Tiled::Map *GmxPlugin::read(const QString &fileName, QSettings *appSettings)
{
    using namespace rapidxml;
    using namespace std;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        mError = tr("Could not open file for reading.");
        return nullptr;
    }

    Gmx::ImporterSettings settings = {
      16,
        16,
        256,
        224,
        QString("../Backgrounds/images"),
        QString("../Objects/templates")
    };
    bool accepted = false;
    RoomImporterDialog *sDialog = new RoomImporterDialog(nullptr,&accepted,&settings);
	{
	//	Preferences* prefs = Preferences::instance();
		//sDialog->setDefaultPaths(prefs->gmProjectPath(), prefs->genTemplatesOutDir());
	}
	if(appSettings != nullptr)
	{
		sDialog->setDefaultPaths(appSettings);
	}

    sDialog->exec();

    delete sDialog;
    if(!accepted)
    {
        return nullptr;
    }
	qDebug()<<"Importing gmx room";
    xml_document<> doc;
    xml_node<> * root_node;

    ifstream theFile(fileName.toStdString().c_str());
    vector<char> buffer((istreambuf_iterator<char>(theFile)), istreambuf_iterator<char>());
    buffer.push_back('\0');

    doc.parse<0>(&buffer[0]);
    root_node = doc.first_node("room");
    xml_node<> *tile = root_node->first_node("tiles")->first_node("tile");
    xml_node<> *instance = root_node->first_node("instances")->first_node("instance");

	int roomSpeed = 60;
	{
		xml_node<> *roomSpeedNode = root_node->first_node("speed");
		if(roomSpeedNode)
		{
			roomSpeed = QString(roomSpeedNode->value()).toInt();
		}
	}

	bool enableViews = QString(root_node->first_node("enableViews")->value()) == "-1" ? true : false;
    int tileWidth = settings.tileWidth;
    int tileHeight = settings.tileHeigth;
    int mapWidth = QString(root_node->first_node("width")->value()).toInt()/tileWidth;
    int mapHeight = QString(root_node->first_node("height")->value()).toInt()/tileHeight;
    int bgColor = QString(root_node->first_node("colour")->value()).toInt();
    bool useBgColor = QString(root_node->first_node("showcolour")->value()).toInt() != 0;

    Map *newMap = new Map(Map::Orthogonal,mapWidth,mapHeight,tileWidth,tileHeight,false);
    newMap->setProperty("code",QVariant(root_node->first_node("code")->value()));
    newMap->setQuadWidth(settings.quadWidth);
    newMap->setQuadHeight(settings.quadHeigth);
	newMap->setProperty("enableViews", enableViews);
	newMap->setProperty("speed", roomSpeed);

    if(useBgColor)
    {
        int r = bgColor % 256;
        int g = (bgColor / 256) % 256;
        int b = (bgColor / 65536) % 256;
        newMap->setBackgroundColor(QColor(r,g,b));
    }


	//VIEW/BG IMPORT
	{
		qDebug()<<"Importing backgrounds";
		xml_node<> *background = root_node->first_node("backgrounds")->first_node("background");
		xml_node<> *view= root_node->first_node("views")->first_node("view");
		auto gmBgLayer = new Tiled::ObjectGroup(QStringLiteral("_gmRoomBgDefs"),0,0);
		auto gmViewLayer= new Tiled::ObjectGroup(QStringLiteral("_gmsRoomViewDefs"),0,0);
		int bgCount = 0;
		int viewCount = 0;

		while(background)
		{
			bool visible = QString(background->first_attribute("visible")->value()) == "-1" ? true : false;
			bool foreground = QString(background->first_attribute("foreground")->value()) == "-1" ? true : false;
			bool htiled = QString(background->first_attribute("htiled")->value()) == "-1" ? true : false;
			bool vtiled = QString(background->first_attribute("vtiled")->value()) == "-1" ? true : false;

			int x = QString(background->first_attribute("x")->value()).toInt();
			int y = QString(background->first_attribute("y")->value()).toInt();
			qreal hspeed = QString(background->first_attribute("hspeed")->value()).toFloat();
			qreal vspeed = QString(background->first_attribute("vspeed")->value()).toFloat();

			QString name = QString(background->first_attribute("name")->value());

			auto obj = new MapObject(QStringLiteral("BG_").append(QString::number(bgCount)), QStringLiteral("t_gmRoomBackground"),QPointF(16*bgCount,0), QSizeF(16,16));

			obj->setProperty(QStringLiteral("visible"), visible);
			obj->setProperty(QStringLiteral("foreground"), foreground);
			obj->setProperty(QStringLiteral("htiled"), htiled);
			obj->setProperty(QStringLiteral("vtiled"), vtiled);
			obj->setProperty(QStringLiteral("x"), x);
			obj->setProperty(QStringLiteral("y"), y);
			obj->setProperty(QStringLiteral("name"), name);
			obj->setProperty(QStringLiteral("hspeed"), hspeed);
			obj->setProperty(QStringLiteral("vspeed"), vspeed);
			obj->setProperty(QStringLiteral("bgId"), bgCount);

			gmBgLayer->addObject(obj);
			++bgCount;

			background = background->next_sibling();
		}
		qDebug()<<"Backgrounds imported";

		qDebug()<<"Importing views";
		while(view)
		{
			bool visible = QString(view->first_attribute("visible")->value()) == "-1" ? true : false;

			int xview = QString(view->first_attribute("xview")->value()).toInt();
			int yview = QString(view->first_attribute("yview")->value()).toInt();
			int wview = QString(view->first_attribute("wview")->value()).toInt();
			int hview = QString(view->first_attribute("hview")->value()).toInt();

			int xport = QString(view->first_attribute("xport")->value()).toInt();
			int yport = QString(view->first_attribute("yport")->value()).toInt();
			int wport = QString(view->first_attribute("wport")->value()).toInt();
			int hport = QString(view->first_attribute("hport")->value()).toInt();

			int hborder= QString(view->first_attribute("hborder")->value()).toInt();
			int vborder = QString(view->first_attribute("vborder")->value()).toInt();

			qreal hspeed = QString(view->first_attribute("hspeed")->value()).toFloat();
			qreal vspeed = QString(view->first_attribute("vspeed")->value()).toFloat();

			auto obj = new MapObject(QStringLiteral("VIEW_").append(QString::number(viewCount)), QStringLiteral("t_gmRoomView"),QPointF(16*viewCount,16), QSizeF(16,16));

			obj->setProperty(QStringLiteral("visible"), visible);
			obj->setProperty(QStringLiteral("xview"), xview);
			obj->setProperty(QStringLiteral("yview"), yview);
			obj->setProperty(QStringLiteral("wview"), wview);
			obj->setProperty(QStringLiteral("hview"), hview);

			obj->setProperty(QStringLiteral("xport"), xport);
			obj->setProperty(QStringLiteral("yport"), yport);
			obj->setProperty(QStringLiteral("wport"), wport);
			obj->setProperty(QStringLiteral("hport"), hport);

			obj->setProperty(QStringLiteral("vborder"), vborder);
			obj->setProperty(QStringLiteral("hborder"), hborder);

			obj->setProperty(QStringLiteral("hspeed"), hspeed);
			obj->setProperty(QStringLiteral("vspeed"), vspeed);
			obj->setProperty(QStringLiteral("viewId"), viewCount);

			gmViewLayer->addObject(obj);
			++viewCount;

			view = view->next_sibling();
			qDebug()<<"added view";

		}
		qDebug()<<"Views Imported";

		newMap->addLayer(gmBgLayer);
		newMap->addLayer(gmViewLayer);
	}
	//VIEW/BG IMPORT END


    QVector<TileLayer*> *mapLayers = new QVector<TileLayer*>();
    QVector<SharedTileset> *tilesets = new QVector<SharedTileset>();

	//Import tiles
    QDir imageDir = QDir(settings.imagesPath);
    while(tile)
    {
        if(QString(tile->name()) == "tile")
        {

            int w = QString(tile->first_attribute("w")->value()).toInt();
            int h = QString(tile->first_attribute("h")->value()).toInt();
            int htiles=1;
            int vtiles=1;
            if(w!=tileWidth||h!=tileHeight)
            {
                if(((w%tileWidth)==0) && ((h%tileWidth)==0))
                {
                    htiles = w/tileWidth;
                    vtiles = h/tileHeight;
                }
                else
                {
                    tile = tile->next_sibling();
                    continue;
                }
            }
            int x = QString(tile->first_attribute("x")->value()).toInt();
            int y = QString(tile->first_attribute("y")->value()).toInt();
            int xo = QString(tile->first_attribute("xo")->value()).toInt();
            int yo = QString(tile->first_attribute("yo")->value()).toInt();
            int scaleX = QString(tile->first_attribute("scaleX")->value()).toInt();
            int scaleY = QString(tile->first_attribute("scaleY")->value()).toInt();
            if(scaleX==-1)
                x-=tileWidth;
            if(scaleY==-1)
                y-=tileHeight;
            int xoff = x%tileWidth;
            int yoff = y%tileHeight;
            x = int(floor(x/tileWidth)*tileWidth);
            y = int(floor(y/tileHeight)*tileHeight);
            int depth = QString(tile->first_attribute("depth")->value()).toInt();
            QString bgName = QString(tile->first_attribute("bgName")->value());
            TileLayer *layer = tileLayerAtDepth(*mapLayers,depth,xoff, yoff,newMap);
            if(layer==nullptr)
            {
                tile = tile->next_sibling();
                continue;
            }
            SharedTileset tileset = tilesetWithName(*tilesets,bgName,newMap,imageDir);
            if(tileset==nullptr)
            {
                tile = tile->next_sibling();
                continue;
            }
            for(int ht = 0;ht<htiles;++ht)
            {
                for(int vt=0; vt<vtiles;vt++)
                {
                    int tileID = ((xo/tileWidth)+ht) + ((yo/tileHeight)+vt)*tileset->columnCount();

                    Tile *addtile  = new Tile(tileID, tileset.get());
                    Cell ncell = Cell(addtile);
                    if(scaleX==-1)
                        ncell.setFlippedHorizontally(true);
                    if(scaleY==-1)
                        ncell.setFlippedVertically(true);
                    layer->setCell(int(floor(x/tileWidth))+ht,int(floor(y/tileHeight))+vt,ncell);
                }
            }


        }
        else
        {
            break;
        }
        tile = tile->next_sibling();
    }
    qDebug()<<"Tiles imported";


    //Import Objects using templates
    Tiled::ObjectGroup *objects;

    if(instance)
    {
		objects = new Tiled::ObjectGroup(QString("objects"),0,0);
        objects->setProperty(QString("depth"),QVariant(0));
        unordered_map<string,string> *templateMap = new unordered_map<string,string> ();
        QDir dir = QDir(settings.templatePath.append(QString("/")));
        GmxPlugin::mapTemplates(templateMap,dir);

        QDir *imagesPath = new QDir(settings.templatePath);
        imagesPath->cdUp();
        SharedTileset imagesTileset = TilesetManager::instance()->loadTileset(imagesPath->filePath(QString("images.tsx")));

        if(imagesTileset.get()!=nullptr)
            newMap->addTileset(imagesTileset);
        delete imagesPath;
        for(; instance ; instance=instance->next_sibling())
        {
            int x = QString(instance->first_attribute("x")->value()).toInt();

            int y = QString(instance->first_attribute("y")->value()).toInt();

            QString code = QString(instance->first_attribute("code")->value());

            QString objName = QString(instance->first_attribute("objName")->value());

            QString tempath = QString("");
            unordered_map<string,string>::iterator it = templateMap->find(objName.toStdString());

            if(it != templateMap->end())
            {
                tempath = QString(it->second.c_str());
            }

            QPointF pos = QPointF(x,y);
            ObjectTemplate *templ = TemplateManager::instance()->loadObjectTemplate(tempath);

			if(templ!=nullptr&&templ->object()!=nullptr&&templ->object()->inheritedProperty(QStringLiteral("originX")).isValid())
            {
				QVariant aux = templ->object()->inheritedProperty(QStringLiteral("originX"));
                int originX = aux.toInt();
				aux = templ->object()->inheritedProperty(QStringLiteral("originY"));
                int originY = aux.toInt();

                double rotation = QString(instance->first_attribute("rotation")->value()).toDouble() *-1;
                double scaleX = QString(instance->first_attribute("scaleX")->value()).toDouble();
                double scaleY = QString(instance->first_attribute("scaleY")->value()).toDouble();
                if(scaleX<0)
                {
                    originX = int(templ->object()->width() - originX);
                }
                if(scaleY<0)
                {
                    originY = int(templ->object()->height() - originY);
                }
                QPointF origin = QPointF(-originX*abs(scaleX),-originY*abs(scaleY));
                if(templ->object()->isTileObject())
                    origin.setY(origin.y()+(templ->object()->height()*abs(scaleY)));
                QTransform transform;
                transform.rotate(rotation);
                pos += transform.map(origin);

				MapObject *obj = new MapObject(QStringLiteral(""),objName,pos,QSizeF(templ->object()->width()*abs(scaleX),templ->object()->height()*abs(scaleY)));

				obj->setObjectTemplate(templ);

				//obj->setProperties(templ->object()->properties());
                obj->setCell(templ->object()->cell());
				obj->setProperty(QStringLiteral("code"),code);

                if(scaleX<0)
                    obj->flip(FlipHorizontally,pos);
                if(scaleY<0)
                    obj->flip(FlipVertically,pos);
                obj->setRotation(rotation);

                obj->setSize(QSizeF(templ->object()->width()*abs(scaleX),templ->object()->height()*abs(scaleY)));


                objects->addObject(obj);
            }
            else
            {

				int originX = 0;
				int originY = 0;

				double rotation = QString(instance->first_attribute("rotation")->value()).toDouble() *-1;
				double scaleX = QString(instance->first_attribute("scaleX")->value()).toDouble();
				double scaleY = QString(instance->first_attribute("scaleY")->value()).toDouble();

				QPointF origin = QPointF(-originX*abs(scaleX),-originY*abs(scaleY));

				QTransform transform;
				transform.rotate(rotation);
				pos += transform.map(origin);

				MapObject *obj = new MapObject(QStringLiteral(""),objName,pos,QSizeF(8,8));

				obj->setProperty(QStringLiteral("code"),code);

				if(scaleX<0)
					obj->flip(FlipHorizontally,pos);
				if(scaleY<0)
					obj->flip(FlipVertically,pos);
				obj->setRotation(rotation);

				obj->setSize(QSizeF(8*abs(scaleX),8*abs(scaleY)));


				objects->addObject(obj);
                continue;
            }
        }
        newMap->addLayer(objects);

        qDebug()<<"Done";
        delete templateMap;
    }

    QList<Layer*> *mLayers = newMap->layersNoConst();
    if(!mLayers->isEmpty())
    {
        std::sort(mLayers->begin(),mLayers->end(),lesThanLayer);
    }

    delete mapLayers;
    delete tilesets;

    return newMap;

}

bool GmxPlugin::supportsFile(const QString &fileName) const
{
    if(fileName.endsWith(".room.gmx"))
        return true;
    return false;
}

static rapidxml::xml_node<>* appendNode(rapidxml::xml_document<> *doc, rapidxml::xml_node<>* prt, const char* name, int value)
{
	using namespace  rapidxml;
	xml_node<> *node = doc->allocate_node(node_type::node_element, name, doc->allocate_string( QString::number(value).toStdString().c_str()));
	prt->append_node(node);
	return node;
}

static void writeBackground(QXmlStreamWriter *stream, bool visible = false, bool foreground = false, QString name = "", int x = 0 , int y = 0, bool htiled = true, bool vtiled = true, qreal hspeed = 0, qreal vspeed = 0, qreal stretch = 0)
{
	stream->writeStartElement("background");

	stream->writeAttribute("visible", toString(visible));
	stream->writeAttribute("foreground", toString(foreground));
	stream->writeAttribute("name", name);
	stream->writeAttribute("x", toString(x));
	stream->writeAttribute("y", toString(y));
	stream->writeAttribute("htiled", toString(htiled));
	stream->writeAttribute("vtiled", toString(vtiled));
	stream->writeAttribute("hspeed", toString(hspeed));
	stream->writeAttribute("vspeed", toString(vspeed));
	stream->writeAttribute("stretch", toString(stretch));

	stream->writeEndElement();
}

static void writeBackground(QXmlStreamWriter *stream, const MapObject *object)
{
	bool visible = optionalProperty(object, "visible", false);
	bool foreground = optionalProperty(object, "foreground", false);
	QString name = optionalProperty(object, "name", QStringLiteral(""));
	int x = optionalProperty(object, "x", 0);
	int y = optionalProperty(object, "y", 0);
	bool htiled = optionalProperty(object, "htiled", true);
	bool vtiled= optionalProperty(object, "vtiled", true);

	qreal hspeed = optionalProperty(object, "hspeed", -1.0);
	qreal vspeed = optionalProperty(object, "vspeed", -1.0);
	qreal stretch = optionalProperty(object, "stetch", 0.0);

	writeBackground(stream, visible, foreground, name, x  , y, htiled, vtiled, hspeed,vspeed,stretch);
}

static void writeView(QXmlStreamWriter *stream, bool visible = false, int xview =0 , int yview = 0, int wview = 640, int hview = 480, int xport = 0, int yport = 0, int hport = 480, int wport = 640, int hborder = 32, int vborder = 32, qreal vspeed = -1, qreal hspeed = -1)
{
	stream->writeStartElement("view");

	stream->writeAttribute("visible", toString(visible));
	stream->writeAttribute("objName", QStringLiteral("<undefined>") );
	stream->writeAttribute("xview", toString(xview));
	stream->writeAttribute("yview", toString(yview));
	stream->writeAttribute("wview", toString(wview));
	stream->writeAttribute("hview", toString(hview));
	stream->writeAttribute("xport", toString(xport));
	stream->writeAttribute("yport", toString(yport));
	stream->writeAttribute("wport", toString(wport));
	stream->writeAttribute("hport", toString(hport));
	stream->writeAttribute("hborder", toString(hborder));
	stream->writeAttribute("vborder", toString(vborder));
	stream->writeAttribute("hspeed", toString(hspeed));
	stream->writeAttribute("vspeed", toString(vspeed));

	stream->writeEndElement();
}

static void writeView(QXmlStreamWriter *stream, const MapObject *object)
{
	bool visible = optionalProperty(object, "visible", true);
	int xview = optionalProperty(object, "xview", 0);
	int yview = optionalProperty(object, "yview", 0);
	int wview = optionalProperty(object, "wview", 640);
	int hview = optionalProperty(object, "hview", 480);

	int xport = optionalProperty(object, "xport", 0);
	int yport = optionalProperty(object, "yport", 0);
	int wport = optionalProperty(object, "wport", 640);
	int hport = optionalProperty(object, "hport", 480);

	int hborder = optionalProperty(object, "hborder", 32);
	int vborder = optionalProperty(object, "vborder", 32);

	qreal hspeed = optionalProperty(object, "hspeed", -1.0);
	qreal vspeed = optionalProperty(object, "vspeed", -1.0);

	writeView(stream, visible, xview, yview, wview, hview, xport, yport, hport, wport, hborder, vborder, vspeed, hspeed);
}

bool GmxPlugin::write(const Map *map, const QString &fileName)
{
	using namespace rapidxml;
    SaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        mError = tr("Could not open file for writing.");
        return false;
    }

	int mapPixelWidth = map->tileWidth() * map->width();
	int mapPixelHeight = map->tileHeight() * map->height();

	//We use this to reorder things)
	QBuffer instancesText;
	QBuffer tilesText;
	instancesText.open(QIODevice::WriteOnly | QIODevice::Text);
	tilesText.open(QIODevice::WriteOnly | QIODevice::Text);
	QTextStream instancesStream(&instancesText);
	QTextStream tilesStream(&tilesText);

	QXmlStreamWriter stream;

	stream.setDevice(file.device());
	stream.device()->write("<!--This Document is generated by GameMaker, if you edit it by hand then you do so at your own risk!-->");
	//stream.writeComment("This Document is generated by Tiled, if you edit it by hand then you do so at your own risk!");
    stream.setAutoFormatting(true);
    stream.setAutoFormattingIndent(2);
    stream.writeStartElement("room");




    stream.writeTextElement("caption", QLatin1String(""));
    stream.writeTextElement("width", QString::number(mapPixelWidth));
    stream.writeTextElement("height", QString::number(mapPixelHeight));
    stream.writeTextElement("vsnap", QString::number(map->tileHeight()));
    stream.writeTextElement("hsnap", QString::number(map->tileWidth()));
    stream.writeTextElement("isometric", QString::number(0));

	writeProperty(stream, map, "speed", optionalProperty(map, QStringLiteral("speed"), 60) );
    writeProperty(stream, map, "persistent", false);
    stream.writeTextElement("colour", colorToOLE((QColor&)map->backgroundColor()));
    stream.writeTextElement("showcolour",toString(optionalProperty(map,"showcolour",true)));
    writeProperty(stream, map, "code", QString());


    // Check if views are defined
    bool enableViews = checkIfViewsDefined(map);
    writeProperty(stream, map, "enableViews", enableViews);

    writeProperty(stream, map, "clearViewBackground", toString(true));
    writeProperty(stream, map, "clearDisplayBuffer", toString(true));
   stream.writeStartElement("makerSettings");
   stream.writeTextElement("isSet",toString(0));
   stream.writeTextElement("w",toString(0));
   stream.writeTextElement("h",toString(0));
   stream.writeTextElement("showGrid",toString(false));
   stream.writeTextElement("showObjects",toString(false));
   stream.writeTextElement("showTiles",toString(false));
   stream.writeTextElement("showBackgrounds",toString(false));
   stream.writeTextElement("showForegrounds",toString(false));
   stream.writeTextElement("showViews",toString(false));
   stream.writeTextElement("deleteUnderlyingObj",toString(false));
   stream.writeTextElement("deleteUnderlyingTiles",toString(false));
   stream.writeTextElement("page",toString(0));
   stream.writeTextElement("xoffset",toString(0));
   stream.writeTextElement("yoffset",toString(0));
   stream.writeEndElement();

	//quint64 instanceMark=0;

	LayerIterator iterator(map);

	// Write out views
	if (true) {
		//Write backgrounds
		stream.writeStartElement("backgrounds");

		int bgCount = 0;

		while (const Layer *layer = iterator.next()) {

			if (layer->layerType() != Layer::ObjectGroupType)
				continue;

			if(layer->name() != QStringLiteral("_gmRoomBgDefs"))
			{
				continue;
			}

			const ObjectGroup *objectLayer = static_cast<const ObjectGroup*>(layer);


			for(int bgId = 0; bgId < 8; ++bgId)
			{
				for (const MapObject *object : objectLayer->objects()) {
					const QString type = object->effectiveType();
					if (type != "t_gmRoomBackground")
						continue;

					auto varBgId = object->inheritedProperty("bgId");
					if(varBgId.isValid())
					{
						int objBgId = varBgId.toInt();
						if(objBgId != bgId)
						{
							continue;
						}
					}
					else
					{
						continue;
					}

					writeBackground(&stream, object);
					++bgCount;
					if(bgCount >= 8)
					{
						break;
					}
				}
			}

			if(bgCount == 0)
			{
				//If the room doesn't have backgrounds defined because
				//it was created before the update set it automatically
				//asuming it's a level for megamix engine
				QString bgquad;
				if(map->quadWidth()==256 && map->quadHeight()==224)
					 bgquad = QString("bgQuadMM9_10");
				else if(map->quadWidth()==256 && map->quadHeight()==240)
					bgquad = QString("bgQuadMM1_6");
				else if(map->quadWidth() == 400 && map->quadHeight()==224)
					bgquad = QString("bgQuad400x224");
				else if(map->quadWidth() == 400 && map->quadHeight()==240)
					bgquad = QString("bgQuad400x240");
				else if(map->quadWidth())
					bgquad = QString("bgQuad").append(QString::number(map->quadWidth())).append(QString("x")).append(QString::number(map->quadHeight()));

				writeBackground(&stream, true, true, bgquad);
				++bgCount;

			}

			//Fill the blanks
			for(int bgId = bgCount; bgId < 8; ++bgId)
			{
				writeBackground(&stream);
			}

			break;
		}
		iterator.toFront();


		stream.writeEndElement();//backgrounds

		//Write views
        stream.writeStartElement("views");
        int viewCount = 0;

        while (const Layer *layer = iterator.next()) {

            if (layer->layerType() != Layer::ObjectGroupType)
                continue;

			if(layer->name() != QStringLiteral("_gmsRoomViewDefs"))
			{
				continue;
			}

            const ObjectGroup *objectLayer = static_cast<const ObjectGroup*>(layer);


			for(int viewId = 0; viewId < 8; ++viewId)
			{
				for (const MapObject *object : objectLayer->objects()) {
					const QString type = object->effectiveType();
					if (type != "t_gmRoomView")
						continue;

					//This is to add them in the right order
					//there's just 8 so we can just loop a bunch
					auto varViewId = object->inheritedProperty("viewId");
					if(varViewId.isValid())
					{
						int objViewId = varViewId.toInt();
						if(objViewId != viewId)
						{
							continue;
						}
					}
					else
					{
						continue;
					}

					writeView(&stream, object);
					viewCount++;

					// Only do 8 views
					if (viewCount >= 8)
						break;
				}
			}

			//Write default view in case the level was created before
			//the update
			if(viewCount == 0)
			{
				writeView(&stream, true, 0, 0, 256, 224, 0, 0, 224, 256, 9999, 9999);
				++viewCount;
			}


			//Fill the blanks
			for(int viewId = viewCount; viewId < 8; ++viewId)
			{
				writeView(&stream);
			}

			break;
        }

		stream.writeEndElement();//views

		iterator.toFront();
    }



    iterator.toFront();

	stream.setDevice(tilesStream.device());
    stream.writeStartElement("tiles");

    uint tileId = 0u;

    // Write out tile instances
    iterator.toBack();

    QVector<AnimationLayer> *animationLayers = new QVector<AnimationLayer>();
    int animStartLayer = 2000000;

    {
        QVariant auxP = map->property(QString("animationLayer"));
        if(auxP.isValid())
            animStartLayer= auxP.toInt();
    }
    int lastAnimLayerDepth = animStartLayer;
    int depthOff = 0;
    int layerCount = 0;
    while (const Layer *layer = iterator.previous()) {
        --layerCount;
        QString depth = QString::number(optionalProperty(layer, QLatin1String("depth"),
                                                         depthOff + 1000000));

        int xoff = layer->offset().x();
        int yoff = layer->offset().y();

        switch (layer->layerType()) {
        case Layer::TileLayerType: {
            ++depthOff;
            auto tileLayer = static_cast<const TileLayer*>(layer);
            for (int y = 0; y < tileLayer->height(); ++y) {
                for (int x = 0; x < tileLayer->width(); ++x) {
                    const Cell &cell = tileLayer->cellAt(x, y);
                    if (const Tile *tile = cell.tile()) {
                        const Tileset *tileset = tile->tileset();

                        if(tile->isAnimated())
                        {
                            if(tileset->isCollection())
                                continue;
                            int animLayer = animStartLayer;
                            int animLength = tile->frames().size();

                            QVector<int> animSpeeds = QVector<int>();
                            for(int j=0;j<animLength;++j)
                            {
                                animSpeeds.append(tile->frames().at(j).duration);
                            }
                            bool customDepth = false;
                            {
                                QVariant prp = tileset->inheritedProperty(QLatin1String("animationLayer"));
                                if(prp.isValid()){
                                    animLayer=prp.toInt();
                                    customDepth=true;
                                }

                            }
                            int layerID = -1;

                            if(!customDepth)
                                layerID = indexOfAnim(animLength,animSpeeds,animationLayers);
                            else
                                layerID = indexOfAnim(animLayer,animLength,animSpeeds,animationLayers);
                            if(layerID!=-1)
                            {
                                animLayer = animationLayers->at(layerID).depth;
                            }
                            else
                            {
                                if(!customDepth)
                                {
                                    AnimationLayer newLayer = {
                                        (lastAnimLayerDepth),
                                        animSpeeds,
                                        animLength
                                    };
                                    lastAnimLayerDepth += animLength;
                                    animLayer = newLayer.depth;
                                    animationLayers->append(newLayer);
                                }
                                else
                                {
                                    AnimationLayer newLayer = {
                                        (animLayer),
                                        animSpeeds,
                                        animLength
                                    };

                                    animationLayers->append(newLayer);
                                }
                            }


                            //Add tiles
                            int pixelX = x * map->tileWidth();
                            int pixelY = y * map->tileHeight();
                            qreal scaleX = 1;
                            qreal scaleY = 1;

                            if (cell.flippedHorizontally()) {
                                scaleX = -1;
                                pixelX += tile->width();
                            }
                            if (cell.flippedVertically()) {
                                scaleY = -1;
                                pixelY += tile->height();
                            }

                            QString bgName;
                            int xo = 0;
                            int yo = 0;

                            if (tileset->isCollection()) {
                                bgName = QFileInfo(tile->imageSource().path()).baseName();

                            }
                            else
                            {
                                bgName = tileset->name();
                            }

                            for(int t =0; t<animLength;t++)
                            {
                                int xInTilesetGrid = tile->frames().at(t).tileId % tileset->columnCount();
                                int yInTilesetGrid = int(tile->frames().at(t).tileId / tileset->columnCount());

                                xo = tileset->margin() + (tileset->tileSpacing() + tileset->tileWidth()) * xInTilesetGrid;
                                yo = tileset->margin() + (tileset->tileSpacing() + tileset->tileHeight()) * yInTilesetGrid;

                                stream.writeStartElement("tile");

                                stream.writeAttribute("bgName", bgName);
                                stream.writeAttribute("x", QString::number(pixelX+xoff));
                                stream.writeAttribute("y", QString::number(pixelY+yoff));
                                stream.writeAttribute("w", QString::number(tile->width()));
                                stream.writeAttribute("h", QString::number(tile->height()));

                                stream.writeAttribute("xo", QString::number(xo));
                                stream.writeAttribute("yo", QString::number(yo));

                                stream.writeAttribute("id", QString::number(++tileId));
                                stream.writeAttribute("depth", QString::number(animLayer + t));
                                stream.writeAttribute("locked","0");
                                stream.writeAttribute("colour",QString::number(4294967295));

                                stream.writeAttribute("scaleX", QString::number(scaleX));
                                stream.writeAttribute("scaleY", QString::number(scaleY));

                                stream.writeEndElement();

                            }

                        }
                        else
                        {

                            stream.writeStartElement("tile");

                            int pixelX = x * map->tileWidth();
                            int pixelY = y * map->tileHeight();
                            qreal scaleX = 1;
                            qreal scaleY = 1;

                            if (cell.flippedHorizontally()) {
                                scaleX = -1;
                                pixelX += tile->width();
                            }

                            if (cell.flippedVertically()) {
                                scaleY = -1;
                                pixelY += tile->height();
                            }

                            QString bgName;
                            int xo = 0;
                            int yo = 0;

                            if (tileset->isCollection()) {
                                bgName = QFileInfo(tile->imageSource().path()).baseName();
                            } else {
                                bgName = tileset->name();

                                int xInTilesetGrid = tile->id() % tileset->columnCount();
                                int yInTilesetGrid = (int)(tile->id() / tileset->columnCount());

                                xo = tileset->margin() + (tileset->tileSpacing() + tileset->tileWidth()) * xInTilesetGrid;
                                yo = tileset->margin() + (tileset->tileSpacing() + tileset->tileHeight()) * yInTilesetGrid;
                            }

                            stream.writeAttribute("bgName", bgName);
                            stream.writeAttribute("x", QString::number(pixelX+xoff));
                            stream.writeAttribute("y", QString::number(pixelY+yoff));
                            stream.writeAttribute("w", QString::number(tile->width()));
                            stream.writeAttribute("h", QString::number(tile->height()));

                            stream.writeAttribute("xo", QString::number(xo));
                            stream.writeAttribute("yo", QString::number(yo));

                            stream.writeAttribute("id", QString::number(++tileId));
                            stream.writeAttribute("depth", depth);
                            stream.writeAttribute("locked","0");
                            stream.writeAttribute("colour",QString::number(4294967295));


                            stream.writeAttribute("scaleX", QString::number(scaleX));
                            stream.writeAttribute("scaleY", QString::number(scaleY));

                            stream.writeEndElement();
                        }
                    }
                }
            }
            break;
        }

        case Layer::ObjectGroupType: {
            auto objectGroup = static_cast<const ObjectGroup*>(layer);
            auto objects = objectGroup->objects();

            // Make sure the objects export in the rendering order
            if (objectGroup->drawOrder() == ObjectGroup::TopDownOrder) {
                std::stable_sort(objects.begin(), objects.end(),
                                 [](const MapObject *a, const MapObject *b) { return a->y() < b->y(); });
            }

            for (const MapObject *object : qAsConst(objects)) {
                // Objects with types are already exported as instances
                if (!object->effectiveType().isEmpty())
                    continue;

                // Non-typed tile objects are exported as tiles. Rotation is
                // not supported here, but scaling is.
                if (const Tile *tile = object->cell().tile()) {
                    const Tileset *tileset = tile->tileset();

                    const QSize tileSize = tile->size();
                    qreal scaleX = object->width() / tileSize.width();
                    qreal scaleY = object->height() / tileSize.height();
                    qreal x = object->x();
                    qreal y = object->y() - object->height();

                    if (object->cell().flippedHorizontally()) {
                        scaleX *= -1;
                        x += object->width();
                    }
                    if (object->cell().flippedVertically()) {
                        scaleY *= -1;
                        y += object->height();
                    }

                    stream.writeStartElement("tile");

                    QString bgName;
                    int xo = 0;
                    int yo = 0;

                    if (tileset->isCollection()) {
                        bgName = QFileInfo(tile->imageSource().path()).baseName();
                    } else {
                        bgName = tileset->name();

                        int xInTilesetGrid = tile->id() % tileset->columnCount();
                        int yInTilesetGrid = tile->id() / tileset->columnCount();

                        xo = tileset->margin() + (tileset->tileSpacing() + tileset->tileWidth()) * xInTilesetGrid;
                        yo = tileset->margin() + (tileset->tileSpacing() + tileset->tileHeight()) * yInTilesetGrid;
                    }

                    stream.writeAttribute("bgName", bgName);
                    stream.writeAttribute("x", QString::number(qRound(x)));
                    stream.writeAttribute("y", QString::number(qRound(y)));
                    stream.writeAttribute("w", QString::number(tile->width()));
                    stream.writeAttribute("h", QString::number(tile->height()));

                    stream.writeAttribute("xo", QString::number(xo));
                    stream.writeAttribute("yo", QString::number(yo));

                    stream.writeAttribute("id", QString::number(++tileId));
                    stream.writeAttribute("depth", depth);

                    stream.writeAttribute("scaleX", QString::number(scaleX));
                    stream.writeAttribute("scaleY", QString::number(scaleY));

                    stream.writeEndElement();
                }
            }
            break;
        }

        case Layer::ImageLayerType:
            // todo: maybe export as backgrounds?
            break;

        case Layer::GroupLayerType:
            // Recursion handled by LayerIterator
            break;
        }
    }
    stream.writeEndElement();
	stream.setDevice(instancesStream.device());

    iterator.toFront();
    stream.writeStartElement("instances");

    QSet<QString> usedNames;

    // Write out object instances
    while (const Layer *layer = iterator.next()) {

        if (layer->layerType() != Layer::ObjectGroupType)
            continue;
		if(layer->name() == QStringLiteral("_gmsRoomViewDefs"))
		{
			continue;
		}

		if(layer->name() == QStringLiteral("_gmRoomBgDefs"))
		{
			continue;
		}

        const ObjectGroup *objectLayer = static_cast<const ObjectGroup*>(layer);

        for (const MapObject *object : objectLayer->objects()) {
            const QString type = object->effectiveType();
            if (type.isEmpty())
                continue;
            if (type == "view")
                continue;

            stream.writeStartElement("instance");

            // The type is used to refer to the name of the object
            stream.writeAttribute("objName", sanitizeName(type));


            QPointF pos = object->position();
            qreal scaleX = 1;
            qreal scaleY = 1;
            qreal imageWidth = optionalProperty(object,"imageWidth",-1);
            qreal imageHeigth = optionalProperty(object,"imageHeight",-1);

            QPointF origin(optionalProperty(object, "originX", 0.0),
                           optionalProperty(object, "originY", 0.0));

            if (!object->cell().isEmpty()) {
                // For tile objects we can support scaling and flipping, though
                // flipping in combination with rotation doesn't work in GameMaker.

                using namespace std;
                if (auto tile = object->cell().tile()) {
                    const QSize tileSize = tile->size();
                    if(imageWidth==-1||imageHeigth==-1)
                    {
                        imageHeigth=tileSize.height();
                        imageWidth=tileSize.width();
                    }
                    scaleX = abs(object->width() / imageWidth) ;
                    scaleY = abs(object->height() / imageHeigth);


                    if (object->cell().flippedHorizontally()) {
                        scaleX *= -1;
                        //origin += QPointF((object->width()*scaleX) - 2 * origin.x(), 0);
                    }
                    if (object->cell().flippedVertically()) {
                        scaleY *= -1;
                        //origin += QPointF(0, (object->height()*scaleY) - 2 * origin.y());
                    }
                    if(scaleX>=0){
                        origin.setX(origin.x()*scaleX);
                    }
                    else{
                        origin.setX((imageWidth*abs(scaleX))-(origin.x()*abs(scaleX)));
                    }

                    if(scaleY>=0){
                        origin.setY(origin.y()*scaleY);
                    }
                    else{
                        origin.setY((imageHeigth*abs(scaleY))-(origin.y()*abs(scaleY)));
                    }
                }
                // Tile objects have bottom-left origin in Tiled, so the
                // position needs to be translated for top-left origin in
                // GameMaker, taking into account the rotation.
                origin += QPointF(0, -object->height());


            }
            else if(imageWidth!=-1 && imageHeigth!=-1){
                scaleX = object->width() / imageWidth;
                scaleY = object->height() / imageHeigth;
            }
            // Allow overriding the scale using custom properties
            scaleX = optionalProperty(object, "scaleX", scaleX);
            scaleY = optionalProperty(object, "scaleY", scaleY);

            // Adjust the position based on the origin
            QTransform transform;
            transform.rotate(object->rotation());
            pos += transform.map(origin);

            stream.writeAttribute("x", QString::number(qRound(pos.x())));
            stream.writeAttribute("y", QString::number(qRound(pos.y())));

            // Include object ID in the name when necessary because duplicates are not allowed
            if (object->name().isEmpty()) {
                stream.writeAttribute("name", QString("inst_%1").arg(object->id()));
            } else {
                QString name = sanitizeName(object->name());

                while (usedNames.contains(name))
                    name += QString("_%1").arg(object->id());

                usedNames.insert(name);
                stream.writeAttribute("name", name);
            }
            //Custom add atribute
            QString cc = QString("code");
            QString cc2 = optionalProperty(object, "code", QString());
            writeAttribute(cc,cc2,(QIODevice*)stream.device(),(QTextCodec*)stream.codec());
            //stream.writeAttribute("code", optionalProperty(object, "code", QString()));
            stream.writeAttribute("scaleX", QString::number(scaleX));
            stream.writeAttribute("scaleY", QString::number(scaleY));
            stream.writeAttribute("rotation", QString::number(-object->rotation()));

            stream.writeEndElement();

        }
    }
    //WriteTileAnimatorInstances
    int alen = animationLayers->size();
    for(int anim = 0; anim<alen; anim++)
    {
        int animLen = animationLayers->at(anim).animLength;

        int animDepth = animationLayers->at(anim).depth;

        stream.writeStartElement("instance");

        stream.writeAttribute("objName", sanitizeName(QLatin1String("objLayerAnimation")));

        stream.writeAttribute("x", QString::number(anim*16));
        stream.writeAttribute("y", QString::number(0));

        stream.writeAttribute("name", QString("LayerAnimationInst%1").arg(anim));
        QString cCode(QString(QString("animLength = ").append(QString::number(animLen)).append(QString(";\nanimateOnTransition = true;\nanimationLayer = ")).append(QString::number(animDepth)).append(QString(";"))));

        for(int frame = 0; frame <animLen ; frame++)
        {
            int animSpeed = int(round(animationLayers->at(anim).animSpeeds.at(frame) * 0.001*60));
            cCode.append(QString("\nanimTime[").append(QString::number(frame)).append(QString("] = ").append(QString::number(animSpeed)).append(QString(";"))));
        }

        //Custom add atribute
        QString cc = QString("code");
        writeAttribute(cc,cCode,(QIODevice*)stream.device(),(QTextCodec*)stream.codec());
        //stream.writeAttribute("code", optionalProperty(object, "code", QString()));
        stream.writeAttribute("scaleX", QString::number(1));
        stream.writeAttribute("scaleY", QString::number(1));
        stream.writeAttribute("rotation", QString::number(0));

        stream.writeEndElement();

    }

	stream.writeEndElement(); //Instances
	stream.setDevice(file.device());
	tilesText.close();
	stream.device()->write(tilesText.buffer());
	instancesText.close();
	stream.device()->write(instancesText.buffer());



    writeProperty(stream, map, "PhysicsWorld", false);
    writeProperty(stream, map, "PhysicsWorldTop", 0);
    writeProperty(stream, map, "PhysicsWorldLeft", 0);
    writeProperty(stream, map, "PhysicsWorldRight", mapPixelWidth);
    writeProperty(stream, map, "PhysicsWorldBottom", mapPixelHeight);
    writeProperty(stream, map, "PhysicsWorldGravityX", 0.0);
    writeProperty(stream, map, "PhysicsWorldGravityY", 10.0);
    writeProperty(stream, map, "PhysicsWorldPixToMeters", 0.1);

    stream.writeEndDocument();


    if (!file.commit()) {
        mError = file.errorString();
        return false;
    }
    delete animationLayers;
    return true;
}

QString GmxPlugin::errorString() const
{
    return mError;
}

QString GmxPlugin::nameFilter() const
{
    return tr("GameMaker room files (*.room.gmx)");
}

QString GmxPlugin::shortName() const
{
    return QLatin1String("gmx");
}
