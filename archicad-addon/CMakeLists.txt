cmake_minimum_required (VERSION 3.17)

include (Tools/CMakeCommon.cmake)

set_property (GLOBAL PROPERTY USE_FOLDERS ON)

set (CMAKE_CONFIGURATION_TYPES Debug;Release;RelWithDebInfo)
set (AC_API_DEVKIT_DIR ${AC_API_DEVKIT_DIR} CACHE PATH "API DevKit directory.")
set (AC_VERSION ${API_AC_VERSION} CACHE STRING "Archicad version.")

SetGlobalCompilerDefinitions (${AC_VERSION})

if (WIN32)
	set (NamePostFix Win)
else ()
	set (NamePostFix Mac)
endif ()
set (AddOnName "TapirAddOn_AC${AC_VERSION}_${NamePostFix}")

project (${AddOnName})

set (AddOnSourcesFolder Sources)
set (AddOnResourcesFolder Sources)
GenerateAddOnProject (AddOn ${AC_VERSION} ${AC_API_DEVKIT_DIR} ${AddOnName} ${AddOnSourcesFolder} ${AddOnResourcesFolder} INT)
# Notes 3D Imaginns (fork Imaginns): GDI+ draws the strokes on the captures, shell32 finds Documents.
if (WIN32)
    target_link_libraries (AddOn gdiplus shell32 ole32)
    # notes.json is read and written with the DevKit's RapidJSON, as "RapidJSON/document.h"
    target_include_directories (AddOn SYSTEM PRIVATE ${AC_API_DEVKIT_DIR}/Modules)
endif ()
