/*
 * clipboardmanager.cpp
 * Copyright 2009, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
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

#include "clipboardmanager.h"

#include "addremovemapobject.h"
#include "map.h"
#include "mapdocument.h"
#include "mapobject.h"
#include "maprenderer.h"
#include "mapview.h"
#include "objectgroup.h"
#include "snaphelper.h"
#include "tmxmapformat.h"
#include "tile.h"
#include "tilelayer.h"
#include "preferences.h"
#include "qmath.h"

#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QMimeData>
#include <QSet>
#include <QUndoStack>

static const char * const TMX_MIMETYPE = "text/tmx";

using namespace Tiled;
using namespace Tiled::Internal;

ClipboardManager *ClipboardManager::mInstance;

ClipboardManager::ClipboardManager()
    : mClipboard(QApplication::clipboard())
    , mHasMap(false)
    , mHasProperties(false)
{
    connect(mClipboard, &QClipboard::dataChanged, this, &ClipboardManager::update);
    update();
}

/**
 * Returns the clipboard manager instance. Creates the instance when it
 * doesn't exist yet.
 */
ClipboardManager *ClipboardManager::instance()
{
    if (!mInstance)
        mInstance = new ClipboardManager;
    return mInstance;
}

/**
 * Deletes the clipboard manager instance if it exists.
 */
void ClipboardManager::deleteInstance()
{
    delete mInstance;
    mInstance = nullptr;
}

/**
 * Retrieves the map from the clipboard. Returns null when there was no map or
 * loading failed.
 */
Map *ClipboardManager::map() const
{
    const QMimeData *mimeData = mClipboard->mimeData();
    const QByteArray data = mimeData->data(QLatin1String(TMX_MIMETYPE));
    if (data.isEmpty())
        return nullptr;

    TmxMapFormat format;
    return format.fromByteArray(data);
}

/**
 * Sets the given map on the clipboard.
 */
void ClipboardManager::setMap(const Map &map)
{
    TmxMapFormat format;

    QMimeData *mimeData = new QMimeData;
    mimeData->setData(QLatin1String(TMX_MIMETYPE), format.toByteArray(&map));

    mClipboard->setMimeData(mimeData);
}

Properties ClipboardManager::properties() const
{
    const QMimeData *mimeData = mClipboard->mimeData();
    const QByteArray data = mimeData->data(QLatin1String(PROPERTIES_MIMETYPE));
    const QJsonDocument document = QJsonDocument::fromBinaryData(data);

    return Properties::fromJson(document.array());
}

void ClipboardManager::setProperties(const Properties &properties)
{
    const QJsonDocument document(properties.toJson());

    QMimeData *mimeData = new QMimeData;
    mimeData->setData(QLatin1String(PROPERTIES_MIMETYPE), document.toBinaryData());
    mimeData->setText(QString::fromUtf8(document.toJson()));

    mClipboard->setMimeData(mimeData);
}

/**
 * Convenience method to copy the current selection to the clipboard.
 * Copies both tile selection and object selection.
 *
 * @returns whether anything was copied.
 */
bool ClipboardManager::copySelection(const MapDocument &mapDocument)
{
    const Map *map = mapDocument.map();
    const QRegion &selectedArea = mapDocument.selectedArea();
    const QList<MapObject*> selectedObjects = mapDocument.selectedObjectsOrdered();
    const QList<Layer*> &selectedLayers = mapDocument.selectedLayers();

    const QRect selectionBounds = selectedArea.boundingRect();

    // Create a temporary map to write to the clipboard
    Map copyMap(map->orientation(),
                selectionBounds.width(),
                selectionBounds.height(),
                map->tileWidth(), map->tileHeight());
    copyMap.setRenderOrder(map->renderOrder());

    LayerIterator layerIterator(map);
    while (Layer *layer = layerIterator.next()) {
        switch (layer->layerType()) {
        case Layer::TileLayerType: {
            if (!selectedLayers.contains(layer))    // ignore unselected tile layers
                continue;

            const TileLayer *tileLayer = static_cast<const TileLayer*>(layer);
            const QRegion area = selectedArea.intersected(tileLayer->bounds());
            if (area.isEmpty())                     // nothing to copy
                continue;

            // Copy the selected part of the layer
            TileLayer *copyLayer = tileLayer->copy(area.translated(-tileLayer->position()));
            copyLayer->setName(tileLayer->name());
            copyLayer->setPosition(area.boundingRect().topLeft());

            copyMap.addLayer(copyLayer);
            break;
        }
        case Layer::ObjectGroupType: // todo: maybe it makes to group selected objects by layer
        case Layer::ImageLayerType:
        case Layer::GroupLayerType:
            break;  // nothing to do
        }
    }

    if (!selectedObjects.isEmpty()) {
        // Create a new object group with clones of the selected objects
        ObjectGroup *objectGroup = new ObjectGroup;
        for (const MapObject *mapObject : selectedObjects)
            objectGroup->addObject(mapObject->clone());
        copyMap.addLayer(objectGroup);
    }

    if (copyMap.layerCount() > 0) {
        // Resolve the set of tilesets used by the created map
        copyMap.addTilesets(copyMap.usedTilesets());

        setMap(copyMap);
        return true;
    }

    return false;
}

/**
 * Convenience method that deals with some of the logic related to pasting
 * a group of objects.
 */
void ClipboardManager::pasteObjectGroup(const ObjectGroup *objectGroup,
                                        MapDocument *mapDocument,
                                        const MapView *view,
                                        PasteFlags flags)
{
    using namespace std;
    Layer *currentLayer = mapDocument->currentLayer();
    if (!currentLayer)
        return;

    ObjectGroup *currentObjectGroup = currentLayer->asObjectGroup();
    if (!currentObjectGroup)
        return;

    QPointF insertPos;

    if (!(flags & PasteInPlace)) {
        // Determine where to insert the objects
        MapRenderer *renderer = mapDocument->renderer();
        QPointF center = objectGroup->objectsBoundingRect().topLeft();
        if(objectGroup->objects().size()==1 && objectGroup->objectAt(0)->isTileObject())
        {
            MapObject *objectClone = objectGroup->objectAt(0);
            QPointF offset = QPointF(0,0);
            QTransform t;
            t.rotate(objectClone->rotation());
            QVariant aux = objectClone->inheritedProperty(QString(QLatin1String("offsetX")));
            double scaleX = abs(objectClone->width())/abs(objectClone->cell().tile()->width());
            double scaleY = abs(objectClone->height())/abs(objectClone->cell().tile()->height());
            if(objectClone->cell().flippedHorizontally())
                scaleX*=-1;
            if(objectClone->cell().flippedVertically())
                scaleY*=-1;

            if(aux.isValid())
            {
                if(scaleX>=0)
                    offset.setX(aux.toInt()*scaleX);
                else
                    offset.setX(objectClone->width()-aux.toInt()*abs(scaleX));
            }

            aux = objectClone->inheritedProperty(QString(QLatin1String("offsetY")));
            if(aux.isValid())
            {
                if(scaleY>=0)
                    offset.setY(aux.toInt()*scaleY);
                else
                    offset.setY(objectClone->height()-aux.toInt()*abs(scaleY));
            }
           center-=t.map(offset);
        }

        // Take the mouse position if the mouse is on the view, otherwise
        // take the center of the view.
        QPoint viewPos;
        if (view->underMouse())
            viewPos = view->mapFromGlobal(QCursor::pos());
        else
            viewPos = QPoint(view->width() / 2, view->height() / 2);

        const QPointF scenePos = view->mapToScene(viewPos);

        insertPos = renderer->screenToPixelCoords(scenePos) - center;
        SnapHelper(renderer).snap(insertPos);
    }

    QVector<AddMapObjects::Entry> objectsToAdd;
    objectsToAdd.reserve(objectGroup->objectCount());

    for (const MapObject *mapObject : objectGroup->objects()) {
        if (flags & PasteNoTileObjects && !mapObject->cell().isEmpty())
            continue;

        MapObject *objectClone = mapObject->clone();
        objectClone->resetId();
        Preferences *prefs = Preferences::instance();
        int tw = prefs->snapGrid().width();
        int th = prefs->snapGrid().height();
        if(!prefs->snapToGrid() && !prefs->snapToPixels())
        {
            tw = 1;
            th = 1;
        }


        if(objectClone->isTileObject())
        {
            objectClone->setPosition(objectClone->position() + insertPos);

            QPointF offset = QPointF(0,0);
            QTransform t;
            t.rotate(objectClone->rotation());
            QVariant aux = objectClone->inheritedProperty(QString(QLatin1String("offsetX")));
            double scaleX = abs(objectClone->width())/abs(objectClone->cell().tile()->width());
            double scaleY = abs(objectClone->height())/abs(objectClone->cell().tile()->height());
            if(objectClone->cell().flippedHorizontally())
                scaleX*=-1;
            if(objectClone->cell().flippedVertically())
                scaleY*=-1;

            if(aux.isValid())
            {
                if(scaleX>=0)
                    offset.setX(aux.toInt()*scaleX);
                else
                    offset.setX(objectClone->width()-aux.toInt()*abs(scaleX));
            }

            aux = objectClone->inheritedProperty(QString(QLatin1String("offsetY")));
            if(aux.isValid())
            {
                if(scaleY>=0)
                    offset.setY(aux.toInt()*scaleY);
                else
                    offset.setY(objectClone->height()-aux.toInt()*abs(scaleY));
            }

            objectClone->setPosition(objectClone->position()+offset);
            objectClone->setX(floor((objectClone->x())/tw)*tw);
            objectClone->setY(floor((objectClone->y())/th)*th);

            offset.setY(offset.y()-objectClone->height());


            objectClone->setPosition(objectClone->position() - t.map(offset));
        }
        else
        {
            //insertPos += QPointF(tw,th);
            objectClone->setPosition(objectClone->position() + insertPos);
        }
        objectsToAdd.append(AddMapObjects::Entry { objectClone, currentObjectGroup });
    }

    auto command = new AddMapObjects(mapDocument, objectsToAdd);
    command->setText(tr("Paste Objects"));

    mapDocument->undoStack()->push(command);
    mapDocument->setSelectedObjects(AddMapObjects::objects(objectsToAdd));
}

void ClipboardManager::update()
{
    bool hasMap = false;
    bool hasProperties = false;

    if (const QMimeData *data = mClipboard->mimeData()) {
        hasMap = data->hasFormat(QLatin1String(TMX_MIMETYPE));
        hasProperties = data->hasFormat(QLatin1String(PROPERTIES_MIMETYPE));
    }

    if (hasMap != mHasMap) {
        mHasMap = hasMap;
        emit hasMapChanged();
    }

    if (hasProperties != mHasProperties) {
        mHasProperties = hasProperties;
        emit hasPropertiesChanged();
    }
}
