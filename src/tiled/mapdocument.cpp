/*
 * mapdocument.cpp
 * Copyright 2008-2017, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 * Copyright 2009, Jeff Bland <jeff@teamphobic.com>
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

#pragma once
#include "mapdocument.h"

#include "addremovelayer.h"
#include "addremovemapobject.h"
#include "addremovetileset.h"
#include "changelayer.h"
#include "changemapobject.h"
#include "changemapobjectsorder.h"
#include "changeproperties.h"
#include "changeselectedarea.h"
#include "containerhelpers.h"
#include "flipmapobjects.h"
#include "grouplayer.h"
#include "hexagonalrenderer.h"
#include "imagelayer.h"
#include "isometricrenderer.h"
#include "layermodel.h"
#include "map.h"
#include "mapobject.h"
#include "mapobjectmodel.h"
#include "movelayer.h"
#include "movemapobject.h"
#include "movemapobjecttogroup.h"
#include "objectgroup.h"
#include "offsetlayer.h"
#include "orthogonalrenderer.h"
#include "painttilelayer.h"
#include "preferences.h"
#include "rangeset.h"
#include "reparentlayers.h"
#include "resizemap.h"
#include "resizetilelayer.h"
#include "rotatemapobject.h"
#include "staggeredrenderer.h"
#include "templatemanager.h"
#include "terrain.h"
#include "tile.h"
#include "tilelayer.h"
#include "tilesetdocument.h"
#include "tmxmapformat.h"

#include <QFileInfo>
#include <QRect>
#include <QUndoStack>

#include "qtcompat_p.h"

using namespace Tiled;
using namespace Tiled::Internal;

MapDocument::MapDocument(Map *map, const QString &fileName)
    : Document(MapDocumentType, fileName)
    , mMap(map)
    , mLayerModel(new LayerModel(this))
    , mHoveredMapObject(nullptr)
    , mMapObjectModel(new MapObjectModel(this))
{
    mCurrentObject = map;

    createRenderer();

    mCurrentLayer = (map->layerCount() == 0) ? nullptr : map->layerAt(0);
    mLayerModel->setMapDocument(this);

    // Forward signals emitted from the layer model
    connect(mLayerModel, &LayerModel::layerAdded,
            this, &MapDocument::onLayerAdded);
    connect(mLayerModel, &LayerModel::layerAboutToBeRemoved,
            this, &MapDocument::onLayerAboutToBeRemoved);
    connect(mLayerModel, &LayerModel::layerRemoved,
            this, &MapDocument::onLayerRemoved);
    connect(mLayerModel, &LayerModel::layerChanged,
            this, &MapDocument::layerChanged);

    // Forward signals emitted from the map object model
    mMapObjectModel->setMapDocument(this);
    connect(mMapObjectModel, &MapObjectModel::objectsAdded,
            this, &MapDocument::objectsAdded);
    connect(mMapObjectModel, &MapObjectModel::objectsChanged,
            this, &MapDocument::objectsChanged);
    connect(mMapObjectModel, &MapObjectModel::objectsTypeChanged,
            this, &MapDocument::objectsTypeChanged);
    connect(mMapObjectModel, &MapObjectModel::objectsRemoved,
            this, &MapDocument::onObjectsRemoved);

    connect(mMapObjectModel, &QAbstractItemModel::rowsInserted,
            this, &MapDocument::onMapObjectModelRowsInserted);
    connect(mMapObjectModel, &QAbstractItemModel::rowsRemoved,
            this, &MapDocument::onMapObjectModelRowsInsertedOrRemoved);
    connect(mMapObjectModel, &QAbstractItemModel::rowsMoved,
            this, &MapDocument::onObjectsMoved);

    connect(TemplateManager::instance(), &TemplateManager::objectTemplateChanged,
            this, &MapDocument::updateTemplateInstances);
}

MapDocument::~MapDocument()
{
}

bool MapDocument::save(const QString &fileName, QString *error)
{
    MapFormat *mapFormat = mWriterFormat;

    TmxMapFormat tmxMapFormat;
    if (!mapFormat)
        mapFormat = &tmxMapFormat;

    if (!mapFormat->write(map(), fileName)) {
        if (error)
            *error = mapFormat->errorString();
        return false;
    }

    undoStack()->setClean();
    setFileName(fileName);
    mLastSaved = QFileInfo(fileName).lastModified();

    // Mark TilesetDocuments for embedded tilesets as saved
    for (const SharedTileset &tileset : mMap->tilesets()) {
        if (TilesetDocument *tilesetDocument = TilesetDocument::findDocumentForTileset(tileset))
            if (tilesetDocument->isEmbedded())
                tilesetDocument->setClean();
    }

    emit saved();
    return true;
}

MapDocumentPtr MapDocument::load(const QString &fileName,
                                 MapFormat *format,
                                 QString *error)
{
	Map *map = format->read(fileName, Preferences::instance()->settings());

    if (!map) {
        if (error)
            *error = format->errorString();
        return MapDocumentPtr();
    }

    MapDocumentPtr document = MapDocumentPtr::create(map, fileName);
    document->setReaderFormat(format);
    if (format->hasCapabilities(MapFormat::Write))
        document->setWriterFormat(format);

    return document;
}

MapFormat *MapDocument::readerFormat() const
{
    return mReaderFormat;
}

void MapDocument::setReaderFormat(MapFormat *format)
{
    mReaderFormat = format;
}

FileFormat *MapDocument::writerFormat() const
{
    return mWriterFormat;
}

void MapDocument::setWriterFormat(MapFormat *format)
{
    mWriterFormat = format;
}

MapFormat *MapDocument::exportFormat() const
{
    return mExportFormat;
}

void MapDocument::setExportFormat(FileFormat *format)
{
    mExportFormat = qobject_cast<MapFormat*>(format);
    Q_ASSERT(mExportFormat);
}

/**
 * Returns the name with which to display this map. It is the file name without
 * its path, or 'untitled.tmx' when the map has no file name.
 */
QString MapDocument::displayName() const
{
    QString displayName = QFileInfo(mFileName).fileName();
    if (displayName.isEmpty())
        displayName = tr("untitled.tmx");

    return displayName;
}

/**
  * Returns the sibling index of the given \a layer, or -1 if no layer is given.
  */
int MapDocument::layerIndex(const Layer *layer) const
{
    if (!layer)
        return -1;
    return layer->siblingIndex();
}

void MapDocument::setCurrentLayer(Layer *layer)
{
    if (mCurrentLayer == layer)
        return;

    mCurrentLayer = layer;
    emit currentLayerChanged(mCurrentLayer);

    if (mCurrentLayer)
        if (!mCurrentObject || mCurrentObject->typeId() == Object::LayerType)
            setCurrentObject(mCurrentLayer);
}

void MapDocument::setSelectedLayers(const QList<Layer *> &layers)
{
    mSelectedLayers = layers;
    emit selectedLayersChanged();
}

/**
 * Custom intersects check necessary because QRectF::intersects wants a
 * non-empty area of overlap, but we should also consider overlap with empty
 * area as intersection.
 *
 * Results for rectangles with negative size are undefined.
 */
static bool intersects(const QRectF &a, const QRectF &b)
{
    return a.right() >= b.left() &&
            a.bottom() >= b.top() &&
            a.left() <= b.right() &&
            a.top() <= b.bottom();
}

static bool visibleIn(const QRectF &area, MapObject *object,
                      const MapRenderer &renderer)
{
    QRectF boundingRect = renderer.boundingRect(object);

    if (object->rotation() != 0) {
        // Rotate around object position
        QPointF pos = renderer.pixelToScreenCoords(object->position());
        boundingRect.translate(-pos);

        QTransform transform;
        transform.rotate(object->rotation());
        boundingRect = transform.mapRect(boundingRect);

        boundingRect.translate(pos);
    }

    return intersects(area, boundingRect);
}

void MapDocument::resizeMap(const QSize &size, const QPoint &offset, bool removeObjects)
{
    const QRegion movedSelection = mSelectedArea.translated(offset);
    const QRect newArea = QRect(-offset, size);
    const QRectF visibleArea = mRenderer->boundingRect(newArea);

    const QPointF origin = mRenderer->tileToPixelCoords(QPointF());
    const QPointF newOrigin = mRenderer->tileToPixelCoords(-offset);
    const QPointF pixelOffset = origin - newOrigin;

    // Resize the map and each layer
    QUndoCommand *command = new QUndoCommand(tr("Resize Map"));

    QList<MapObject *> objectsToRemove;

    LayerIterator iterator(mMap.get());
    while (Layer *layer = iterator.next()) {
        switch (layer->layerType()) {
        case Layer::TileLayerType: {
            TileLayer *tileLayer = static_cast<TileLayer*>(layer);
            new ResizeTileLayer(this, tileLayer, size, offset, command);
            break;
        }
        case Layer::ObjectGroupType: {
            ObjectGroup *objectGroup = static_cast<ObjectGroup*>(layer);

            for (MapObject *o : objectGroup->objects()) {
                if (removeObjects && !visibleIn(visibleArea, o, *mRenderer)) {
                    // Remove objects that will fall outside of the map
                    objectsToRemove.append(o);
                } else {
                    QPointF oldPos = o->position();
                    QPointF newPos = oldPos + pixelOffset;
                    new MoveMapObject(this, o, newPos, oldPos, command);
                }
            }
            break;
        }
        case Layer::ImageLayerType: {
            // Adjust image layer by changing its offset
            auto imageLayer = static_cast<ImageLayer*>(layer);
            new SetLayerOffset(this, layer,
                               imageLayer->offset() + pixelOffset,
                               command);
            break;
        }
        case Layer::GroupLayerType: {
            // Recursion handled by LayerIterator
            break;
        }
        }
    }

    if (!objectsToRemove.isEmpty())
        new RemoveMapObjects(this, objectsToRemove, command);

    new ResizeMap(this, size, command);
    new ChangeSelectedArea(this, movedSelection, command);

    mUndoStack->push(command);

    // TODO: Handle layers that don't match the map size correctly
}

void MapDocument::autocropMap()
{
    if (!mCurrentLayer || !mCurrentLayer->isTileLayer())
        return;

    TileLayer *tileLayer = static_cast<TileLayer*>(mCurrentLayer);

    const QRect bounds = tileLayer->region().boundingRect();
    if (bounds.isNull())
        return;

    resizeMap(bounds.size(), -bounds.topLeft(), true);
}

void MapDocument::offsetMap(const QList<Layer*> &layers,
                            const QPoint &offset,
                            const QRect &bounds,
                            bool wrapX, bool wrapY)
{
    if (layers.empty())
        return;

    mUndoStack->beginMacro(tr("Offset Map"));
    for (auto layer : layers) {
        mUndoStack->push(new OffsetLayer(this, layer, offset,
                                         bounds, wrapX, wrapY));
    }
    mUndoStack->endMacro();
}

/**
 * Flips the selected objects in the given \a direction.
 */
void MapDocument::flipSelectedObjects(FlipDirection direction)
{
    if (mSelectedObjects.isEmpty())
        return;

    mUndoStack->push(new FlipMapObjects(this, mSelectedObjects, direction));
}

/**
 * Rotates the selected objects.
 */
void MapDocument::rotateSelectedObjects(RotateDirection direction)
{
    if (mSelectedObjects.isEmpty())
        return;

    mUndoStack->beginMacro(tr("Rotate %n Object(s)", "",
                              mSelectedObjects.size()));

    // TODO: Rotate them properly as a group
    const auto &selectedObjects = mSelectedObjects;
    for (MapObject *mapObject : selectedObjects) {
        const qreal oldRotation = mapObject->rotation();
        qreal newRotation = oldRotation;

        if (direction == RotateLeft) {
            newRotation -= 90;
            if (newRotation < -180)
                newRotation += 360;
        } else {
            newRotation += 90;
            if (newRotation > 180)
                newRotation -= 360;
        }

        mUndoStack->push(new RotateMapObject(this, mapObject,
                                             newRotation, oldRotation));
    }
    mUndoStack->endMacro();
}

/**
 * Adds a layer of the given type to the top of the layer stack. After adding
 * the new layer, emits editLayerNameRequested().
 */
Layer *MapDocument::addLayer(Layer::TypeFlag layerType)
{
    Layer *layer = nullptr;
    QString name;

    switch (layerType) {
    case Layer::TileLayerType:
        name = tr("Tile Layer %1").arg(mMap->tileLayerCount() + 1);
        layer = new TileLayer(name, 0, 0, mMap->width(), mMap->height());
        break;
    case Layer::ObjectGroupType:
        name = tr("Object Layer %1").arg(mMap->objectGroupCount() + 1);
        layer = new ObjectGroup(name, 0, 0);
        break;
    case Layer::ImageLayerType:
        name = tr("Image Layer %1").arg(mMap->imageLayerCount() + 1);
        layer = new ImageLayer(name, 0, 0);
        break;
    case Layer::GroupLayerType:
        name = tr("Group %1").arg(mMap->groupLayerCount() + 1);
        layer = new GroupLayer(name, 0, 0);
        break;
    }
    Q_ASSERT(layer);

    auto parentLayer = mCurrentLayer ? mCurrentLayer->parentLayer() : nullptr;
    const int index = layerIndex(mCurrentLayer) + 1;
    mUndoStack->push(new AddLayer(this, index, layer, parentLayer));
    setCurrentLayer(layer);

    emit editLayerNameRequested();

    return layer;
}

Layer *MapDocument::addLayer(Layer::TypeFlag layerType, QString name)
{
	Layer *layer = nullptr;
	if(!name.endsWith(QStringLiteral("%1")))
	{
		name = name.append(QStringLiteral(" %1"));
	}
	name = name.arg(mMap->tileLayerCount() + 1);
	switch (layerType) {
	case Layer::TileLayerType:

		layer = new TileLayer(name, 0, 0, mMap->width(), mMap->height());
		break;
	case Layer::ObjectGroupType:
		layer = new ObjectGroup(name, 0, 0);
		break;
	case Layer::ImageLayerType:
		layer = new ImageLayer(name, 0, 0);
		break;
	case Layer::GroupLayerType:
		layer = new GroupLayer(name, 0, 0);
		break;
	}
	Q_ASSERT(layer);

	auto parentLayer = mCurrentLayer ? mCurrentLayer->parentLayer() : nullptr;
	const int index = layerIndex(mCurrentLayer) + 1;
	mUndoStack->push(new AddLayer(this, index, layer, parentLayer));
	setCurrentLayer(layer);

	emit editLayerNameRequested();

	return layer;
}

/**
 * Creates a new group layer, putting the given \a layer inside the group.
 */
void MapDocument::groupLayer(Layer *layer)
{
    if (!layer)
        return;

    Q_ASSERT(layer->map() == mMap.get());

    QString name = tr("Group %1").arg(mMap->groupLayerCount() + 1);
    auto groupLayer = new GroupLayer(name, 0, 0);
    auto parentLayer = layer->parentLayer();
    const int index = layer->siblingIndex() + 1;
    mUndoStack->beginMacro(tr("Group Layer"));
    mUndoStack->push(new AddLayer(this, index, groupLayer, parentLayer));
    mUndoStack->push(new ReparentLayers(this, QList<Layer*>() << layer, groupLayer, 0));
    mUndoStack->endMacro();
}

/**
 * Ungroups the given \a layer. If the layer itself is a group layer, then this
 * group is ungrouped. Otherwise, if the layer is part of a group layer, then
 * it is removed from the group.
 */
void MapDocument::ungroupLayer(Layer *layer)
{
    if (!layer)
        return;

    GroupLayer *groupLayer = layer->asGroupLayer();
    QList<Layer *> layers;

    if (groupLayer) {
        layers = groupLayer->layers();
    } else if (layer->parentLayer()) {
        layers.append(layer);
        groupLayer = layer->parentLayer();
    } else {
        // No ungrouping possible
        return;
    }

    GroupLayer *targetParent = groupLayer->parentLayer();
    int groupIndex = groupLayer->siblingIndex();

    mUndoStack->beginMacro(tr("Ungroup Layer"));
    mUndoStack->push(new ReparentLayers(this, layers, targetParent, groupIndex + 1));

    if (groupLayer->layerCount() == 0)
        mUndoStack->push(new RemoveLayer(this, groupIndex, targetParent));

    mUndoStack->endMacro();
}

/**
 * Duplicates the currently selected layer.
 */
void MapDocument::duplicateLayer()
{
    if (!mCurrentLayer)
        return;

    Layer *duplicate = mCurrentLayer->clone();
    duplicate->setName(tr("Copy of %1").arg(duplicate->name()));

    if (duplicate->layerType() == Layer::ObjectGroupType)
        static_cast<ObjectGroup*>(duplicate)->resetObjectIds();

    auto parentLayer = mCurrentLayer ? mCurrentLayer->parentLayer() : nullptr;
    const int index = layerIndex(mCurrentLayer) + 1;
    QUndoCommand *cmd = new AddLayer(this, index, duplicate, parentLayer);
    cmd->setText(tr("Duplicate Layer"));
    mUndoStack->push(cmd);
    setCurrentLayer(duplicate);
}

/**
 * Merges the currently selected layer with the layer below. This only works
 * when the layers can be merged.
 *
 * \see Layer::canMergeWith
 */
void MapDocument::mergeLayerDown()
{
    auto parentLayer = mCurrentLayer ? mCurrentLayer->parentLayer() : nullptr;
    const int index = layerIndex(mCurrentLayer);

    if (index < 1)
        return;

    Layer *lowerLayer = mCurrentLayer->siblings().at(index - 1);

    if (!lowerLayer->canMergeWith(mCurrentLayer))
        return;

    Layer *merged = lowerLayer->mergedWith(mCurrentLayer);

    mUndoStack->beginMacro(tr("Merge Layer Down"));
    mUndoStack->push(new AddLayer(this, index - 1, merged, parentLayer));
    mUndoStack->push(new RemoveLayer(this, index, parentLayer));
    mUndoStack->push(new RemoveLayer(this, index, parentLayer));
    mUndoStack->endMacro();
}

/**
 * Moves the given \a layer up, when it is not already at the top of the map.
 */
void MapDocument::moveLayerUp(Layer *layer)
{
    if (!layer || !MoveLayer::canMoveUp(*layer))
        return;

    mUndoStack->push(new MoveLayer(this, layer, MoveLayer::Up));
}

/**
 * Moves the given \a layer up, when it is not already at the bottom of the map.
 */
void MapDocument::moveLayerDown(Layer *layer)
{
    if (!layer || !MoveLayer::canMoveDown(*layer))
        return;

    mUndoStack->push(new MoveLayer(this, layer, MoveLayer::Down));
}

/**
 * Removes the given \a layer.
 */
void MapDocument::removeLayer(Layer *layer)
{
    Q_ASSERT(layer->map() == mMap.get());
    mUndoStack->push(new RemoveLayer(this,
                                     layer->siblingIndex(),
                                     layer->parentLayer()));
}

/**
  * Show or hide all other layers except the given \a layer.
  * If any other layer is visible then all layers will be hidden, otherwise
  * the layers will be shown.
  */
void MapDocument::toggleOtherLayers(Layer *layer)
{
    mLayerModel->toggleOtherLayers(layer);
}

/**
* Lock or unlock all other layers except the given \a layer.
* If any other layer is unlocked then all layers will be locked, otherwise
* the layers will be unlocked.
*/
void MapDocument::toggleLockOtherLayers(Layer *layer)
{
    mLayerModel->toggleLockOtherLayers(layer);
}


/**
 * Adds a tileset to this map at the given \a index. Emits the appropriate
 * signal.
 */
void MapDocument::insertTileset(int index, const SharedTileset &tileset)
{
    emit tilesetAboutToBeAdded(index);
    mMap->insertTileset(index, tileset);
    emit tilesetAdded(index, tileset.data());
}

/**
 * Removes the tileset at the given \a index from this map. Emits the
 * appropriate signal.
 *
 * \warning Does not make sure that any references to tiles in the removed
 *          tileset are cleared.
 */
void MapDocument::removeTilesetAt(int index)
{
    emit tilesetAboutToBeRemoved(index);

    SharedTileset tileset = mMap->tilesets().at(index);
    mMap->removeTilesetAt(index);
    emit tilesetRemoved(tileset.data());
}

/**
 * Replaces the tileset at the given \a index with the new \a tileset. Replaces
 * all tiles from the replaced tileset with tiles from the new tileset.
 *
 * @return The replaced tileset.
 */
SharedTileset MapDocument::replaceTileset(int index, const SharedTileset &tileset)
{
    SharedTileset oldTileset = mMap->tilesetAt(index);

    bool added = mMap->replaceTileset(oldTileset, tileset);

    if (added)
        emit tilesetReplaced(index, tileset.data(), oldTileset.data());
    else
        emit tilesetRemoved(oldTileset.data());

    return oldTileset;
}

/**
 * Paints the tile layers present in the given \a map onto this map. Matches
 * layers by name and creates new layers when they could not be found.
 *
 * In case the \a map only contains a single tile layer, it is always painted
 * into the current tile layer. This happens also for unnamed layers. In these
 * cases, the layers are skipped when the current layer isn't a tile layer.
 *
 * If the matched target layer is locked it is also skipped.
 *
 * \a mergeable indicates whether the paint operations performed by this
 * function are mergeable with previous compatible paint operations.
 *
 * If \a missingTilesets is given, the listed tilesets will be added to the map
 * on the first paint operation. The list will then be cleared.
 *
 * If \a paintedRegions is given, then no regionEdited signal is emitted.
 * In this case it is the responsibility of the caller to emit this signal for
 * each affected tile layer.
 */
void MapDocument::paintTileLayers(const Map *map, bool mergeable,
                                  QVector<SharedTileset> *missingTilesets,
                                  QHash<TileLayer*, QRegion> *paintedRegions)
{
    TileLayer *currentTileLayer = mCurrentLayer ? mCurrentLayer->asTileLayer() : nullptr;

    LayerIterator it(map, Layer::TileLayerType);
    const bool isMultiLayer = it.next() && it.next();

    it.toFront();
    while (auto tileLayer = static_cast<TileLayer*>(it.next())) {
        TileLayer *targetLayer = currentTileLayer;
        bool addLayer = false;

        // When the map contains only a single layer, always paint it into
        // the current layer. This makes sure you can still take pieces from
        // one layer and draw them into another.
        if (isMultiLayer && !tileLayer->name().isEmpty()) {
            targetLayer = static_cast<TileLayer*>(mMap->findLayer(tileLayer->name(), Layer::TileLayerType));
            if (!targetLayer) {
                // Create a layer with this name
                targetLayer = new TileLayer(tileLayer->name(), 0, 0,
                                            mMap->width(),
                                            mMap->height());
                addLayer = true;
            }
        }

        if (!targetLayer)
            continue;
        if (!targetLayer->isUnlocked())
            continue;
        if (!mMap->infinite() && !targetLayer->rect().intersects(tileLayer->bounds()))
            continue;

        PaintTileLayer *paint = new PaintTileLayer(this,
                                                   targetLayer,
                                                   tileLayer->x(),
                                                   tileLayer->y(),
                                                   tileLayer);

        if (missingTilesets && !missingTilesets->isEmpty()) {
            for (const SharedTileset &tileset : *missingTilesets) {
                if (!mMap->tilesets().contains(tileset))
                    new AddTileset(this, tileset, paint);
            }

            missingTilesets->clear();
        }

        if (addLayer) {
            new AddLayer(this,
                         mMap->layerCount(), targetLayer, nullptr,
                         paint);
        }

        paint->setMergeable(mergeable);
        undoStack()->push(paint);

        const QRegion editedRegion = tileLayer->region();
        if (paintedRegions)
            (*paintedRegions)[targetLayer] |= editedRegion;
        else
            emit regionEdited(editedRegion, targetLayer);

        mergeable = true; // further paints are always mergeable
    }
}

void MapDocument::replaceObjectTemplate(const ObjectTemplate *oldObjectTemplate,
                                        const ObjectTemplate *newObjectTemplate)
{
    auto changedObjects = mMap->replaceObjectTemplate(oldObjectTemplate, newObjectTemplate);

    // Update the objects in the map scene
    emit objectsChanged(changedObjects);
    emit objectTemplateReplaced(newObjectTemplate, oldObjectTemplate);
}

void MapDocument::setSelectedArea(const QRegion &selection)
{
    if (mSelectedArea != selection) {
        const QRegion oldSelectedArea = mSelectedArea;
        mSelectedArea = selection;
        emit selectedAreaChanged(mSelectedArea, oldSelectedArea);
    }
}

static QList<MapObject *> sortObjects(const Map &map, const QList<MapObject *> &objects)
{
    QList<MapObject *> sorted;
    sorted.reserve(objects.size());

    LayerIterator iterator(&map);
    while (Layer *layer = iterator.next()) {
        if (layer->layerType() != Layer::ObjectGroupType)
            continue;

        for (MapObject *mapObject : static_cast<ObjectGroup*>(layer)->objects()) {
            if (objects.contains(mapObject))
                sorted.append(mapObject);
        }
    }

    return sorted;
}

/**
 * Returns the list of selected objects, in their display order (when
 * ObjectGroup::IndexOrder is used).
 */
QList<MapObject *> MapDocument::selectedObjectsOrdered() const
{
    return sortObjects(*mMap, mSelectedObjects);
}

void MapDocument::setSelectedObjects(const QList<MapObject *> &selectedObjects)
{
    mSelectedObjects = selectedObjects;
    emit selectedObjectsChanged();

    ObjectGroup *singleObjectGroup = nullptr;
    for (MapObject *object : selectedObjects) {
        ObjectGroup *currentObjectGroup = object->objectGroup();

        if (!singleObjectGroup) {
            singleObjectGroup = currentObjectGroup;
        } else if (singleObjectGroup != currentObjectGroup) {
            singleObjectGroup = nullptr;
            break;
        }
    }

    // Switch the current object layer if only one object layer (and/or its objects)
    // are included in the current selection.
    if (singleObjectGroup)
        setCurrentLayer(singleObjectGroup);

    if (selectedObjects.size() == 1)
        setCurrentObject(selectedObjects.first());
}

QList<Object*> MapDocument::currentObjects() const
{
    if (mCurrentObject && mCurrentObject->typeId() == Object::MapObjectType && !mSelectedObjects.isEmpty()) {
        QList<Object*> objects;
        for (MapObject *mapObj : mSelectedObjects)
            objects.append(mapObj);
        return objects;
    }

    return Document::currentObjects();
}

void MapDocument::setHoveredMapObject(MapObject *object)
{
    if (mHoveredMapObject == object)
        return;

    MapObject *previous = mHoveredMapObject;
    mHoveredMapObject = object;
    emit hoveredMapObjectChanged(object, previous);
}

/**
 * Makes sure the all tilesets which are used at the given \a map will be
 * present in the map document.
 *
 * To reach the aim, all similar tilesets will be replaced by the version
 * in the current map document and all missing tilesets will be added to
 * the current map document.
 */
void MapDocument::unifyTilesets(Map *map)
{
    QList<QUndoCommand*> undoCommands;
    QVector<SharedTileset> availableTilesets = mMap->tilesets();

    // Iterate over a copy because map->replaceTileset may invalidate iterator
    const QVector<SharedTileset> tilesets = map->tilesets();
    for (const SharedTileset &tileset : tilesets) {
        if (availableTilesets.contains(tileset))
            continue;

        SharedTileset replacement = tileset->findSimilarTileset(availableTilesets);
        if (!replacement) {
            undoCommands.append(new AddTileset(this, tileset));
            availableTilesets.append(tileset);
            continue;
        }

        // Merge the tile properties
        for (Tile *replacementTile : replacement->tiles()) {
            if (Tile *originalTile = tileset->findTile(replacementTile->id())) {
                Properties properties = replacementTile->properties();
                properties.merge(originalTile->properties());
                undoCommands.append(new ChangeProperties(this,
                                                         tr("Tile"),
                                                         replacementTile,
                                                         properties));
            }
        }

        map->replaceTileset(tileset, replacement);
    }

    if (!undoCommands.isEmpty()) {
        mUndoStack->beginMacro(tr("Tileset Changes"));
        const auto &commands = undoCommands;
        for (QUndoCommand *command : commands)
            mUndoStack->push(command);
        mUndoStack->endMacro();
    }
}

/**
 * Replaces tilesets in \a map by similar tilesets in this map when possible,
 * and adds tilesets to \a missingTilesets whenever there is a tileset without
 * replacement in this map.
 */
void MapDocument::unifyTilesets(Map *map, QVector<SharedTileset> &missingTilesets)
{
    QVector<SharedTileset> availableTilesets = mMap->tilesets();
    for (const SharedTileset &tileset : qAsConst(missingTilesets))
        if (!availableTilesets.contains(tileset))
            availableTilesets.append(tileset);

    // Iterate over a copy because map->replaceTileset may invalidate iterator
    const QVector<SharedTileset> tilesets = map->tilesets();
    for (const SharedTileset &tileset : tilesets) {
        // tileset already added
        if (availableTilesets.contains(tileset))
            continue;

        SharedTileset replacement = tileset->findSimilarTileset(availableTilesets);

        // tileset not present and no replacement tileset found
        if (!replacement) {
            missingTilesets.append(tileset);
            availableTilesets.append(tileset);
            continue;
        }

        // replacement tileset found, change given map
        map->replaceTileset(tileset, replacement);
    }
}

bool MapDocument::templateAllowed(const ObjectTemplate *objectTemplate) const
{
    if (!objectTemplate->object())
        return false;
    if (objectTemplate->object()->isTileObject() && !mAllowTileObjects)
        return false;

    return true;
}

/**
 * Before forwarding the signal, the objects are removed from the list of
 * selected objects, triggering a selectedObjectsChanged signal when
 * appropriate.
 */
void MapDocument::onObjectsRemoved(const QList<MapObject*> &objects)
{
    if (mHoveredMapObject && objects.contains(mHoveredMapObject))
        setHoveredMapObject(nullptr);

    deselectObjects(objects);
    emit objectsRemoved(objects);
}

void MapDocument::onMapObjectModelRowsInserted(const QModelIndex &parent,
                                               int first, int last)
{
    ObjectGroup *objectGroup = mMapObjectModel->toObjectGroup(parent);
    if (!objectGroup) // we're not dealing with insertion of objects
        return;

    emit objectsInserted(objectGroup, first, last);
    onMapObjectModelRowsInsertedOrRemoved(parent, first, last);
}

void MapDocument::onMapObjectModelRowsInsertedOrRemoved(const QModelIndex &parent,
                                                        int first, int last)
{
    Q_UNUSED(first)

    ObjectGroup *objectGroup = mMapObjectModel->toObjectGroup(parent);
    if (!objectGroup)
        return;

    // Inserting or removing objects changes the index of any that come after
    const int lastIndex = objectGroup->objectCount() - 1;
    if (last < lastIndex)
        emit objectsIndexChanged(objectGroup, last + 1, lastIndex);
}

void MapDocument::onObjectsMoved(const QModelIndex &parent, int start, int end,
                                 const QModelIndex &destination, int row)
{
    if (parent != destination)
        return;

    ObjectGroup *objectGroup = mMapObjectModel->toObjectGroup(parent);

    // Determine the full range over which object indexes changed
    const int first = qMin(start, row);
    const int last = qMax(end, row - 1);

    emit objectsIndexChanged(objectGroup, first, last);
}

void MapDocument::onLayerAdded(Layer *layer)
{
    emit layerAdded(layer);

    // Select the first layer that gets added to the map
    if (mMap->layerCount() == 1 && mMap->layerAt(0) == layer)
        setCurrentLayer(layer);
}

static void collectObjects(Layer *layer, QList<MapObject*> &objects)
{
    switch (layer->layerType()) {
    case Layer::ObjectGroupType:
        objects.append(static_cast<ObjectGroup*>(layer)->objects());
        break;
    case Layer::GroupLayerType:
        for (auto childLayer : *static_cast<GroupLayer*>(layer))
            collectObjects(childLayer, objects);
        break;
    case Layer::ImageLayerType:
    case Layer::TileLayerType:
        break;
    }
}

void MapDocument::onLayerAboutToBeRemoved(GroupLayer *groupLayer, int index)
{
    Layer *layer = groupLayer ? groupLayer->layerAt(index) : mMap->layerAt(index);

    // Deselect any objects on this layer when necessary
    if (layer->isObjectGroup() || layer->isGroupLayer()) {
        QList<MapObject*> objects;
        collectObjects(layer, objects);
        deselectObjects(objects);

        if (mHoveredMapObject && objects.contains(mHoveredMapObject))
            setHoveredMapObject(nullptr);
    }

    emit layerAboutToBeRemoved(groupLayer, index);
}

void MapDocument::onLayerRemoved(Layer *layer)
{
    if (mCurrentLayer && mCurrentLayer->isParentOrSelf(layer)) {
        // Assumption: the current object is either not a layer, or it is the current layer.
        if (mCurrentObject == mCurrentLayer)
            setCurrentObject(nullptr);

        setCurrentLayer(nullptr);
    }

    emit layerRemoved(layer);
}

void MapDocument::updateTemplateInstances(const ObjectTemplate *objectTemplate)
{
    QList<MapObject*> objectList;
    for (Layer *layer : mMap->objectGroups()) {
        for (auto object : static_cast<ObjectGroup*>(layer)->objects()) {
            if (object->objectTemplate() == objectTemplate) {
                object->syncWithTemplate();
                objectList.append(object);
            }
        }
    }
    emit objectsChanged(objectList);
}

void MapDocument::selectAllInstances(const ObjectTemplate *objectTemplate)
{
    QList<MapObject*> objectList;
    for (Layer *layer : mMap->objectGroups()) {
        for (auto object : static_cast<ObjectGroup*>(layer)->objects())
            if (object->objectTemplate() == objectTemplate)
                objectList.append(object);
    }
    setSelectedObjects(objectList);
}

/**
 * Deselects the given list of \a objects.
 *
 * If any of the given objects is the "current" object, the current object
 * is reset as well.
 */
void MapDocument::deselectObjects(const QList<MapObject *> &objects)
{
    // Unset the current object when it was part of this list of objects
    if (mCurrentObject && mCurrentObject->typeId() == Object::MapObjectType)
        if (objects.contains(static_cast<MapObject*>(mCurrentObject)))
            setCurrentObject(nullptr);

    int removedCount = 0;
    for (MapObject *object : objects)
        removedCount += mSelectedObjects.removeAll(object);

    if (removedCount > 0)
        emit selectedObjectsChanged();
}

void MapDocument::duplicateObjects(const QList<MapObject *> &objects)
{
    if (objects.isEmpty())
        return;

    QVector<AddMapObjects::Entry> objectsToAdd;
    objectsToAdd.reserve(objects.size());

    for (const MapObject *mapObject : objects) {
        MapObject *clone = mapObject->clone();
        clone->resetId();
        objectsToAdd.append(AddMapObjects::Entry { clone, mapObject->objectGroup() });
    }

    auto command = new AddMapObjects(this, objectsToAdd);
    command->setText(tr("Duplicate %n Object(s)", "", objects.size()));

    mUndoStack->push(command);

    setSelectedObjects(AddMapObjects::objects(objectsToAdd));
}

void MapDocument::removeObjects(const QList<MapObject *> &objects)
{
    if (objects.isEmpty())
        return;

    auto command = new RemoveMapObjects(this, objects);
    command->setText(tr("Remove %n Object(s)", "", objects.size()));

    mUndoStack->push(command);
}

void MapDocument::moveObjectsToGroup(const QList<MapObject *> &objects,
                                     ObjectGroup *objectGroup)
{
    if (objects.isEmpty())
        return;

    mUndoStack->beginMacro(tr("Move %n Object(s) to Layer", "",
                              objects.size()));

    const auto objectsToMove = sortObjects(*mMap, objects);
    for (MapObject *mapObject : objectsToMove) {
        if (mapObject->objectGroup() == objectGroup)
            continue;

        mUndoStack->push(new MoveMapObjectToGroup(this,
                                                  mapObject,
                                                  objectGroup));
    }
    mUndoStack->endMacro();
}

typedef QMap<ObjectGroup*, RangeSet<int>>           Ranges;
typedef QMapIterator<ObjectGroup*, RangeSet<int>>   RangesIterator;

static Ranges computeRanges(const QList<MapObject *> &objects)
{
    Ranges ranges;

    for (MapObject *object : objects) {
        ObjectGroup *group = object->objectGroup();
        auto &set = ranges[group];
        set.insert(group->objects().indexOf(object));
    }

    return ranges;
}

void MapDocument::moveObjectsUp(const QList<MapObject *> &objects)
{
    if (objects.isEmpty())
        return;

    const auto ranges = computeRanges(objects);

    std::unique_ptr<QUndoCommand> command(new QUndoCommand(tr("Move %n Object(s) Up",
                                                              "", objects.size())));

    RangesIterator rangesIterator(ranges);
    while (rangesIterator.hasNext()) {
        rangesIterator.next();

        ObjectGroup *group = rangesIterator.key();
        const RangeSet<int> &rangeSet = rangesIterator.value();

        const RangeSet<int>::Range it_begin = rangeSet.begin();
        RangeSet<int>::Range it = rangeSet.end();
        Q_ASSERT(it != it_begin);

        do {
            --it;

            int from = it.first();
            int count = it.length();
            int to = from + count + 1;

            if (to <= group->objectCount())
                new ChangeMapObjectsOrder(this, group, from, to, count, command.get());

        } while (it != it_begin);
    }

    if (command->childCount() > 0)
        mUndoStack->push(command.release());
}

void MapDocument::moveObjectsDown(const QList<MapObject *> &objects)
{
    if (objects.isEmpty())
        return;

    std::unique_ptr<QUndoCommand> command(new QUndoCommand(tr("Move %n Object(s) Down",
                                                              "", objects.size())));

    RangesIterator rangesIterator(computeRanges(objects));
    while (rangesIterator.hasNext()) {
        rangesIterator.next();

        ObjectGroup *group = rangesIterator.key();
        const RangeSet<int> &rangeSet = rangesIterator.value();

        RangeSet<int>::Range it = rangeSet.begin();
        const RangeSet<int>::Range it_end = rangeSet.end();

        for (; it != it_end; ++it) {
            int from = it.first();

            if (from > 0) {
                int to = from - 1;
                int count = it.length();

                new ChangeMapObjectsOrder(this, group, from, to, count, command.get());
            }
        }
    }

    if (command->childCount() > 0)
        mUndoStack->push(command.release());
}

void MapDocument::detachObjects(const QList<MapObject *> &objects)
{
    if (objects.isEmpty())
        return;

    mUndoStack->push(new DetachObjects(this, objects));
}

void MapDocument::createRenderer()
{
    switch (mMap->orientation()) {
    case Map::Isometric:
        mRenderer.reset(new IsometricRenderer(mMap.get()));
        break;
    case Map::Staggered:
        mRenderer.reset(new StaggeredRenderer(mMap.get()));
        break;
    case Map::Hexagonal:
        mRenderer.reset(new HexagonalRenderer(mMap.get()));
        break;
    default:
        mRenderer.reset(new OrthogonalRenderer(mMap.get()));
        break;
    }
}
