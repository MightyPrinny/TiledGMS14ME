/*
 * mapobject.cpp
 * Copyright 2008-2013, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 * Copyright 2008, Roderic Morris <roderic@ccs.neu.edu>
 * Copyright 2017, Klimov Viktor <vitek.fomino@bk.ru>
 *
 * This file is part of libtiled.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mapobject.h"

#include "map.h"
#include "maprenderer.h"
#include "objectgroup.h"
#include "objecttemplate.h"
#include "tile.h"

#include <QFontMetricsF>
#include <qmath.h>

namespace Tiled {

TextData::TextData()
    : font(QStringLiteral("sans-serif"))
{
    font.setPixelSize(16);
}

int TextData::flags() const
{
    return wordWrap ? (alignment | Qt::TextWordWrap) : alignment;
}

QTextOption TextData::textOption() const
{
    QTextOption option(alignment);

    if (wordWrap)
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    else
        option.setWrapMode(QTextOption::ManualWrap);

    return option;
}

/**
 * Returns the size of the text when drawn without wrapping.
 */
QSizeF TextData::textSize() const
{
    QFontMetricsF fontMetrics(font);
    return fontMetrics.size(0, text);
}


MapObject::MapObject():
    MapObject(QString(), QString(), QPointF(), QSizeF(0, 0))
{
}

MapObject::MapObject(const QString &name, const QString &type,
                     const QPointF &pos,
                     const QSizeF &size):
    Object(MapObjectType),
    mId(0),
    mShape(Rectangle),
    mName(name),
    mType(type),
    mPos(pos),
    mSize(size),
    mObjectTemplate(nullptr),
    mObjectGroup(nullptr),
    mRotation(0.0),
    mVisible(true),
    mTemplateBase(false)
{
}

int MapObject::index() const
{
    if (mObjectGroup)
        return mObjectGroup->objects().indexOf(const_cast<MapObject*>(this));
    return -1;
}

/**
 * Returns the affective type of this object. This may be the type of its tile,
 * if the object does not have a type set explicitly.
 */
const QString &MapObject::effectiveType() const
{
    if (mType.isEmpty())
        if (const Tile *tile = mCell.tile())
            return tile->type();

    return mType;
}

/**
 * Sets the text data associated with this object.
 */
void MapObject::setTextData(const TextData &textData)
{
    mTextData = textData;
}

/**
 * Shortcut to getting a QRectF from position() and size() that uses cell tile
 * if present.
 *
 * \deprecated See problems in comment.
 */
QRectF MapObject::boundsUseTile() const
{
    // FIXME: This is outdated code:
    // * It does not take into account that a tile object can be scaled.
    // * It neglects that origin is not the same in orthogonal and isometric
    //   maps (see MapObject::alignment).
    // * It does not deal with rotation.

    if (const Tile *tile = mCell.tile()) {
        // Using the tile for determing boundary
        // Note the position given is the bottom-left corner so correct for that
        return QRectF(QPointF(mPos.x(),
                              mPos.y() - tile->height()),
                      tile->size());
    }

    // No tile so just use regular bounds
    return bounds();
}

static void align(QRectF &r, Alignment alignment)
{
    r.translate(-alignmentOffset(r, alignment));
}

/**
 * Returns the bounds of the object in screen space when using the given
 * \a renderer. Does not take into account rotation!
 *
 * This is slightly different from the bounds that should be used when
 * rendering the object, which are returned by the MapRenderer::boundingRect
 * function.
 *
 * \todo Look into unduplicating this code, which is also present in
 * objectselectiontool.cpp in very similar form (objectBounds).
 */
QRectF MapObject::screenBounds(const MapRenderer &renderer) const
{
    if (!mCell.isEmpty()) {
        // Tile objects can have a tile offset, which is scaled along with the image
        QSizeF imgSize;
        QPoint tileOffset;

        if (const Tile *tile = mCell.tile()) {
            imgSize = tile->size();
            tileOffset = tile->offset();
        } else {
            imgSize = size();
        }

        const QPointF position = renderer.pixelToScreenCoords(mPos);
        const QSizeF objectSize = size();
        const qreal scaleX = imgSize.width() > 0 ? objectSize.width() / imgSize.width() : 0;
        const qreal scaleY = imgSize.height() > 0 ? objectSize.height() / imgSize.height() : 0;

        QRectF bounds(position.x() + (tileOffset.x() * scaleX),
                      position.y() + (tileOffset.y() * scaleY),
                      objectSize.width(),
                      objectSize.height());

        align(bounds, alignment());

        return bounds;
    } else {
        switch (mShape) {
        case MapObject::Ellipse:
        case MapObject::Rectangle: {
            QRectF bounds(this->bounds());
            align(bounds, alignment());
            QPolygonF screenPolygon = renderer.pixelToScreenCoords(bounds);
            return screenPolygon.boundingRect();
        }
        case MapObject::Point:
            return renderer.shape(this).boundingRect();
        case MapObject::Polygon:
        case MapObject::Polyline: {
            // Alignment is irrelevant for polygon objects since they have no size
            const QPolygonF polygon = mPolygon.translated(mPos);
            QPolygonF screenPolygon = renderer.pixelToScreenCoords(polygon);
            return screenPolygon.boundingRect();
        }
        case MapObject::Text:
            return renderer.boundingRect(this);
        }
    }

    return QRectF();
}

/*
 * This is somewhat of a workaround for dealing with the ways different objects
 * align.
 *
 * Traditional rectangle objects have top-left alignment.
 * Tile objects have bottom-left alignment on orthogonal maps, but
 * bottom-center alignment on isometric maps.
 *
 * Eventually, the object alignment should probably be configurable. For
 * backwards compatibility, it will need to be configurable on a per-object
 * level.
 */
Alignment MapObject::alignment() const
{
    if (mCell.isEmpty()) {
        return TopLeft;
    } else if (mObjectGroup) {
        if (Map *map = mObjectGroup->map())
            if (map->orientation() == Map::Isometric)
                return Bottom;
	}
	return BottomLeft;
}

QVariant MapObject::mapObjectProperty(Property property) const
{
    switch (property) {
    case NameProperty:          return mName;
    case TypeProperty:          return mType;
    case VisibleProperty:       return mVisible;
    case TextProperty:          return mTextData.text;
    case TextFontProperty:      return mTextData.font;
    case TextAlignmentProperty: return QVariant::fromValue(mTextData.alignment);
    case TextWordWrapProperty:  return mTextData.wordWrap;
    case TextColorProperty:     return mTextData.color;
    case SizeProperty:          return mSize;
    case RotationProperty:      return mRotation;
    case CellProperty:          Q_ASSERT(false); break;
    case ShapeProperty:         return mShape;
    }
    return QVariant();
}

void MapObject::setMapObjectProperty(Property property, const QVariant &value)
{
    switch (property) {
    case NameProperty:          mName = value.toString(); break;
    case TypeProperty:          mType = value.toString(); break;
    case VisibleProperty:       mVisible = value.toBool(); break;
    case TextProperty:          mTextData.text = value.toString(); break;
    case TextFontProperty:      mTextData.font = value.value<QFont>(); break;
    case TextAlignmentProperty: mTextData.alignment = value.value<Qt::Alignment>(); break;
    case TextWordWrapProperty:  mTextData.wordWrap = value.toBool(); break;
    case TextColorProperty:     mTextData.color = value.value<QColor>(); break;
    case SizeProperty:          mSize = value.toSizeF(); break;
    case RotationProperty:      mRotation = value.toReal(); break;
    case CellProperty:          Q_ASSERT(false); break;
    case ShapeProperty:         mShape = value.value<Shape>(); break;
    }
}

/**
 * Flip this object in the given \a direction. This doesn't change the size
 * of the object.
 */
void MapObject::flip(FlipDirection direction, const QPointF &origin)
{
    //computing new rotation and flip transform
    QTransform flipTransform;
    flipTransform.translate(origin.x(), origin.y());
    qreal newRotation = 0;
    bool hor=false;
    bool ver=false;
    if(!mCell.isEmpty())
    {
        hor = this->cell().flippedHorizontally();
        ver = this->cell().flippedVertically();
    }
    if (direction == FlipHorizontally) {
        newRotation = 180.0 - rotation();
        flipTransform.scale(-1, 1);
        if(!mCell.isEmpty())
        {
            hor=!hor;
        }
    } else { //direction == FlipVertically
        flipTransform.scale(1, -1);
        newRotation = -rotation();
        if(!mCell.isEmpty())
        {
            ver=!ver;
        }
    }
    flipTransform.translate(-origin.x(), -origin.y());


    if (!mCell.isEmpty())
    {
        newRotation=rotation();
        flipTileObject(hor,ver);



    }
    else if (!mPolygon.isEmpty())
        flipPolygonObject(flipTransform);
    else
        flipRectObject(flipTransform);

    //installing new rotation after computing new position
    setRotation(newRotation);

}

/**
 * Returns a duplicate of this object. The caller is responsible for the
 * ownership of this newly created object.
 */
MapObject *MapObject::clone() const
{
	MapObject *o = new MapObject(mName, mType, mPos, mSize);
    o->setId(mId);
	o->setObjectTemplate(mObjectTemplate);
	o->setProperties(properties());
	o->setChangedProperties(mChangedProperties);
	o->setCell(mCell);
	o->setSize(mSize);
	o->setVisible(mVisible);
	o->setTextData(mTextData);
	o->setPolygon(mPolygon);
	o->setShape(mShape);

	o->setRotation(mRotation);

    return o;
}


void MapObject::copyPropertiesFrom(const MapObject *object)
{
    setName(object->name());
    setType(object->type());
	setObjectTemplate(object->objectTemplate());
	setProperties(object->properties());
	setChangedProperties(object->changedProperties());
	setTextData(object->textData());
    setPolygon(object->polygon());
    setShape(object->shape());
    setCell(object->cell());
	setSize(object->size());
    setRotation(object->rotation());
    setVisible(object->isVisible());


}

const MapObject *MapObject::templateObject() const
{
    if (mObjectTemplate)
        return mObjectTemplate->object();
    return nullptr;
}

void MapObject::syncWithTemplate()
{
    const MapObject *base = templateObject();
    if (!base)
        return;

    if (!propertyChanged(MapObject::NameProperty))
        setName(base->name());

    if (!propertyChanged(MapObject::SizeProperty))
        setSize(base->size());

    if (!propertyChanged(MapObject::TypeProperty))
        setType(base->type());

    if (!propertyChanged(MapObject::TextProperty))
        setTextData(base->textData());

    if (!propertyChanged(MapObject::ShapeProperty)) {
        setShape(base->shape());
        setPolygon(base->polygon());
    }

    if (!propertyChanged(MapObject::CellProperty))
	{
		setCell(base->cell());
	}
	else
	{
		auto _cell = cell();
		_cell.setTile(base->cell().tile());
		setCell(_cell);
	}

    if (!propertyChanged(MapObject::RotationProperty))
        setRotation(base->rotation());

    if (!propertyChanged(MapObject::VisibleProperty))
        setVisible(base->isVisible());
}

void MapObject::flipRectObject(const QTransform &flipTransform)
{
    QPointF oldBottomLeftPoint = QPointF(cos(qDegreesToRadians(rotation() + 90)) * height() + x(),
                                         sin(qDegreesToRadians(rotation() + 90)) * height() + y());
    QPointF newPos = flipTransform.map(oldBottomLeftPoint);

    setPosition(newPos);
}

void MapObject::flipPolygonObject(const QTransform &flipTransform)
{
    QTransform polygonToMapTransform;
    polygonToMapTransform.translate(x(), y());
    polygonToMapTransform.rotate(rotation());

    QPointF localPolygonCenter = mPolygon.boundingRect().center();
    QTransform polygonFlip;
    polygonFlip.translate(localPolygonCenter.x(), localPolygonCenter.y());
    polygonFlip.scale(1, -1);
    polygonFlip.translate(-localPolygonCenter.x(), -localPolygonCenter.y());

    QPointF oldBottomLeftPoint = polygonToMapTransform.map(polygonFlip.map(QPointF(0, 0)));
    QPointF newPos = flipTransform.map(oldBottomLeftPoint);

    mPolygon = polygonFlip.map(mPolygon);
    setPosition(newPos);
}

void MapObject::flipTileObject(const QTransform &flipTransform)
{
    mCell.setFlippedVertically(!mCell.flippedVertically());

    //old tile position is bottomLeftPoint
    QPointF topLeftTilePoint = QPointF(cos(qDegreesToRadians(rotation() - 90)) * height() + x(),
                                       sin(qDegreesToRadians(rotation() - 90)) * height() + y());
    QPointF newPos = flipTransform.map(topLeftTilePoint);

    setPosition(newPos);
}

void MapObject::flipTileObject(bool hor, bool ver)
{
    mCell.setFlippedVertically(ver);
    mCell.setFlippedHorizontally(hor);
}

} // namespace Tiled
