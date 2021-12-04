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
#include "bgximporterdialog.h"

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
static void writeOptionalProperty(QXmlStreamWriter &writer,
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

static QString colorToLongOle(QColor &c)
{

	return QString::number(c.red() + (c.green() * 256u) + (c.blue() * 256u * 256u) + (c.alpha()*256u*256u*256u));
}

// OLE = red + (green * 256) + (blue * 256 * 256)
static QColor oleToColor(int oleColor)
{
	int r = oleColor % 256;
	int g = (oleColor / 256) % 256;
	int b = (oleColor / 65536) % 256;
	return QColor(r,g,b);
}

//Long OLE = red + (green * 256) + (blue * 256 * 256) + (alpha * 256 * 256 * 256)
static QColor longOleToColor(uint oleColor)
{
	int r = oleColor % 256u;
	int g = (oleColor / 256u) % 256u;
	int b = (oleColor / 65536u) % 256u;
	int a = (oleColor / 16777216u) % 256u;
	return QColor(r,g,b,a);
}

static void wrt(const char *s, QIODevice *device,QTextCodec* codec)
{
    if (device)
            device->write(s, qint64(strlen(s)));
    else
        qWarning("QXmlStreamWriter: No device");
}

static void wrt(const QString &s,QIODevice *device,QTextCodec* codec)
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



GmxPlugin::GmxPlugin(QObject *parent)
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

static SharedTileset tilesetWithName(const QString &bgName, Map *map, QDir &imageDir)
{	
	QString imgPath = imageDir.absoluteFilePath(bgName + (".png"));
	QDir imgDir = QDir(imageDir.path());
	imgDir.cdUp();

	SharedTileset tst = TilesetManager::instance()->findTilesetAbsoluteWithSize(imgPath, map->tileSize());

	if(!tst.isNull())
	{
		map->addTileset(tst);
		return tst;
	}

	QString fname = QString(bgName);

	SharedTileset newTileset = Tileset::create(fname,map->tileWidth(),map->tileHeight(),0,0);

	if(newTileset->loadFromImage(imgPath))
    {

		newTileset->setFileName(imgPath);
		map->addTileset(newTileset);

		return newTileset;
    }

	return SharedTileset();

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

void GmxPlugin::writeAttribute(const QString &qualifiedName, QString &value, QIODevice* d, QTextCodec* codec)
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
	mError = QStringLiteral("");
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
		mError = tr("Operation cancelled");
        return nullptr;
    }

	appSettings->setValue(QStringLiteral("GMSMESizes/lastUsedMapTilesize"), QSize(settings.tileWidth, settings.tileHeigth));
	appSettings->setValue(QStringLiteral("GMSMESizes/LastUsedQuadSize"), QSize(settings.quadWidth, settings.quadHeigth));


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
	QColor bgColor = oleToColor(QString(root_node->first_node("colour")->value()).toInt());
    bool useBgColor = QString(root_node->first_node("showcolour")->value()).toInt() != 0;

    Map *newMap = new Map(Map::Orthogonal,mapWidth,mapHeight,tileWidth,tileHeight,false);
    newMap->setProperty("code",QVariant(root_node->first_node("code")->value()));
    newMap->setQuadWidth(settings.quadWidth);
    newMap->setQuadHeight(settings.quadHeigth);
	newMap->setProperty("enableViews", enableViews);
	newMap->setProperty("speed", roomSpeed);
	newMap->setProperty("combineTilesOnExport", true);

    if(useBgColor)
    {
		newMap->setBackgroundColor(bgColor);
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

		}
		qDebug()<<"Views Imported";

		gmBgLayer->setVisible(false);
		gmViewLayer->setVisible(false);
		newMap->addLayer(gmBgLayer);
		newMap->addLayer(gmViewLayer);
	}
	//VIEW/BG IMPORT END


    QVector<TileLayer*> *mapLayers = new QVector<TileLayer*>();
	//QVector<SharedTileset> *tilesets = new QVector<SharedTileset>();

	bool warnAboutSkippedTiles = false;

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

					//Some times tiles might be bigger than they should
					//and game maker won't do anything about it,
					//it will simple not draw the extra region
					//wo we need to make sure we don't wrap around the tileset
					//and just ignore the extra space
                }
                else
                {
					tile = tile->next_sibling();
					qDebug() << "Skipped tile with a tiles size that doesn't fit the map";
					warnAboutSkippedTiles = true;
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
			x = (x/tileWidth)*tileWidth;
			y = (y/tileHeight)*tileHeight;
            int depth = QString(tile->first_attribute("depth")->value()).toInt();
            QString bgName = QString(tile->first_attribute("bgName")->value());

            TileLayer *layer = tileLayerAtDepth(*mapLayers,depth,xoff, yoff,newMap);
            if(layer==nullptr)
            {
                tile = tile->next_sibling();
                continue;
            }
			SharedTileset tileset = tilesetWithName(bgName,newMap,imageDir);
			if(tileset.isNull())
            {
                tile = tile->next_sibling();
                continue;
            }
			if(htiles > 1)
			{
				int xInTileset = (xo/tileWidth);
				if(xInTileset + htiles-1 >= tileset->columnCount()) {
					htiles += tileset->columnCount() - 1 - (xInTileset + htiles-1);
					qDebug()<<"trimmed tile horizontally";
				}
			}
			if(vtiles > 1)
			{
				int yInTileset = (yo/tileHeight);
				if(yInTileset + vtiles -1 >= tileset->rowCount()){
					vtiles += tileset->rowCount() - 1 - (yInTileset + vtiles-1);
					qDebug()<<"trimmed tile vertically";
				}
			}
			int maxTileId = tileset->rowCount()*tileset->columnCount() - 1;
            for(int ht = 0;ht<htiles;++ht)
            {
                for(int vt=0; vt<vtiles;vt++)
				{
					int tileID = ((xo/tileWidth)+ht) + ((yo/tileHeight)+vt)*tileset->columnCount();
					Cell ncell = Cell();
					if(tileID > maxTileId)
					{
						qDebug() << "Tile with invalid tileId in tileset:" << tileset->name()
								<<", tst dims: " << QPoint(tileset->rowCount(), tileset->columnCount())
								<< ", tile pos(in tileset): " << QPoint(((xo/tileWidth)+ht) ,(yo/tileHeight)+vt)
								<<", at map pos:" << QPoint(x/tileWidth + ht, y/tileHeight+vt)
								   ;

					}
					else
					{
						ncell.setTile(tileset.get(),tileID);
						if(scaleX==-1)
							ncell.setFlippedHorizontally(true);
						if(scaleY==-1)
							ncell.setFlippedVertically(true);
						layer->setCell(x/tileWidth+ht, y/tileHeight+vt,ncell);
					}
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

		QDir imagesPath = QDir(settings.templatePath);
		imagesPath.cdUp();
		SharedTileset imagesTileset = TilesetManager::instance()->loadTileset(imagesPath.filePath(QString("images.tsx")));

        if(imagesTileset.get()!=nullptr)
            newMap->addTileset(imagesTileset);

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

				//Colors and locked
				auto auxAttr = instance->first_attribute("locked");
				bool locked = false;
				if(auxAttr && auxAttr->value() == QStringLiteral("-1"))
				{
					locked = true;
				}
				auxAttr = instance->first_attribute("colour");
				QColor color = QColor(255,255,255,255);
				if(auxAttr)
				{
					color = longOleToColor(QString(auxAttr->value()).toUInt());
				}
				obj->setProperty("colour", color);
				if(locked)
					obj->setProperty("locked", locked);

				//Make sure these don't get reloaded when it syncs with the template
				auto propsChanged = obj->changedProperties();
				auto changedProps = MapObject::Property::RotationProperty
								  | MapObject::Property::SizeProperty
								  | MapObject::Property::VisibleProperty
								  | MapObject::Property::CellProperty
								  | MapObject::Property::TypeProperty;
				propsChanged.setFlag((MapObject::Property)changedProps, true);
				obj->setChangedProperties(propsChanged);
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

	doc.clear();
    delete mapLayers;
	//delete tilesets;

	if(warnAboutSkippedTiles)
	{
		mError = tr("Some tiles were skipped because their size didn't fit the tile size");
	}

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

static void writeRoomProps(const Map* map, QXmlStreamWriter &stream)
{
	int mapPixelWidth = map->tileWidth() * map->width();
	int mapPixelHeight = map->tileHeight() * map->height();
	stream.writeTextElement("caption", QLatin1String(""));
	stream.writeTextElement("width", QString::number(mapPixelWidth));
	stream.writeTextElement("height", QString::number(mapPixelHeight));
	stream.writeTextElement("vsnap", QString::number(map->tileHeight()));
	stream.writeTextElement("hsnap", QString::number(map->tileWidth()));
	stream.writeTextElement("isometric", QString::number(0));

	writeOptionalProperty(stream, map, "speed", 60);
	writeOptionalProperty(stream, map, "persistent", false);
	stream.writeTextElement("colour", colorToOLE((QColor&)map->backgroundColor()));
	stream.writeTextElement("showcolour",toString(optionalProperty(map,"showcolour",true)));

	writeOptionalProperty(stream, map, "code", QString());

	writeOptionalProperty(stream, map, "enableViews", true);
	writeOptionalProperty(stream, map, "clearViewBackground", toString(true));
	writeOptionalProperty(stream, map, "clearDisplayBuffer", toString(true));

	stream.writeStartElement("makerSettings");
	   stream.writeTextElement("isSet",toString(0));
	   stream.writeTextElement("w", toString(0));
	   stream.writeTextElement("h", toString(0));
	   stream.writeTextElement("showGrid",toString(false));
	   stream.writeTextElement("showObjects",toString(true));
	   stream.writeTextElement("showTiles",toString(true));
	   stream.writeTextElement("showBackgrounds",toString(true));
	   stream.writeTextElement("showForegrounds",toString(true));
	   stream.writeTextElement("showViews",toString(false));
	   stream.writeTextElement("deleteUnderlyingObj",toString(false));
	   stream.writeTextElement("deleteUnderlyingTiles",toString(false));
	   stream.writeTextElement("page",toString(0));
	   stream.writeTextElement("xoffset",toString(0));
	   stream.writeTextElement("yoffset",toString(0));
	stream.writeEndElement();
}

static void writeViews(QXmlStreamWriter &stream, const Map *map, std::vector<const Layer*> &layers)
{
	// Write out views
	if (true) {
		//Write backgrounds
		stream.writeStartElement("backgrounds");

		int bgCount = 0;

		for(uint i=0; i<layers.size(); ++i){
			const Layer *layer = layers[i];
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

		stream.writeEndElement();//backgrounds

		//Write views
		stream.writeStartElement("views");
		int viewCount = 0;

		for(uint i=0; i<layers.size(); ++i){
			const Layer *layer = layers[i];

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
	}
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

	//Prepare layers for iteration
	std::vector<const Layer*> layers;
	layers.reserve(32);
	{
		LayerIterator iterator(map);
		while (const Layer *layer = iterator.next()) {
			layers.push_back(layer);
		}
	}

	bool combineTiles = optionalProperty(map, QStringLiteral("combineTilesOnExport"), true);

	//Game maker outputs tiles ordered by their depth in descending order
	//and instance id in ascending order
	std::sort(layers.begin(), layers.end(),
	[](const Layer *a, const Layer *b)
	{
		int depthA = 0;
		int depthB = 0;
		auto auxProp = a->property(QStringLiteral("depth"));
		if(a->isTileLayer())
		{
			depthA	= 1000000;
		}
		if(auxProp.isValid() && auxProp.type() == QVariant::Int)
		{
			depthA = auxProp.toInt();
		}

		auxProp = b->property(QStringLiteral("depth"));
		if(b->isTileLayer())
		{
			depthB	= 1000000;
		}
		if(auxProp.isValid() && auxProp.type() == QVariant::Int)
		{
			depthB = auxProp.toInt();
		}

		return depthA > depthB;
	});

	QXmlStreamWriter stream;
	stream.setDevice(file.device());

	stream.device()->write("<!--This Document is generated by GameMaker, if you edit it by hand then you do so at your own risk!-->");
	
    stream.setAutoFormatting(true);
    stream.setAutoFormattingIndent(2);
	stream.writeStartElement("room");

	writeRoomProps(map, stream);
	writeViews(stream, map, layers);

	//Global instance id for everything written out
	uint instId = 100000u;//In GM They start at 100000 for some reason

	// Write out object instances
	stream.writeStartElement("instances");
	for(uint i=0; i<layers.size(); ++i){
		const Layer *layer = layers[i];
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

			//Unique instance name
			QString instIdStr = QString::number(++instId);
			stream.writeAttribute("name", QStringLiteral("inst_")+instIdStr);

			stream.writeAttribute("locked", toString(optionalProperty(object, QStringLiteral("locked"), false)));

			//Custom add atribute
			QString cc2 = optionalProperty(object, "code", QString());

			writeAttribute(QStringLiteral("code"),cc2,(QIODevice*)stream.device(),(QTextCodec*)stream.codec());
			//stream.writeAttribute(QStringLiteral("code"), cc2);
			//stream.writeAttribute("code", optionalProperty(object, "code", QString()));
			stream.writeAttribute("scaleX", QString::number(scaleX));
			stream.writeAttribute("scaleY", QString::number(scaleY));
			QColor color = optionalProperty(object, QStringLiteral("colour"), QColor(255,255,255,255));
			QString colorStr = colorToLongOle(color);
			stream.writeAttribute("colour", colorStr);
			stream.writeAttribute("rotation", QString::number(-object->rotation()));

			stream.writeEndElement();

		}
	}
	stream.writeEndElement(); //Instances
	
	// Write out tile instances
	stream.writeStartElement("tiles");
    int depthOff = 0;
    int layerCount = 0;
	std::unordered_map <long, QPoint> processedTiles;
	processedTiles.reserve((map->width()*map->height())/2);


	for(uint i=0; i<layers.size(); ++i){
		const Layer *layer = layers[i];
        --layerCount;
		QString depth = QString::number(optionalProperty(layer, QLatin1String("depth"), 1000000 - depthOff));

        int xoff = layer->offset().x();
        int yoff = layer->offset().y();

        switch (layer->layerType()) {
        case Layer::TileLayerType: {
            ++depthOff;
            auto tileLayer = static_cast<const TileLayer*>(layer);
			int layerWidth = tileLayer->width();
			int layerHeight = tileLayer->height();
			for (int y = 0; y < layerHeight; ++y)
			{
				for (int x = 0; x < layerWidth; ++x)
				{
					//Skip processed tiles
					if(combineTiles && processedTiles.count(x + y*layerWidth) > 0)
					{
						continue;
					}

                    const Cell &cell = tileLayer->cellAt(x, y);

                    if (const Tile *tile = cell.tile()) {
                        const Tileset *tileset = tile->tileset();

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
						int tileWidth = tile->width();
						int tileHeight = tile->height();

						if (tileset->isCollection()) {
							bgName = QFileInfo(tile->imageSource().path()).baseName();
						} else {
							bgName = tileset->name().split(".",QString::SkipEmptyParts).at(0);
							int tstColumns = tileset->columnCount();
							int tstRows = tileset->rowCount();

							int xInTilesetGrid = tile->id() % tileset->columnCount();
							int yInTilesetGrid = (tile->id() / tileset->columnCount());

							xo = tileset->margin() + (tileset->tileSpacing() + tileset->tileWidth()) * xInTilesetGrid;
							yo = tileset->margin() + (tileset->tileSpacing() + tileset->tileHeight()) * yInTilesetGrid;

							//We only combine these for now to keep it simple
							if(combineTiles && scaleX == 1 && scaleY == 1
							&& (tileset->margin() == 0) && (tileset->tileSpacing()) == 0
							&& (tile->width() == map->tileWidth()) && (tile->height() == map->tileHeight()))
							{
								int maxCheckXOff = tstColumns-1 - xInTilesetGrid;
								int maxCheckYOff = tstRows-1 - yInTilesetGrid;

								int combineXRight = 0;
								int combineYBottom = 0;
								int combineArea = 1;//in tiles
								int currentArea = 1;

								int checkX = 1;
								int checkY = 0;
								bool rowJump = false;
								if(checkX > maxCheckXOff || (checkX + x) >= layerWidth)
								{
									checkX = 0;
									++checkY;
									rowJump = true;
								}

								bool prevAccepted = true;

								for(;checkY <= maxCheckYOff && (checkY + y) < layerHeight; ++checkY)
								{

									for(; checkX <= maxCheckXOff && (checkX	+ x) < layerWidth; ++checkX)
									{

										const Cell &sCell = tileLayer->cellAt(x + checkX,y + checkY);
										bool acceptedTile = false;
										if(const Tile *sTile = sCell.tile())
										{
											if(processedTiles.count((checkX + x) + (checkY+y)*layerWidth) == 0)
											{
												const Tileset *sTileset = sTile->tileset();

												if(sTileset == tileset
												&& (sTile->width() == map->tileWidth()) && (sTile->height() == map->tileHeight())
												&& !sCell.flippedVertically() && !sCell.flippedHorizontally())
												{
													int sTileXInGrid = sTile->id() % tstColumns;
													int sTileYInGrid = (int)(sTile->id() / tstColumns);

													if( (sTileXInGrid == (xInTilesetGrid + checkX))
													&& (sTileYInGrid == (yInTilesetGrid + checkY)))
													{
														currentArea = ((checkX+1)*(checkY+1));

														acceptedTile = true;
														if(currentArea > combineArea)
														{
															combineArea = currentArea;
															combineXRight = checkX;
															combineYBottom = checkY;


														}
													}
												}
											}
										}
										rowJump = false;
										if(!acceptedTile)
										{
											if(checkX > 0)
												maxCheckXOff = checkX-1;
											else
												maxCheckXOff = 0;
											if(!prevAccepted)
											{
												maxCheckXOff = -1;
												maxCheckYOff = -1;
											}

											prevAccepted = false;
											break;
										}
										else
										{
											prevAccepted = true;
										}

									}

									checkX = 0;
									rowJump = true;
								}

								if(combineArea > 1)
								{
									//Final step
									checkX = 1;
									for(checkY = 0 ;checkY <= combineYBottom && (checkY + y) < layerHeight; ++checkY)
									{

										for(; checkX <= combineXRight && (checkX + x) < layerWidth; ++checkX)
										{
											processedTiles.emplace(
														(x+checkX) + (y+checkY)*layerWidth,
														QPoint(checkX + x, checkY + y));
										}

										checkX	= 0;
									}
									tileWidth = (combineXRight + 1)*tile->width();
									tileHeight = (combineYBottom+ 1)*tile->height();
									if(!(tileWidth/tile->width())*(tileHeight/tile->height()) == combineArea )
									{
										qDebug() << "Fail";
									}
								}
							}
							else //No Combine
							{
								if(tile->width() > map->tileWidth())
								{
									pixelX = x*map->tileWidth();
									if(cell.flippedHorizontally())
									{
										pixelX += map->tileWidth();
									}

								}
								if(tile->height() > map->tileHeight())
								{
									pixelY = y*map->tileHeight();

									pixelY -= tile->height() - map->tileHeight();
									if(cell.flippedVertically())
									{
										pixelY += map->tileHeight();
									}

								}
							}
						}

						stream.writeAttribute("bgName", bgName);
						stream.writeAttribute("x", QString::number(pixelX+xoff));
						stream.writeAttribute("y", QString::number(pixelY+yoff));
						stream.writeAttribute("w", QString::number(tileWidth));
						stream.writeAttribute("h", QString::number(tileHeight));

						stream.writeAttribute("xo", QString::number(xo));
						stream.writeAttribute("yo", QString::number(yo));

						QString tileIdStr = QString::number(++instId);
						stream.writeAttribute("id", tileIdStr);
						stream.writeAttribute("name", QStringLiteral("inst_") + tileIdStr);
						stream.writeAttribute("depth", depth);
						stream.writeAttribute("locked","0");
						stream.writeAttribute("colour", QStringLiteral("4294967295"));

						stream.writeAttribute("scaleX", QString::number(scaleX));
						stream.writeAttribute("scaleY", QString::number(scaleY));

						stream.writeEndElement();
                    }
                }

            }

			processedTiles.clear();
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
						bgName = tileset->name().split(".",QString::SkipEmptyParts).at(0);;

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

					QString tileIdStr = QString::number(++instId);
					stream.writeAttribute("id", tileIdStr);
					stream.writeAttribute("name", QStringLiteral("inst_") + tileIdStr);
					stream.writeAttribute("depth", depth);
					stream.writeAttribute("locked","0");
					stream.writeAttribute("colour", QStringLiteral("4294967295"));

					stream.writeAttribute("scaleX", QString::number(scaleX));
					stream.writeAttribute("scaleY", QString::number(scaleY));

                    stream.writeEndElement();
                }
            }
            break;
        }

        case Layer::ImageLayerType:
            break;

        case Layer::GroupLayerType:
			//Sub layers added through the layer iterator to our layers vector
            break;
        }
    }
    stream.writeEndElement();

	//Todo store these in read() and restore them as optional properties
	writeOptionalProperty(stream, map, "PhysicsWorld", false);
	writeOptionalProperty(stream, map, "PhysicsWorldTop", 0);
	writeOptionalProperty(stream, map, "PhysicsWorldLeft", 0);
	writeOptionalProperty(stream, map, "PhysicsWorldRight", mapPixelWidth);
	writeOptionalProperty(stream, map, "PhysicsWorldBottom", mapPixelHeight);
	writeOptionalProperty(stream, map, "PhysicsWorldGravityX", 0.0);
	writeOptionalProperty(stream, map, "PhysicsWorldGravityY", 10.0);
	writeOptionalProperty(stream, map, "PhysicsWorldPixToMeters", 0.1);

    stream.writeEndDocument();

    if (!file.commit()) {
        mError = file.errorString();
        return false;
    }

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
	return QLatin1String("roomgmx");
}

//Tilesets

GmxTilesetPlugin::GmxTilesetPlugin(QObject *parent)
{

}

SharedTileset GmxTilesetPlugin::tstFromPng(const QString &fileName, QSettings *prefs)
{
	QFile file(fileName);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		mError = tr("Could not open file for reading.");
		return SharedTileset();
	}
	file.close();

	using namespace std;
	using namespace Tiled;
	int tileWidth = 16;
	int tileHeight = 16;

	if(prefs != nullptr)
	{
		auto diag = new BGXImporterDialog();
		diag->SetDefaultsFromSettings(prefs);
		diag->exec();
		if(!diag->accepted)
		{
			mError = tr("Operation cancelled");
			return SharedTileset();
		}
		else
		{
			tileWidth = diag->tileSize.width();
			tileHeight = diag->tileSize.height();

			prefs->setValue(QLatin1String("GMSMESizes/LastUsedTilesetTileSize"), QSize(tileWidth,tileHeight));
		}
	}
	QFile imgFile(fileName);
	if(!imgFile.exists())
	{
		mError = tr("Invalid background, image file doesn't exist");
		qDebug() << imgFile.fileName();
		return SharedTileset();
	}

	SharedTileset sh = TilesetManager::instance()->findTilesetAbsoluteWithSize(fileName,QSize(tileWidth, tileHeight));
	if(!sh.isNull())
	{
		qDebug() << "reused tileset";
		return sh;
	}

	QFileInfo imgFileInfo = QFileInfo(imgFile);

	SharedTileset tileset = Tileset::create(imgFileInfo.fileName(), tileWidth, tileHeight, 0, 0);

	if(tileset->loadFromImage(fileName))
	{
		tileset->setGridSize(QSize(tileWidth, tileHeight));
		tileset->setFileName(fileName);
		qDebug()<<"New tileset " << imgFileInfo.fileName() << ", " << tileset->name() << ", tw:"<<tileWidth << ", th:"<<tileHeight;
		return tileset;
	}
	return SharedTileset();

	return SharedTileset();
}

SharedTileset GmxTilesetPlugin::read(const QString &fileName, QSettings *prefs)
{
	if(fileName.endsWith(".png"))
	{
		return tstFromPng(fileName, prefs);
	}
	using namespace rapidxml;
	using namespace std;
	using namespace Tiled;

	QFile file(fileName);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		mError = tr("Could not open file for reading.");
		return SharedTileset();
	}
	file.close();

	bool loadTileSizeFromBg = true;
	int tileWidth = 16;
	int tileHeight = 16;

	if(prefs != nullptr)
	{
		auto diag = new BGXImporterDialog();
		diag->SetDefaultsFromSettings(prefs);
		diag->exec();
		if(!diag->accepted)
		{
			mError = tr("Operation cancelled");
			return SharedTileset();
		}
		else
		{
			loadTileSizeFromBg = diag->loadFromBg;
			if(!loadTileSizeFromBg)
			{
				tileWidth = diag->tileSize.width();
				tileHeight = diag->tileSize.height();

				prefs->setValue(QLatin1String("GMSMESizes/LastUsedTilesetTileSize"), QSize(tileWidth,tileHeight));
			}
		}
	}

	QDir bgDir = QDir(fileName);
	bgDir.cdUp();
	qDebug() << "BgPath: "<<bgDir.path();

	xml_document<> doc;

	ifstream theFile(fileName.toStdString().c_str());
	vector<char> buffer((istreambuf_iterator<char>(theFile)), istreambuf_iterator<char>());
	buffer.push_back('\0');
	doc.parse<0>(&buffer[0]);
	//file.close();

	xml_node<>* root_node = doc.first_node("background");
	if(!root_node)
	{
		doc.clear();
		mError = tr("Invalid background, no root node");
		return SharedTileset();
	}

	xml_node<> *node = root_node->first_node("istileset");


	int tileYOff = 0;
	int tileXOff = 0;
	//int tileHSep = 0;
	//int tileVSep = 0;
	bool isTileset = true;
	if(node)
		isTileset = node->value() == QStringLiteral("-1") ? true : false;
	if(!isTileset)
	{
		mError = tr("Invalid background, not a tileset");
		doc.clear();
		return SharedTileset();
	}

	//node = root_node->first_node();
	QString filePath;


	while(node)
	{
		if(loadTileSizeFromBg)
		{
			if(node->name() == QStringLiteral("tilewidth"))
			{
				tileWidth = QString(node->value()).toInt();
			}
			else if(node->name() == QStringLiteral("tileheight"))
			{
				tileHeight = QString(node->value()).toInt();
			}
			else if(node->name() == QStringLiteral("tilexoff"))
			{
				tileXOff = QString(node->value()).toInt();
			}
			else if(node->name() == QStringLiteral("tileyoff"))
			{
				tileYOff = QString(node->value()).toInt();
			}
		}
				/*
		else if(node->name() == QStringLiteral("tilehsep"))
		{
			tileHSep = QString(node->value()).toInt();
		}
		else if(node->name() == QStringLiteral("tilevsep"))
		{
			tileVSep = QString(node->value()).toInt();
		}*/
		else if(node->name() == QStringLiteral("data"))
		{
			filePath = node->value();
			filePath.replace('\\','/');

			filePath = bgDir.filePath(filePath);
			qDebug()<<filePath;
		}

		node = node->next_sibling();
	}

	doc.clear();

	QFile imgFile(filePath);
	if(!imgFile.exists())
	{
		mError = tr("Invalid background, image file doesn't exist");
		qDebug() << imgFile.fileName();
		return SharedTileset();
	}

	SharedTileset sh = TilesetManager::instance()->findTilesetAbsoluteWithSize(filePath,QSize(tileWidth, tileHeight));
	if(!sh.isNull())
	{
		qDebug() << "reused tileset";
		return sh;
	}

	QFileInfo imgFileInfo = QFileInfo(imgFile);

	SharedTileset tileset = Tileset::create(imgFileInfo.fileName(), tileWidth, tileHeight, 0, 0);
	tileset->setTileOffset(QPoint(tileXOff, tileYOff));
	if(tileset->loadFromImage(filePath))
	{
		tileset->setGridSize(QSize(tileWidth, tileHeight));
		tileset->setFileName(filePath);
		qDebug()<<"New tileset " << imgFileInfo.fileName() << ", " << tileset->name() << ", tw:"<<tileWidth << ", th:"<<tileHeight;
		return tileset;
	}
	return SharedTileset();



}

bool GmxTilesetPlugin::write(const Tileset &tst, const QString &filename)
{
	return false;
}

bool GmxTilesetPlugin::supportsFile(const QString &fileName) const
{

	return fileName.endsWith(QStringLiteral(".background.gmx")) || fileName.endsWith(QStringLiteral(".png"));
}

QString GmxTilesetPlugin::errorString() const
{
	return mError;
}

QString GmxTilesetPlugin::shortName() const
{
	return QStringLiteral("gmxbg");
}

FileFormat::Capabilities GmxTilesetPlugin::capabilities() const
{
	return Capability::Read;
}

QString GmxTilesetPlugin::nameFilter() const
{
	return QStringLiteral("GameMaker background files (*.background.gmx, *.png)");
}

void GmxTopPlugin::initialize()
{
	addObject(new GmxPlugin(this));
	addObject(new GmxTilesetPlugin(this));
}
