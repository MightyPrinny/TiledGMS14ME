import qbs 1.0

TiledPlugin {
    cpp.defines: base.concat(["GMX_LIBRARY"])

    files: [
        "bgximporterdialog.cpp",
        "bgximporterdialog.h",
        "bgximporterdialog.ui",
        "gmx_global.h",
        "gmxplugin.cpp",
        "gmxplugin.h",
        "plugin.json",
        "rapidxml.hpp",
        "rapidxml_iterators.hpp",
        "rapidxml_print.hpp",
        "rapidxml_utils.hpp",
        "roomimporterdialog.cpp",
        "roomimporterdialog.h",
        "roomimporterdialog.ui",
    ]
    Depends { name: "Qt"; submodules: ["widgets"]; versionAtLeast: "5.5" }
}
