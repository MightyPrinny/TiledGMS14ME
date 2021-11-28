/*
 * abstractobjecttool.cpp
 * Copyright 2011, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
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

#include "abstractobjecttool.h"

#include "addremovetileset.h"
#include "changemapobject.h"
#include "documentmanager.h"
#include "mapdocument.h"
#include "map.h"
#include "mapobject.h"
#include "mapobjectitem.h"
#include "maprenderer.h"
#include "mapscene.h"
#include "objectgroup.h"
#include "preferences.h"
#include "raiselowerhelper.h"
#include "resizemapobject.h"
#include "templatemanager.h"
#include "tile.h"
#include "tmxmapformat.h"
#include "utils.h"

#include <QFileDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QToolBar>
#include <QUndoStack>

#include <QtMath>
#include <QDebug>

using namespace Tiled;
using namespace Tiled::Internal;

static bool isTileObject(MapObject *mapObject)
{
    return !mapObject->cell().isEmpty();
}

static bool isTemplateInstance(MapObject *mapObject)
{
    return mapObject->isTemplateInstance();
}

static bool isResizedTileObject(MapObject *mapObject)
{
    if (const auto tile = mapObject->cell().tile())
        return mapObject->size() != tile->size();
    return false;
}

static bool isChangedTemplateInstance(MapObject *mapObject)
{
    if (const MapObject *templateObject = mapObject->templateObject()) {
        return mapObject->changedProperties() != 0 ||
               mapObject->properties() != templateObject->properties();
    }
    return false;
}


AbstractObjectTool::AbstractObjectTool(const QString &name,
                                       const QIcon &icon,
                                       const QKeySequence &shortcut,
                                       QObject *parent)
    : AbstractTool(name, icon, shortcut, parent)
    , mMapScene(nullptr)
{
    QIcon flipHorizontalIcon(QLatin1String(":images/24x24/flip-horizontal.png"));
    QIcon flipVerticalIcon(QLatin1String(":images/24x24/flip-vertical.png"));
    QIcon rotateLeftIcon(QLatin1String(":images/24x24/rotate-left.png"));
    QIcon rotateRightIcon(QLatin1String(":images/24x24/rotate-right.png"));

    flipHorizontalIcon.addFile(QLatin1String(":images/32x32/flip-horizontal.png"));
    flipVerticalIcon.addFile(QLatin1String(":images/32x32/flip-vertical.png"));
    rotateLeftIcon.addFile(QLatin1String(":images/32x32/rotate-left.png"));
    rotateRightIcon.addFile(QLatin1String(":images/32x32/rotate-right.png"));

    mFlipHorizontal = new QAction(this);
    mFlipHorizontal->setIcon(flipHorizontalIcon);

    mFlipVertical = new QAction(this);
    mFlipVertical->setIcon(flipVerticalIcon);

    mRotateLeft = new QAction(this);
    mRotateLeft->setIcon(rotateLeftIcon);

    mRotateRight = new QAction(this);
    mRotateRight->setIcon(rotateRightIcon);

    connect(mFlipHorizontal, &QAction::triggered, this, &AbstractObjectTool::flipHorizontally);
    connect(mFlipVertical, &QAction::triggered, this, &AbstractObjectTool::flipVertically);
    connect(mRotateLeft, &QAction::triggered, this, &AbstractObjectTool::rotateLeft);
    connect(mRotateRight, &QAction::triggered, this, &AbstractObjectTool::rotateRight);

    AbstractObjectTool::languageChanged();
}

void AbstractObjectTool::activate(MapScene *scene)
{
    mMapScene = scene;
}

void AbstractObjectTool::deactivate(MapScene *)
{
    mMapScene = nullptr;
}

void AbstractObjectTool::keyPressed(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_PageUp:    raise(); return;
    case Qt::Key_PageDown:  lower(); return;
    case Qt::Key_Home:      raiseToTop(); return;
    case Qt::Key_End:       lowerToBottom(); return;
    case Qt::Key_D:
        if (event->modifiers() & Qt::ControlModifier) {
            duplicateObjects();
            return;
        }
        break;
    }

    event->ignore();
}

void AbstractObjectTool::mouseLeft()
{
    setStatusInfo(QString());
}

void AbstractObjectTool::mouseMoved(const QPointF &pos,
                                    Qt::KeyboardModifiers)
{
    // Take into account the offset of the current layer
    QPointF offsetPos = pos;
    if (Layer *layer = currentLayer())
        offsetPos -= layer->totalOffset();

    const QPoint pixelPos = offsetPos.toPoint();

    const QPointF tilePosF = mapDocument()->renderer()->screenToTileCoords(offsetPos);
    const int x = qFloor(tilePosF.x());
    const int y = qFloor(tilePosF.y());
    setStatusInfo(QString(QLatin1String("%1, %2 (%3, %4)")).arg(x).arg(y).arg(pixelPos.x()).arg(pixelPos.y()));
}

void AbstractObjectTool::mousePressed(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        showContextMenu(topMostMapObjectAt(event->scenePos()),
                        event->screenPos());
    }
}

void AbstractObjectTool::languageChanged()
{
    mFlipHorizontal->setToolTip(tr("Flip Horizontally"));
    mFlipVertical->setToolTip(tr("Flip Vertically"));
    mRotateLeft->setToolTip(QCoreApplication::translate("Tiled::Internal::StampActions", "Rotate Left"));
    mRotateRight->setToolTip(QCoreApplication::translate("Tiled::Internal::StampActions", "Rotate Right"));

    mFlipHorizontal->setShortcut(QKeySequence(QCoreApplication::translate("Tiled::Internal::StampActions", "X")));
    mFlipVertical->setShortcut(QKeySequence(QCoreApplication::translate("Tiled::Internal::StampActions", "Y")));
    mRotateLeft->setShortcut(QKeySequence(QCoreApplication::translate("Tiled::Internal::StampActions", "Shift+Z")));
    mRotateRight->setShortcut(QKeySequence(QCoreApplication::translate("Tiled::Internal::StampActions", "Z")));
}

void AbstractObjectTool::populateToolBar(QToolBar *toolBar)
{
    toolBar->addAction(mFlipHorizontal);
    toolBar->addAction(mFlipVertical);
    toolBar->addAction(mRotateLeft);
    toolBar->addAction(mRotateRight);
}

void AbstractObjectTool::updateEnabledState()
{
    setEnabled(currentObjectGroup() != nullptr);
}

ObjectGroup *AbstractObjectTool::currentObjectGroup() const
{
    if (!mapDocument())
        return nullptr;

    return dynamic_cast<ObjectGroup*>(mapDocument()->currentLayer());
}

QList<MapObject*> AbstractObjectTool::mapObjectsAt(const QPointF &pos) const
{
    const QList<QGraphicsItem *> &items = mMapScene->items(pos);

    QList<MapObject*> objectList;
    for (auto item : items) {
        if (!item->isEnabled())
            continue;

        MapObjectItem *objectItem = qgraphicsitem_cast<MapObjectItem*>(item);
        if (objectItem && objectItem->mapObject()->objectGroup()->isUnlocked())
            objectList.append(objectItem->mapObject());
    }
    return objectList;
}

MapObject *AbstractObjectTool::topMostMapObjectAt(const QPointF &pos) const
{
    const QList<QGraphicsItem *> &items = mMapScene->items(pos);

    for (QGraphicsItem *item : items) {
        if (!item->isEnabled())
            continue;

        MapObjectItem *objectItem = qgraphicsitem_cast<MapObjectItem*>(item);
        if (objectItem && objectItem->mapObject()->objectGroup()->isUnlocked())
            return objectItem->mapObject();
    }
    return nullptr;
}

void AbstractObjectTool::duplicateObjects()
{
    mapDocument()->duplicateObjects(mapDocument()->selectedObjects());
}

void AbstractObjectTool::removeObjects()
{
    mapDocument()->removeObjects(mapDocument()->selectedObjects());
}

void AbstractObjectTool::resetTileSize()
{
    QList<QUndoCommand*> commands;

    for (auto mapObject : mapDocument()->selectedObjects()) {
        if (!isResizedTileObject(mapObject))
            continue;

        commands << new ResizeMapObject(mapDocument(),
                                        mapObject,
                                        mapObject->cell().tile()->size(),
                                        mapObject->size());
    }

    if (!commands.isEmpty()) {
        QUndoStack *undoStack = mapDocument()->undoStack();
        undoStack->beginMacro(tr("Reset Tile Size"));
        for (auto command : commands)
            undoStack->push(command);
        undoStack->endMacro();
    }
}

static QString saveObjectTemplate(const MapObject *mapObject)
{
    FormatHelper<ObjectTemplateFormat> helper(FileFormat::ReadWrite);
    QString filter = helper.filter();
    QString selectedFilter = XmlObjectTemplateFormat().nameFilter();

    Preferences *prefs = Preferences::instance();
    QString suggestedFileName = prefs->lastPath(Preferences::ObjectTemplateFile);
    suggestedFileName += QLatin1Char('/');
    if (!mapObject->name().isEmpty())
        suggestedFileName += mapObject->name();
    else
        suggestedFileName += QCoreApplication::translate("Tiled::Internal::MainWindow", "untitled");
    suggestedFileName += QLatin1String(".tx");

    QWidget *parent = DocumentManager::instance()->widget()->window();
    QString fileName = QFileDialog::getSaveFileName(parent,
                                                    QCoreApplication::translate("Tiled::Internal::MainWindow", "Save Template"),
                                                    suggestedFileName,
                                                    filter,
                                                    &selectedFilter);

    if (fileName.isEmpty())
        return QString();

    ObjectTemplateFormat *format = helper.formatByNameFilter(selectedFilter);

    ObjectTemplate objectTemplate;
    objectTemplate.setObject(mapObject);

    if (!format->write(&objectTemplate, fileName)) {
        QMessageBox::critical(nullptr, QCoreApplication::translate("Tiled::Internal::MainWindow", "Error Saving Template"),
                              format->errorString());
        return QString();
    }

    prefs->setLastPath(Preferences::ObjectTemplateFile,
                       QFileInfo(fileName).path());

    return fileName;
}

void AbstractObjectTool::saveSelectedObject()
{
    auto object = mapDocument()->selectedObjects().first();
    QString fileName = saveObjectTemplate(object);
    if (fileName.isEmpty())
        return;

    // Convert the saved object into an instance
    if (ObjectTemplate *objectTemplate = TemplateManager::instance()->loadObjectTemplate(fileName))
        mapDocument()->undoStack()->push(new ReplaceObjectsWithTemplate(mapDocument(), { object }, objectTemplate));
}

void AbstractObjectTool::detachSelectedObjects()
{
    MapDocument *currentMapDocument = mapDocument();
    QList<MapObject *> templateInstances;

    /**
     * Stores the unique tilesets used by the templates
     * to avoid creating multiple undo commands for the same tileset
     */
    QSet<SharedTileset> sharedTilesets;

    for (MapObject *object : mapDocument()->selectedObjects()) {
        if (object->templateObject()) {
            templateInstances.append(object);

            if (Tile *tile = object->cell().tile())
                sharedTilesets.insert(tile->tileset()->sharedPointer());
        }
    }

    auto changeMapObjectCommand = new DetachObjects(currentMapDocument, templateInstances);

    // Add any missing tileset used by the templates to the map map before detaching
    for (SharedTileset sharedTileset : sharedTilesets) {
        if (!currentMapDocument->map()->tilesets().contains(sharedTileset))
            new AddTileset(currentMapDocument, sharedTileset, changeMapObjectCommand);
    }

    currentMapDocument->undoStack()->push(changeMapObjectCommand);
}

void AbstractObjectTool::replaceObjectsWithTemplate()
{
    mapDocument()->undoStack()->push(new ReplaceObjectsWithTemplate(mapDocument(),
                                                                    mapDocument()->selectedObjects(),
                                                                    objectTemplate()));
}

void AbstractObjectTool::resetInstances()
{
    QList<MapObject *> templateInstances;

    for (MapObject *object : mapDocument()->selectedObjects()) {
        if (object->templateObject())
            templateInstances.append(object);
    }

    mapDocument()->undoStack()->push(new ResetInstances(mapDocument(), templateInstances));
}

void AbstractObjectTool::changeTile()
{
    QList<MapObject*> tileObjects;

    MapDocument *currentMapDocument = mapDocument();

    for (auto object : currentMapDocument->selectedObjects())
        if (object->isTileObject())
            tileObjects.append(object);

    auto changeMapObjectCommand = new ChangeMapObjectsTile(currentMapDocument, tileObjects, tile());

    // Make sure the tileset is part of the document
    SharedTileset sharedTileset = tile()->tileset()->sharedPointer();
    if (!currentMapDocument->map()->tilesets().contains(sharedTileset))
        new AddTileset(currentMapDocument, sharedTileset, changeMapObjectCommand);

	currentMapDocument->undoStack()->push(changeMapObjectCommand);
}

template <typename T>
static T optionalProperty(const Object *object, const QString &name, const T &def)
{
	QVariant var = object->inheritedProperty(name);
	return var.isValid() ? var.value<T>() : def;
}

void AbstractObjectTool::computeGMSCoordinates()
{
	using namespace Tiled;
	auto doc = mapDocument();
	auto objects = doc->selectedObjects();
	for(int i=0; i<objects.size(); ++i)
	{
		MapObject *obj = objects[i];
		if(!obj)
			continue;

		if(!obj->inheritedProperty(QStringLiteral("originX")).isValid())
		{
			continue;
		}

		qDebug() << "Ass";

		QPointF pos = obj->position();
		qreal scaleX = 1;
		qreal scaleY = 1;
		qreal imageWidth = optionalProperty(obj,QStringLiteral("imageWidth"),-1);
		qreal imageHeigth = optionalProperty(obj,QStringLiteral("imageHeight"),-1);

		QPointF origin(optionalProperty(obj, QStringLiteral("originX"), 0.0),
					   optionalProperty(obj, QStringLiteral("originY"), 0.0));

		if (!obj->cell().isEmpty()) {
			// For tile objects we can support scaling and flipping, though
			// flipping in combination with rotation doesn't work in GameMaker.

			using namespace std;
			if (auto tile = obj->cell().tile()) {
				const QSize tileSize = tile->size();
				if(imageWidth==-1||imageHeigth==-1)
				{
					imageHeigth=tileSize.height();
					imageWidth=tileSize.width();
				}
				scaleX = abs(obj->width() / imageWidth) ;
				scaleY = abs(obj->height() / imageHeigth);


				if (obj->cell().flippedHorizontally()) {
					scaleX *= -1;
					//origin += QPointF((object->width()*scaleX) - 2 * origin.x(), 0);
				}
				if (obj->cell().flippedVertically()) {
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
			origin += QPointF(0, -obj->height());


		}
		else if(imageWidth!=-1 && imageHeigth!=-1){
			scaleX = obj->width() / imageWidth;
			scaleY = obj->height() / imageHeigth;
		}
		// Allow overriding the scale using custom properties
		scaleX = optionalProperty(obj, QStringLiteral("scaleX"), scaleX);
		scaleY = optionalProperty(obj, QStringLiteral("scaleY"), scaleY);

		// Adjust the position based on the origin
		QTransform transform;
		transform.rotate(obj->rotation());
		pos += transform.map(origin);



		doc->setProperty(obj, QStringLiteral("gmsX"), qRound(pos.x()));
		doc->setProperty(obj, QStringLiteral("gmsY"), qRound(pos.y()));

	}
}

void AbstractObjectTool::flipHorizontally()
{
    mapDocument()->flipSelectedObjects(FlipHorizontally);
}

void AbstractObjectTool::flipVertically()
{
    mapDocument()->flipSelectedObjects(FlipVertically);
}

void AbstractObjectTool::rotateLeft()
{
    mapDocument()->rotateSelectedObjects(RotateLeft);
}

void AbstractObjectTool::rotateRight()
{
    mapDocument()->rotateSelectedObjects(RotateRight);
}

void AbstractObjectTool::raise()
{
    RaiseLowerHelper(mMapScene).raise();
}

void AbstractObjectTool::lower()
{
    RaiseLowerHelper(mMapScene).lower();
}

void AbstractObjectTool::raiseToTop()
{
    RaiseLowerHelper(mMapScene).raiseToTop();
}

void AbstractObjectTool::lowerToBottom()
{
    RaiseLowerHelper(mMapScene).lowerToBottom();
}

/**
 * Shows the context menu for map objects. The menu allows you to duplicate and
 * remove the map objects, or to edit their properties.
 */
void AbstractObjectTool::showContextMenu(MapObject *clickedObject,
                                         QPoint screenPos)
{
    const QList<MapObject*> &selectedObjects = mapDocument()->selectedObjects();

    if (clickedObject && !selectedObjects.contains(clickedObject))
        mapDocument()->setSelectedObjects({ clickedObject });

    if (selectedObjects.isEmpty())
        return;

    QMenu menu;
    QAction *duplicateAction = menu.addAction(tr("Duplicate %n Object(s)", "", selectedObjects.size()),
                                              this, SLOT(duplicateObjects()));
    QAction *removeAction = menu.addAction(tr("Remove %n Object(s)", "", selectedObjects.size()),
                                           this, SLOT(removeObjects()));

    duplicateAction->setIcon(QIcon(QLatin1String(":/images/16x16/stock-duplicate-16.png")));
    removeAction->setIcon(QIcon(QLatin1String(":/images/16x16/edit-delete.png")));

    bool anyTileObjectSelected = std::any_of(selectedObjects.begin(),
                                             selectedObjects.end(),
                                             isTileObject);

    if (anyTileObjectSelected) {
        auto resetTileSizeAction = menu.addAction(tr("Reset Tile Size"), this, SLOT(resetTileSize()));
        resetTileSizeAction->setEnabled(std::any_of(selectedObjects.begin(),
                                                    selectedObjects.end(),
                                                    isResizedTileObject));

        auto changeTileAction = menu.addAction(tr("Replace Tile"), this, SLOT(changeTile()));
        changeTileAction->setEnabled(tile() && (!selectedObjects.first()->isTemplateBase() ||
                                                tile()->tileset()->isExternal()));
    }

    // Create action for replacing an object with a template
    auto replaceTemplateAction = menu.addAction(tr("Replace With Template"), this, SLOT(replaceObjectsWithTemplate()));
	menu.addAction(tr("Compute GMS Coordinates"), this, SLOT(computeGMSCoordinates()));
    auto selectedTemplate = objectTemplate();

    if (selectedTemplate) {
        QString name = QFileInfo(selectedTemplate->fileName()).fileName();
        replaceTemplateAction->setText(tr("Replace With Template \"%1\"").arg(name));
    }
    if (!selectedTemplate || !mapDocument()->templateAllowed(selectedTemplate))
        replaceTemplateAction->setEnabled(false);

    if (selectedObjects.size() == 1) {
        MapObject *currentObject = selectedObjects.first();

        if (!(currentObject->isTemplateBase() || currentObject->isTemplateInstance())) {
            const Cell cell = selectedObjects.first()->cell();
            // Saving objects with embedded tilesets is disabled
            if (cell.isEmpty() || cell.tileset()->isExternal())
                menu.addAction(tr("Save As Template"), this, SLOT(saveSelectedObject()));
        }

        if (currentObject->isTemplateBase()) { // Hide this operations for template base
            duplicateAction->setVisible(false);
            removeAction->setVisible(false);
            replaceTemplateAction->setVisible(false);
        }
    }

    bool anyTemplateInstanceSelected = std::any_of(selectedObjects.begin(),
                                                   selectedObjects.end(),
                                                   isTemplateInstance);

    if (anyTemplateInstanceSelected) {
        menu.addAction(tr("Detach"), this, SLOT(detachSelectedObjects()));

        auto resetToTemplateAction = menu.addAction(tr("Reset Template Instance(s)"), this, SLOT(resetInstances()));
        resetToTemplateAction->setEnabled(std::any_of(selectedObjects.begin(),
                                                      selectedObjects.end(),
                                                      isChangedTemplateInstance));
    }

    menu.addSeparator();
    menu.addAction(tr("Flip Horizontally"), this, SLOT(flipHorizontally()), QKeySequence(tr("X")));
    menu.addAction(tr("Flip Vertically"), this, SLOT(flipVertically()), QKeySequence(tr("Y")));

    ObjectGroup *sameObjectGroup = RaiseLowerHelper::sameObjectGroup(selectedObjects);
    if (sameObjectGroup && sameObjectGroup->drawOrder() == ObjectGroup::IndexOrder) {
        menu.addSeparator();
        menu.addAction(tr("Raise Object"), this, SLOT(raise()), QKeySequence(tr("PgUp")));
        menu.addAction(tr("Lower Object"), this, SLOT(lower()), QKeySequence(tr("PgDown")));
        menu.addAction(tr("Raise Object to Top"), this, SLOT(raiseToTop()), QKeySequence(tr("Home")));
        menu.addAction(tr("Lower Object to Bottom"), this, SLOT(lowerToBottom()), QKeySequence(tr("End")));
    }

    auto objectGroups = mapDocument()->map()->objectGroups();
    if (!objectGroups.isEmpty()) {
        menu.addSeparator();
        QMenu *moveToLayerMenu = menu.addMenu(tr("Move %n Object(s) to Layer",
                                                 "", selectedObjects.size()));
        for (Layer *layer : objectGroups) {
            ObjectGroup *objectGroup = static_cast<ObjectGroup*>(layer);
            QAction *action = moveToLayerMenu->addAction(objectGroup->name());
            action->setData(QVariant::fromValue(objectGroup));
            action->setEnabled(objectGroup != sameObjectGroup);
        }
    }

    menu.addSeparator();
    QIcon propIcon(QLatin1String(":images/16x16/document-properties.png"));
    QAction *propertiesAction = menu.addAction(propIcon,
                                               tr("Object &Properties..."));

    Utils::setThemeIcon(removeAction, "edit-delete");
    Utils::setThemeIcon(propertiesAction, "document-properties");

    QAction *action = menu.exec(screenPos);
    if (!action)
        return;

    if (action == propertiesAction) {
        MapObject *mapObject = selectedObjects.first();
        mapDocument()->setCurrentObject(mapObject);
        emit mapDocument()->editCurrentObject();
        return;
    }

    if (ObjectGroup *objectGroup = action->data().value<ObjectGroup*>()) {
        const auto selectedObjectsCopy = selectedObjects;
        mapDocument()->moveObjectsToGroup(selectedObjects, objectGroup);
        mapDocument()->setSelectedObjects(selectedObjectsCopy);
    }
}
