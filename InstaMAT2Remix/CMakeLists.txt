cmake_minimum_required(VERSION 3.16)

project(InstaMAT2Remix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
# NOTE: We do NOT link against Quick/QuickWidgets to avoid adding Qt6Quick.dll as a
# hard load-time dependency (which breaks plugin loading from .IMP packages).
# QML items are traversed at runtime via the QObject meta-system instead.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# Source files
set(SOURCES
    PluginMain.cpp
    CExports.cpp
    RemixConnector.cpp
    RemixNodes.cpp
    GuiManager.cpp
    ExternalTools.cpp
    SettingsDialog.cpp
    DiagnosticsDialog.cpp
    PluginPaths.cpp
    Logger.cpp
)

set(HEADERS
    RemixConnector.h
    RemixNodes.h
    GuiManager.h
    ExternalTools.h
    Utils.h
    SettingsDialog.h
    DiagnosticsDialog.h
    PluginPaths.h
    PluginInfo.h
    Logger.h
    InstaMATAPI.h
    InstaMATPluginAPI.h
)

# Define the library
add_library(InstaMAT2Remix SHARED ${SOURCES} ${HEADERS})

# Defines
target_compile_definitions(InstaMAT2Remix PRIVATE 
    INSTAMATPLUGIN_LIB 
    INSTAMAT_LIB_DYNAMIC
    _CRT_SECURE_NO_WARNINGS
)

# Include directories
target_include_directories(InstaMAT2Remix PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link Libraries
target_link_libraries(InstaMAT2Remix PRIVATE 
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

# Set properties
set_target_properties(InstaMAT2Remix PROPERTIES
    PREFIX ""
    OUTPUT_NAME "InstaMAT2RemixPlugin"
    SUFFIX ".dll"
)

# Ensure texconv.exe is next to the plugin DLL (for DDS <-> PNG conversion)
add_custom_command(TARGET InstaMAT2Remix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/texconv.exe"
        "$<TARGET_FILE_DIR:InstaMAT2Remix>/texconv.exe"
)

# Ship a default tiling mesh next to the plugin DLL (so Pull-with-tiling works out of the box)
add_custom_command(TARGET InstaMAT2Remix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:InstaMAT2Remix>/assets/meshes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/meshes/plane_tiling.usd"
        "$<TARGET_FILE_DIR:InstaMAT2Remix>/assets/meshes/plane_tiling.usd"
)
