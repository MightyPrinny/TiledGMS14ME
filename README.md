This is a modified version of Tiled 1.1.5 made to edit Game Maker Studio 1.4 rooms, with some aditional features for MegamixEngine.


Quick start

-Go to preferences and set the path to your game maker project and a new folder where the templates will be generated

-Go to the "MegamixEngine" menu and click on "Generate templates"

-The dialog will open with the paths you set in preferences already set.

-Click ok and wait a bit, for some reason this takes quite a bit longer on Windows than on GNU/Linux, but it's still quite fast compared
to how long GMS1.4 takes to laod and save a project.



Remarks

-it's recomended that you duplicate a game maker room to make a new room.

-You have to regenerate your templates when your project changes.


Changes

-Option to generate object templates from a game maker project.

-Objects using templates will use their sprite origin for snapping and will be sorted by their 

-Maps now have a secondary grid, refered to as "quad size".

-Option to generate templates from .object.gmx files, which also generates a file with the object types(Note: this takes some time)

-The map's background color is exported properly.

-The GMXPlugin has been heavily modified to support importing game maker objects using Tiled's templates, it's also more compatible than the one in Tiled 1.1.5 and the rooms produced by it resemble a room generated by Game Maker Studio 1.4 a lot more.

-Option to run an executable passing the exported room as a parameter, meant to be used with MegamixEngine's external room loading feature.

-Animated tilesets will be exported for use in MegamixEngine, so don't use that for other projects unless you copy their objLayerAnimation object.


Outdated guide
https://github.com/MightyPrinny/TiledGMS14ME/wiki/


Tiled 1.1.5's Readme

Tiled Map Editor - http://www.mapeditor.org/

About Tiled
-------------------------------------------------------------------------------

Tiled is a general purpose tile map editor for all tile-based games, such as
RPGs, platformers or Breakout clones.

Tiled is highly flexible. It can be used to create maps of any size, with no
restrictions on tile size, or the number of layers or tiles that can be used.
Maps, layers, tiles, and objects can all be assigned arbitrary properties.
Tiled's map format (TMX) is easy to understand and allows multiple tilesets to
be used in any map. Tilesets can be modified at any time.

[![Build Status](https://travis-ci.org/bjorn/tiled.svg?branch=master)](https://travis-ci.org/bjorn/tiled)
[![Build status](https://ci.appveyor.com/api/projects/status/ceb79jn5cf99y3qd/branch/master?svg=true)](https://ci.appveyor.com/project/bjorn/tiled/branch/master)
[![Bountysource](https://www.bountysource.com/badge/tracker?tracker_id=52019)](https://www.bountysource.com/trackers/52019-tiled?utm_source=52019&utm_medium=shield&utm_campaign=TRACKER_BADGE)
[![Translation status](https://hosted.weblate.org/widgets/tiled/-/shields-badge.svg)](https://hosted.weblate.org/engage/tiled/?utm_source=widget)

About the Qt Version
-------------------------------------------------------------------------------

Tiled was originally written in Java. In 2008, work began to develop a faster,
better looking, and easier-to-use version of Tiled based on the Qt framework.
This decision was made as the Qt framework has a greater feature set than is
offered by the standard Java libraries.


Compiling
-------------------------------------------------------------------------------

Before you can compile Tiled, you must ensure the Qt (>= 5.6) development
libraries have been installed:

* On Ubuntu/Debian: `apt-get install qt5-default qttools5-dev-tools zlib1g-dev`
* On Fedora:        `sudo dnf builddep tiled`
* On Arch Linux:    `pacman -S qt`
* On Mac OS X with [Homebrew](http://brew.sh/):
  + `brew install qt5`
  + `brew link qt5 --force`
* Alternatively, you can [download Qt here](https://www.qt.io/download-qt-installer)

Next, compile by running:

    $ qmake (or qmake-qt5 on some systems)
    $ make

To perform a shadow build, run qmake from a different directory and refer
it to tiled.pro. For example:

    $ mkdir build
    $ cd build
    $ qmake ../tiled.pro
    $ make

You can now run Tiled using the executable in `bin/tiled`.

Installing
-------------------------------------------------------------------------------

To install Tiled, run `make install` from the terminal. By default, Tiled will
install itself to `/usr/local`.

The installation prefix can be changed when running qmake, or by changing the
install root when running `make install`. For example, to use an installation
prefix of  `/usr` instead of `/usr/local`:

    $ qmake -r PREFIX=/usr

Note: The -r recursive flag is required if you've run qmake before, as this
command will affect nested pro files)

To install Tiled to a packaging directory:

    $ make install INSTALL_ROOT=/tmp/tiled-pkg

By default, Tiled and its plugins are compiled with an Rpath that allows them
to find the shared *libtiled* library immediately after being compiled. When
packaging a Tiled map for distribution, the Rpath should be disabled by
appending `RPATH=no` to the qmake command.
