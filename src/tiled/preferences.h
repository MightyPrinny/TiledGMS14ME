/*
 * preferences.h
 * Copyright 2009-2011, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 * Copyright 2016, Mamed Ibrahimov <ibramlab@gmail.com>
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

#include <QColor>
#include <QDate>
#include <QObject>

#include "map.h"
#include "objecttypes.h"

class QSettings;

namespace Tiled {
namespace Internal {

/**
 * This class holds user preferences and provides a convenient interface to
 * access them.
 */
class Preferences : public QObject
{
    Q_OBJECT

public:
    static Preferences *instance();
    static void deleteInstance();

    bool showGrid() const { return mShowGrid; }
    bool showQuads() const { return mShowQuads; }
    bool showTileObjectOutlines() const { return mShowTileObjectOutlines; }
    bool showTileAnimations() const { return mShowTileAnimations; }
    bool snapToGrid() const { return mSnapToGrid; }
    bool snapToFineGrid() const { return mSnapToFineGrid; }
    bool snapToPixels() const { return mSnapToPixels; }
	bool defaultCombineTiles() const {return mDefaultCombineTiles;}
    QColor gridColor() const { return mGridColor; }
    QColor quadColor() const { return mQuadColor; }
    int gridFine() const { return mGridFine; }
    qreal objectLineWidth() const { return mObjectLineWidth; }
    QSize snapGrid();

	QString executablePath(){return mExecutablePath;}
	QString gmProjectPath(){return mGMProjectPath;}
    QString gmProjectFilePath(){return mGMProjectFilePath;}
	QString genTemplatesOutDir(){return mGenTemplatesOutDir;}
	bool autoFlipObjectSetter(){return mAutoFlipObjectSetter;}

    bool highlightCurrentLayer() const { return mHighlightCurrentLayer; }
    bool highlightHoveredObject() const;
    bool showTilesetGrid() const { return mShowTilesetGrid; }

    enum ObjectLabelVisiblity {
        NoObjectLabels,
        SelectedObjectLabels,
        AllObjectLabels
    };

    ObjectLabelVisiblity objectLabelVisibility() const;
    void setObjectLabelVisibility(ObjectLabelVisiblity visiblity);

    bool labelForHoveredObject() const;
    void setLabelForHoveredObject(bool enabled);

    enum ApplicationStyle {
        SystemDefaultStyle,
        FusionStyle,
        TiledStyle
    };

    ApplicationStyle applicationStyle() const;
    void setApplicationStyle(ApplicationStyle style);

    QColor baseColor() const;
    void setBaseColor(const QColor &color);

    QColor selectionColor() const;
    void setSelectionColor(const QColor &color);

    Map::LayerDataFormat layerDataFormat() const;
    void setLayerDataFormat(Map::LayerDataFormat layerDataFormat);

    Map::RenderOrder mapRenderOrder() const;
    void setMapRenderOrder(Map::RenderOrder mapRenderOrder);

    bool dtdEnabled() const;
    void setDtdEnabled(bool enabled);

    bool safeSavingEnabled() const;
    void setSafeSavingEnabled(bool enabled);

    QString language() const;
    void setLanguage(const QString &language);

    bool reloadTilesetsOnChange() const;
    void setReloadTilesetsOnChanged(bool value);

    bool useOpenGL() const { return mUseOpenGL; }
    void setUseOpenGL(bool useOpenGL);

    void setObjectTypes(const ObjectTypes &objectTypes);

    enum FileType {
        ObjectTypesFile,
        ObjectTemplateFile,
        ImageFile,
        ExportedFile,
        ExternalTileset,
        WorldFile
    };

    QString lastPath(FileType fileType) const;
    void setLastPath(FileType fileType, const QString &path);

    bool automappingDrawing() const { return mAutoMapDrawing; }

    QString mapsDirectory() const;
    void setMapsDirectory(const QString &path);

    QString stampsDirectory() const;
    void setStampsDirectory(const QString &stampsDirectory);

    QString templatesDirectory() const;
	void setTemplatesDirectory(const QString &path, bool forceEmitChanged = false);

    QString objectTypesFile() const;
    void setObjectTypesFile(const QString &filePath);

	QSize lastUsedMapTileSize();
	QSize lastUsedQuadSize();
	QSize lastUsedTilesetTileSize();

	void setLastUsedMapTileSize(QSize size);
	void setLastUsedQuadSize(QSize size);
	void setLastUsedTilesetTileSize(QSize size);

    QDate firstRun() const;
    int runCount() const;

    bool isPatron() const;
    void setPatron(bool isPatron);

    bool shouldShowPatreonDialog() const;
    void setPatreonDialogReminder(const QDate &date);

    enum { MaxRecentFiles = 8 };
    QStringList recentFiles() const;
    QString fileDialogStartLocation() const;
    void addRecentFile(const QString &fileName);

    bool openLastFilesOnStartup() const;

    bool checkForUpdates() const;
    void setCheckForUpdates(bool on);

    bool wheelZoomsByDefault() const;

    /**
     * Provides access to the QSettings instance to allow storing/retrieving
     * arbitrary values. The naming style for groups and keys is CamelCase.
     */
    QSettings *settings() const { return mSettings; }

public slots:
    void browseExecutablePath();
	void browseGMProjectPath();
    void browseGMProjectFile();
	void browseGenTemplateOutDir();
	void setAutoFlipObjectSetter(bool value);
    void setShowGrid(bool showGrid);
    void setShowQuads(bool showQuads);
    void setShowTileObjectOutlines(bool enabled);
    void setShowTileAnimations(bool enabled);
    void setSnapToGrid(bool snapToGrid);
    void setSnapToFineGrid(bool snapToFineGrid);
    void setSnapToPixels(bool snapToPixels);
    void setGridColor(QColor gridColor);
	void setDefaultCombineTiles(bool value);
    void setQuadColor(QColor quadColor);
    void setGridFine(int gridFine);
    void setSnapGrid();
    void setObjectLineWidth(qreal lineWidth);
    void setHighlightCurrentLayer(bool highlight);
    void setHighlightHoveredObject(bool highlight);
    void setShowTilesetGrid(bool showTilesetGrid);
    void setAutomappingDrawing(bool enabled);
    void setOpenLastFilesOnStartup(bool load);
    void setPluginEnabled(const QString &fileName, bool enabled);
    void setWheelZoomsByDefault(bool mode);

    void clearRecentFiles();

signals:
    void showGridChanged(bool showGrid);
    void showQuadsChanged(bool showQuads);
    void showTileObjectOutlinesChanged(bool enabled);
	void setAutoFlipObjectSetterChanged(bool enabled);
    void showTileAnimationsChanged(bool enabled);
    void snapToGridChanged(bool snapToGrid);
    void snapToFineGridChanged(bool snapToFineGrid);
    void snapToPixelsChanged(bool snapToPixels);
	void defaultCombineTilesChanged(bool value);
    void gridColorChanged(QColor gridColor);
    void quadColorChanged(QColor gridColor);
    void gridFineChanged(int gridFine);
    void objectLineWidthChanged(qreal lineWidth);
    void highlightCurrentLayerChanged(bool highlight);
    void highlightHoveredObjectChanged(bool highlight);
    void showTilesetGridChanged(bool showTilesetGrid);
    void objectLabelVisibilityChanged(ObjectLabelVisiblity);
    void labelForHoveredObjectChanged(bool enabled);
    void executablePathChanged(QString path);
	void gmProjectPathChanged(QString path);
    void gmProjectFilePathChanged(QString path);
	void genTemplatesOutDirChanged(QString path);


    void applicationStyleChanged(ApplicationStyle);
    void baseColorChanged(const QColor &baseColor);
    void selectionColorChanged(const QColor &selectionColor);

    void useOpenGLChanged(bool useOpenGL);

    void languageChanged();

    void objectTypesChanged();

    void mapsDirectoryChanged();
    void stampsDirectoryChanged(const QString &stampsDirectory);
	void templatesDirectoryChanged(const QString &templatesDirectory);

    void isPatronChanged();

    void recentFilesChanged();

    void checkForUpdatesChanged();

private:
    Preferences();
    ~Preferences();

    bool boolValue(const char *key, bool def = false) const;
    QColor colorValue(const char *key, const QColor &def = QColor()) const;
    QString stringValue(const char *key, const QString &def = QString()) const;
    int intValue(const char *key, int defaultValue) const;
    qreal realValue(const char *key, qreal defaultValue) const;

    QSettings *mSettings;

    bool mShowGrid;
    bool mShowQuads;
    bool mShowTileObjectOutlines;
    bool mShowTileAnimations;
    bool mSnapToGrid;
    bool mSnapToFineGrid;
    bool mSnapToPixels;
	bool mAutoFlipObjectSetter;
	bool mDefaultCombineTiles;
    int mSnapGridHeigth;
    int mSnapGridWidth;
    QColor mGridColor;
    QColor mQuadColor;
    int mGridFine;
    qreal mObjectLineWidth;
    bool mHighlightCurrentLayer;
    bool mHighlightHoveredObject;
    bool mShowTilesetGrid;
    bool mOpenLastFilesOnStartup;
    ObjectLabelVisiblity mObjectLabelVisibility;
    bool mLabelForHoveredObject;
    ApplicationStyle mApplicationStyle;
    QColor mBaseColor;
    QColor mSelectionColor;

    Map::LayerDataFormat mLayerDataFormat;
    Map::RenderOrder mMapRenderOrder;
    bool mDtdEnabled;
    bool mSafeSavingEnabled;
    QString mLanguage;
    bool mReloadTilesetsOnChange;
    bool mUseOpenGL;

    bool mAutoMapDrawing;

    QString mExecutablePath;
	QString mGMProjectPath;
    QString mGMProjectFilePath;
	QString mGenTemplatesOutDir;

    QString mMapsDirectory;
    QString mStampsDirectory;
    QString mTemplatesDirectory;
    QString mObjectTypesFile;

    QDate mFirstRun;
    QDate mPatreonDialogTime;
    int mRunCount;
    bool mIsPatron;
    bool mCheckForUpdates;
    bool mWheelZoomsByDefault;

    static Preferences *mInstance;
};


inline Preferences::ApplicationStyle Preferences::applicationStyle() const
{
    return mApplicationStyle;
}

inline QColor Preferences::baseColor() const
{
    return mBaseColor;
}

inline QColor Preferences::selectionColor() const
{
    return mSelectionColor;
}

inline bool Preferences::safeSavingEnabled() const
{
    return mSafeSavingEnabled;
}

inline bool Preferences::highlightHoveredObject() const
{
    return mHighlightHoveredObject;
}

inline Preferences::ObjectLabelVisiblity Preferences::objectLabelVisibility() const
{
    return mObjectLabelVisibility;
}

inline bool Preferences::labelForHoveredObject() const
{
    return mLabelForHoveredObject;
}

inline QDate Preferences::firstRun() const
{
    return mFirstRun;
}

inline int Preferences::runCount() const
{
    return mRunCount;
}

inline bool Preferences::isPatron() const
{
    return mIsPatron;
}

inline bool Preferences::checkForUpdates() const
{
    return mCheckForUpdates;
}

inline bool Preferences::openLastFilesOnStartup() const
{
    return mOpenLastFilesOnStartup;
}

inline bool Preferences::wheelZoomsByDefault() const
{
    return mWheelZoomsByDefault;
}

} // namespace Internal
} // namespace Tiled
