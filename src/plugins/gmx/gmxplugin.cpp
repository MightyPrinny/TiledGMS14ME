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
#include <QFileInfo>
#include <QDirIterator>

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
#include <fstream>
#include <QFileDialog>
#include <string.h>
#include <unordered_map>

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
static void write(char *s, QIODevice *device,QTextCodec* codec)
{
    if (device)
            device->write(s, strlen(s));
    else
        qWarning("QXmlStreamWriter: No device");
}

static void write(QString &s,QIODevice *device,QTextCodec* codec)
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
static void writeAttribute(QString &qualifiedName, QString &value, QIODevice* d, QTextCodec* codec)
{
    write(" ",d,codec);
    write(qualifiedName,d,codec);
    write("=\"",d,codec);
    QString escaped = getEscaped(value, true);
    write(escaped,d,codec);
    write("\"",d,codec);
}


static bool checkIfViewsDefined(const Map *map)
{
    LayerIterator iterator(map);
    while (const Layer *layer = iterator.next()) {

        if (layer->layerType() != Layer::ObjectGroupType)
            continue;

        const ObjectGroup *objectLayer = static_cast<const ObjectGroup*>(layer);

        for (const MapObject *object : objectLayer->objects()) {
            const QString type = object->effectiveType();
            if (type == "view")
                return true;
        }
    }

    return false;
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

static void mapTemplates(std::unordered_map<std::string,std::string> *map, QDir &root )
{
    QFileInfoList fileInfoList = root.entryInfoList();
    qDebug()<<root.path();

    QDirIterator it(root.path(), QStringList() << "*.tx", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        QFileInfo info = it.fileInfo();
        if(info.isFile())
        {
            map->emplace(make_pair(info.baseName().toStdString(),info.filePath().toStdString()));
        }
        it.next();
    }
}


GmxPlugin::GmxPlugin()
{
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
    if(newTileset->loadFromImage(imageDir.filePath(bgName.append(".png"))))
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

    QVariant aux = lay1->inheritedProperty("depth");
    if(aux.isValid())
        depth1=aux.toInt();
    aux = lay2->inheritedProperty("depth");
    if(aux.isValid())
        depth2 = aux.toInt();
    return depth1<depth2;
}

Tiled::Map *GmxPlugin::read(const QString &fileName)
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
    sDialog->exec();

    delete sDialog;
    if(!accepted)
    {
        return nullptr;
    }
    xml_document<> doc;
    xml_node<> * root_node;

    ifstream theFile(fileName.toStdString().c_str());
    vector<char> buffer((istreambuf_iterator<char>(theFile)), istreambuf_iterator<char>());
    buffer.push_back('\0');

    doc.parse<0>(&buffer[0]);
    root_node = doc.first_node("room");
    xml_node<> *tile = root_node->first_node("tiles")->first_node("tile");
    xml_node<> *instance = root_node->first_node("instances")->first_node("instance");
    int tileWidth = settings.tileWidth;
    int tileHeight = settings.tileHeigth;
    int mapWidth = QString(root_node->first_node("width")->value()).toInt()/tileWidth;
    int mapHeight = QString(root_node->first_node("height")->value()).toInt()/tileHeight;
    int bgColor = QString(root_node->first_node("colour")->value()).toInt();
    bool useBgColor = QString(root_node->first_node("showcolour")->value()).toInt() != 0;

    Map *newMap = new Map(Map::Orthogonal,mapWidth,mapHeight,tileWidth,tileHeight,0);
    newMap->setProperty("code",QVariant(root_node->first_node("code")->value()));
    newMap->setQuadWidth(settings.quadWidth);
    newMap->setQuadHeight(settings.quadHeigth);
    if(useBgColor)
    {
        int r = bgColor % 256;
        int g = (bgColor / 256) % 256;
        int b = (bgColor / 65536) % 256;
        newMap->setBackgroundColor(QColor(r,g,b));
    }
    //Import tiles
    QVector<TileLayer*> *mapLayers = new QVector<TileLayer*>();
    QVector<SharedTileset> *tilesets = new QVector<SharedTileset>();
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
            int xoff = x%tileWidth;
            int yoff = y%tileHeight;
            x = floor(x/tileWidth)*tileWidth;
            y = floor(y/tileHeight)*tileHeight;
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
                    layer->setCell(floor(x/tileWidth)+ht,floor(y/tileHeight)+vt,Cell(addtile));
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
        unordered_map<string,string> *templateMap = new unordered_map<string,string> ();
        mapTemplates(templateMap,QDir(settings.templatePath.append(QString("/"))));
        //Add view object if views are enabled
        if(root_node->first_node("enableViews"))
        {
            if(QString(root_node->first_node("enableViews")->value()).toInt()!=0)
            {
                MapObject *obj = new MapObject("View","view",QPointF(0,0),QSizeF(16,16));
                xml_node<> *viewNode = root_node->first_node("views")->first_node("view");
                if(viewNode)
                {
                    int xview = QString(viewNode->first_attribute("xview")->value()).toInt();
                    int yview = QString(viewNode->first_attribute("yview")->value()).toInt();
                    int wview = QString(viewNode->first_attribute("wview")->value()).toInt();
                    int hview = QString(viewNode->first_attribute("hview")->value()).toInt();
                    int xport = QString(viewNode->first_attribute("xport")->value()).toInt();
                    int yport = QString(viewNode->first_attribute("yport")->value()).toInt();
                    int wport = QString(viewNode->first_attribute("wport")->value()).toInt();
                    int hport = QString(viewNode->first_attribute("hport")->value()).toInt();
                    int hborder = QString(viewNode->first_attribute("hborder")->value()).toInt();
                    int vborder = QString(viewNode->first_attribute("vborder")->value()).toInt();
                    obj->setProperty("xview",QVariant(xview));
                    obj->setProperty("yview",QVariant(yview));
                    obj->setProperty("wview",QVariant(wview));
                    obj->setProperty("hview",QVariant(hview));
                    obj->setProperty("xport",QVariant(xport));
                    obj->setProperty("yport",QVariant(yport));
                    obj->setProperty("wport",QVariant(wport));
                    obj->setProperty("hport",QVariant(hport));
                    obj->setProperty("hborder",QVariant(hborder));
                    obj->setProperty("vborder",QVariant(vborder));
                }
                objects->addObject(obj);


            }
        }



        QDir *imagesPath = new QDir(settings.templatePath);
        imagesPath->cdUp();
        SharedTileset imagesTileset = TilesetManager::instance()->loadTileset(imagesPath->filePath(QString("images.tsx")));
        if(imagesTileset.get()!=nullptr)
            newMap->addTileset(imagesTileset);
        delete imagesPath;
        for(instance; instance; instance=instance->next_sibling())
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

            if(templ!=nullptr&&templ->object()!=nullptr&&templ->object()->inheritedProperty(QString("originX")).isValid())
            {
                QVariant aux = templ->object()->inheritedProperty(QString("originX"));
                int originX = aux.toInt();
                aux = templ->object()->inheritedProperty(QString("originY"));
                int originY = aux.toInt();

                double rotation = QString(instance->first_attribute("rotation")->value()).toDouble() *-1;
                double scaleX = QString(instance->first_attribute("scaleX")->value()).toDouble();
                double scaleY = QString(instance->first_attribute("scaleY")->value()).toDouble();
                if(scaleX<0)
                {
                    originX = templ->object()->width() - originX;
                }
                if(scaleY<0)
                {
                    originY = templ->object()->height() - originY;
                }
                QPointF origin = QPointF(-originX*abs(scaleX),-originY*abs(scaleY));
                if(templ->object()->isTileObject())
                    origin.setY(origin.y()+(templ->object()->height()*abs(scaleY)));
                QTransform transform;
                transform.rotate(rotation);
                pos += transform.map(origin);

                MapObject *obj = new MapObject(QString(""),objName,pos,QSizeF(templ->object()->width()*abs(scaleX),templ->object()->height()*abs(scaleY)));
                obj->setObjectTemplate(templ);
                obj->setProperty(QString("code"),code);
                obj->syncWithTemplate();
                if(scaleX<0)
                    obj->flip(FlipHorizontally,pos);
                if(scaleY<0)
                    obj->flip(FlipVertically,pos);
                obj->setRotation(rotation);
                obj->setWidth(templ->object()->width()*abs(scaleX));
                obj->setHeight(templ->object()->height()*abs(scaleY));
                objects->addObject(obj);
            }
            else
            {
                qDebug()<<QString("Bad template or file doesn't exist: ").append(tempath).toStdString().c_str();
                continue;
            }
        }
        newMap->addLayer(objects);
        qDebug()<<"Done";
        delete templateMap;
    }

    QList<Layer*> mLayers = newMap->layers();
    std::sort(mLayers.begin(),mLayers.end(),lesThanLayer);
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

bool GmxPlugin::write(const Map *map, const QString &fileName)
{
    SaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        mError = tr("Could not open file for writing.");
        return false;
    }

    QXmlStreamWriter stream(file.device());

    stream.writeComment("This Document is generated by Tiled, if you edit it by hand then you do so at your own risk!");
    stream.setAutoFormatting(true);
    stream.setAutoFormattingIndent(2);
    stream.writeStartElement("room");


    int mapPixelWidth = map->tileWidth() * map->width();
    int mapPixelHeight = map->tileHeight() * map->height();


    stream.writeTextElement("caption", QLatin1String(""));
    stream.writeTextElement("width", QString::number(mapPixelWidth));
    stream.writeTextElement("height", QString::number(mapPixelHeight));
    stream.writeTextElement("vsnap", QString::number(map->tileHeight()));
    stream.writeTextElement("hsnap", QString::number(map->tileWidth()));
    stream.writeTextElement("isometric", QString::number(0));
    writeProperty(stream, map, "speed", 60);
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

    //Megamix Engine
    stream.writeStartElement("backgrounds");
    stream.writeStartElement("background");

    stream.writeAttribute("visible",QString("-1"));
    stream.writeAttribute("foreground",QString("-1"));
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
    stream.writeAttribute("name",bgquad);
    stream.writeAttribute("x",QString("0"));
    stream.writeAttribute("y",QString("0"));
    stream.writeAttribute("htiled",QString("-1"));
    stream.writeAttribute("vtiled",QString("-1"));
    stream.writeAttribute("hspeed",QString("0"));
    stream.writeAttribute("vspeed",QString("0"));
    stream.writeAttribute("stretch",QString("0"));

    stream.writeEndElement();
    stream.writeEndElement();

    quint64 instanceMark=0;

    // Write out views
    // Last view in Object layer is the first view in the room
    LayerIterator iterator(map);
    if (enableViews) {
        stream.writeStartElement("views");
        int viewCount = 0;
        while (const Layer *layer = iterator.next()) {

            if (layer->layerType() != Layer::ObjectGroupType)
                continue;

            const ObjectGroup *objectLayer = static_cast<const ObjectGroup*>(layer);

            for (const MapObject *object : objectLayer->objects()) {
                const QString type = object->effectiveType();
                if (type != "view")
                    continue;

                // GM only has 8 views so drop anything more than that
                if (viewCount > 7)
                    break;

                viewCount++;
                stream.writeStartElement("view");

                stream.writeAttribute("visible", toString(object->isVisible()));
                stream.writeAttribute("objName", QString(optionalProperty(object, "objName", QString())));
                QPointF pos = object->position();
                // Note: GM only supports ints for positioning
                // so views could be off if user doesn't align to whole number
                stream.writeAttribute("xview", QString::number(qRound(pos.x())));
                stream.writeAttribute("yview", QString::number(qRound(pos.y())));
                stream.writeAttribute("wview", QString::number(qRound(optionalProperty(object, "wview", 256.0))));
                stream.writeAttribute("hview", QString::number(qRound(optionalProperty(object, "hview", 224.0))));
                // Round these incase user adds properties as floats and not ints
                stream.writeAttribute("xport", QString::number(qRound(optionalProperty(object, "xport", 0.0))));
                stream.writeAttribute("yport", QString::number(qRound(optionalProperty(object, "yport", 0.0))));
                stream.writeAttribute("wport", QString::number(qRound(optionalProperty(object, "wport", 256.0))));
                stream.writeAttribute("hport", QString::number(qRound(optionalProperty(object, "hport", 224.0))));
                stream.writeAttribute("hborder", QString::number(qRound(optionalProperty(object, "hborder", 32.0))));
                stream.writeAttribute("vborder", QString::number(qRound(optionalProperty(object, "vborder", 32.0))));
                stream.writeAttribute("hspeed", QString::number(qRound(optionalProperty(object, "hspeed", -1.0))));
                stream.writeAttribute("vspeed", QString::number(qRound(optionalProperty(object, "vspeed", -1.0))));

                stream.writeEndElement();
            }
        }

        stream.writeEndElement();
    }



    iterator.toFront();

    stream.writeStartElement("tiles");

    uint tileId = 0u;

    // Write out tile instances
    iterator.toBack();

    QVector<AnimationLayer> *animationLayers = new QVector<AnimationLayer>();
    int lastAnimLayerDepth = 2000000;
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
                            int animLayer = 2000000;
                            int animLength = tile->frames().size();
                            QVariant optionalLayer = tile->inheritedProperty(QLatin1String("animLayer"));
                            if(optionalLayer.isValid())
                            {
                                animLayer = optionalLayer.toInt();
                            }
                            else
                            {
                                QVector<int> animSpeeds = QVector<int>();
                                for(int j=0;j<animLength;++j)
                                {
                                    animSpeeds.append(tile->frames().at(j).duration);
                                }
                                int layerID = indexOfAnim(animLength,animSpeeds,animationLayers);
                                if(layerID!=-1)
                                {
                                    animLayer = animationLayers->at(layerID).depth;
                                }
                                else
                                {
                                    AnimationLayer newLayer = {
                                        (lastAnimLayerDepth+1),
                                        animSpeeds,
                                        animLength
                                    };
                                    lastAnimLayerDepth += animLength;
                                    animLayer = newLayer.depth;
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

                            bgName = tileset->name();

                            for(int t =0; t<animLength;t++)
                            {
                                int xInTilesetGrid = tile->frames().at(t).tileId % tileset->columnCount();
                                int yInTilesetGrid = (int)(tile->frames().at(t).tileId / tileset->columnCount());

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

    iterator.toFront();
    stream.writeStartElement("instances");

    QSet<QString> usedNames;

    // Write out object instances
    while (const Layer *layer = iterator.next()) {

        if (layer->layerType() != Layer::ObjectGroupType)
            continue;

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
                        origin += QPointF(object->width() - 2 * origin.x(), 0);
                    }
                    if (object->cell().flippedVertically()) {
                        scaleY *= -1;
                        origin += QPointF(0, object->height() - 2 * origin.y());
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
            writeAttribute(QString("code"),optionalProperty(object, "code", QString()),(QIODevice*)stream.device(),(QTextCodec*)stream.codec());
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
            int animSpeed = (animationLayers->at(anim).animSpeeds.at(frame) * 0.001)*60;
            cCode.append(QString("\nanimTime[").append(QString::number(frame)).append(QString("] = ").append(QString::number(animSpeed)).append(QString(";"))));
        }

        //Custom add atribute
        writeAttribute(QString("code"),cCode,(QIODevice*)stream.device(),(QTextCodec*)stream.codec());
        //stream.writeAttribute("code", optionalProperty(object, "code", QString()));
        stream.writeAttribute("scaleX", QString::number(1));
        stream.writeAttribute("scaleY", QString::number(1));
        stream.writeAttribute("rotation", QString::number(0));

        stream.writeEndElement();

    }

    stream.writeEndElement();

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
