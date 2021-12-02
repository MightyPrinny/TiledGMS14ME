/*
 * tilesetformat.h
 * Copyright 2015, Thorbjørn Lindeijer <bjorn@lindeijer.nl>
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

#pragma once

#include "fileformat.h"
#include "tileset.h"
#include <QSettings>

namespace Tiled {

/**
 * An interface to be implemented for adding support for a tileset format to
 * Tiled. It can implement support for either loading or saving to a certain
 * tileset format, or both.
 */
class TILEDSHARED_EXPORT TilesetFormat : public FileFormat
{
    Q_OBJECT
    Q_INTERFACES(Tiled::FileFormat)

public:
    explicit TilesetFormat(QObject *parent = nullptr)
        : FileFormat(parent)
    {}

    /**
     * Reads the tileset and returns a new Tileset instance, or a null shared
     * pointer if reading failed.
     */
	virtual SharedTileset read(const QString &fileName, QSettings *prefs = nullptr) = 0;

    /**
     * Writes the given \a tileset based on the suggested \a fileName.
     *
     * @return <code>true</code> on success, <code>false</code> when an error
     *         occurred. The error can be retrieved by errorString().
     */
    virtual bool write(const Tileset &tileset, const QString &fileName) = 0;

private:
    QPointer<TilesetFormat> mExportFormat;
};

} // namespace Tiled

Q_DECLARE_INTERFACE(Tiled::TilesetFormat, "org.mapeditor.TilesetFormat")

namespace Tiled {

/**
 * Convenience class for adding a format that can only be written.
 */
class TILEDSHARED_EXPORT WritableTilesetFormat : public TilesetFormat
{
    Q_OBJECT
    Q_INTERFACES(Tiled::TilesetFormat)

public:
    explicit WritableTilesetFormat(QObject *parent = nullptr)
        : TilesetFormat(parent)
    {}

    Capabilities capabilities() const override { return Write; }
	SharedTileset read(const QString &, QSettings *prefs = nullptr) override { return SharedTileset(); }
    bool supportsFile(const QString &) const override { return false; }
};

/**
 * Attempt to read the given tileset using any of the tileset formats added
 * to the plugin manager, falling back to the TSX format if none are capable.
 */
TILEDSHARED_EXPORT SharedTileset readTileset(const QString &fileName,
                                             QString *error = nullptr);

/**
 * Attempts to find a tileset format supporting the given file.
 */
TILEDSHARED_EXPORT TilesetFormat *findSupportingTilesetFormat(const QString &fileName);

} // namespace Tiled

