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

#pragma once

#include "mapformat.h"
#include "tilesetformat.h"
#include "gmx_global.h"
#include <QIODevice>
#include <QDir>
#include <QImage>
#include <unordered_map>
#include <plugin.h>

namespace Gmx {

class GMXSHARED_EXPORT GmxTopPlugin : public Tiled::Plugin
{
	Q_OBJECT
	Q_INTERFACES(Tiled::Plugin)
	Q_PLUGIN_METADATA(IID "org.mapeditor.Plugin" FILE "plugin.json")

public:
	void initialize() override;
};


class GMXSHARED_EXPORT GmxPlugin : public Tiled::MapFormat
{
    Q_OBJECT
    Q_INTERFACES(Tiled::MapFormat)

public:
	GmxPlugin(QObject *parent = nullptr);
	void writeAttribute(const QString &qualifiedName, QString &value, QIODevice* d, QTextCodec* codec);
    static void mapTemplates(std::unordered_map<std::string,std::string> *map, QDir &root );
	Tiled::Map *read(const QString &fileName, QSettings *) override;
	bool supportsFile(const QString &fileName) const override;
	bool write(const Tiled::Map *map, const QString &fileName) override;
    QString errorString() const override;
    QString shortName() const override;

protected:
    QString nameFilter() const override;

private:
    QString mError;
};

class GMXSHARED_EXPORT GmxTilesetPlugin : public Tiled::TilesetFormat
{
	Q_OBJECT
	Q_INTERFACES(Tiled::TilesetFormat)

public:
	GmxTilesetPlugin(QObject *parent = nullptr);

	Tiled::SharedTileset read(const QString &fileName) override;
	bool write (const Tiled::Tileset &tst, const QString &filename) override;
	bool supportsFile(const QString &fileName) const override;

	Capabilities capabilities() const override;
	QString nameFilter() const override;
	QString errorString() const override;
	QString shortName() const override;
protected:
	QString mError;


};

} // namespace Gmx
