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
#include "objectgroup.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamWriter>
#include <sstream>
#include <iostream>
#include <iomanip>
#include "qtcompat_p.h"
#include <vector>
#include <QTextCodec>
#include <QTextDocument>
#include "pugixml.hpp"
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

GmxPlugin::GmxPlugin()
{
}

bool GmxPlugin::write(const Map *map, const QString &fileName)
{
    using namespace pugi;
    SaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        mError = tr("Could not open file for writing.");
        return false;
    }

    //file.commit();
    QXmlStreamWriter stream(file.device());

    /*xml_document* doc = new xml_document();
    xml_node comment = doc->append_child(node_comment);
    comment.set_value("This Document is generated by Tiled, if you edit it by hand then you do so at your own risk!");

    xml_node room;
    */

    stream.writeComment("This Document is generated by Tiled, if you edit it by hand then you do so at your own risk!");
    stream.setAutoFormatting(true);
    stream.setAutoFormattingIndent(2);
    stream.writeStartElement("room");

    //room = doc->append_child("room");

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
   stream.writeTextElement("showGrid",toString(true));
   stream.writeTextElement("showObjects",toString(true));
   stream.writeTextElement("showTiles",toString(true));
   stream.writeTextElement("showBackgrounds",toString(true));
   stream.writeTextElement("showForegrounds",toString(true));
   stream.writeTextElement("showViews",toString(true));
   stream.writeTextElement("deleteUnderlyingObj",toString(true));
   stream.writeTextElement("deleteUnderlyingTiles",toString(true));
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

    iterator.toFront();
    stream.writeStartElement("instances");

    QSet<QString> usedNames;
    int layerCount = 0;

    // Write out object instances
    while (const Layer *layer = iterator.next()) {
        ++layerCount;

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

    stream.writeEndElement();


    stream.writeStartElement("tiles");

    uint tileId = 0u;

    // Write out tile instances
    iterator.toFront();
    while (const Layer *layer = iterator.next()) {
        --layerCount;
        QString depth = QString::number(optionalProperty(layer, QLatin1String("depth"),
                                                         layerCount + 1000000));

        switch (layer->layerType()) {
        case Layer::TileLayerType: {
            auto tileLayer = static_cast<const TileLayer*>(layer);
            for (int y = 0; y < tileLayer->height(); ++y) {
                for (int x = 0; x < tileLayer->width(); ++x) {
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
                        stream.writeAttribute("x", QString::number(pixelX));
                        stream.writeAttribute("y", QString::number(pixelY));
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
