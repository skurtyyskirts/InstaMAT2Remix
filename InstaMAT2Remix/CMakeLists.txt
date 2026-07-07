cmake_minimum_required(VERSION 3.16)

project(InstaMAT2Remix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Test)

# Main library files
set(SOURCES
    PluginMain.cpp
    CExports.cpp
    RemixConnector.cpp
    RemixNodes.cpp
    GuiManager.cpp
    ExternalTools.cpp
    MeshData.cpp
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
    MeshData.h
    Utils.h
    SettingsDialog.h
    DiagnosticsDialog.h
    PluginPaths.h
    PluginInfo.h
    Logger.h
    InstaMATAPI.h
    InstaMATPluginAPI.h
    vendor/xxhash.h
)

# Define the main library
add_library(InstaMAT2Remix SHARED ${SOURCES} ${HEADERS})
# INSTAMATPLUGIN_LIB only, matching the official SDK sample: it turns
# INSTAMATPLUGIN_API into dllexport for GetInstaMATPlugin. INSTAMAT_LIB_DYNAMIC
# must NOT be defined here — it would mark every SDK interface dllimport, which
# breaks deriving from IGraphMesh (unresolvable ctor/dtor imports); the plugin
# talks to the SDK purely through host-provided vtables.
target_compile_definitions(InstaMAT2Remix PRIVATE
    INSTAMATPLUGIN_LIB
    _CRT_SECURE_NO_WARNINGS
)
target_include_directories(InstaMAT2Remix PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(InstaMAT2Remix PRIVATE 
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
)
set_target_properties(InstaMAT2Remix PROPERTIES
    PREFIX ""
    OUTPUT_NAME "InstaMAT2RemixPlugin"
    SUFFIX ".dll"
)

# Ensure texconv.exe and default meshes
add_custom_command(TARGET InstaMAT2Remix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/texconv.exe"
        "$<TARGET_FILE_DIR:InstaMAT2Remix>/texconv.exe"
)
add_custom_command(TARGET InstaMAT2Remix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:InstaMAT2Remix>/assets/meshes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/meshes/plane_tiling.usd"
        "$<TARGET_FILE_DIR:InstaMAT2Remix>/assets/meshes/plane_tiling.usd"
)

# Out-of-process export worker (deliberately Qt-free: a single exe with no
# runtime deps beyond the CRT). It hosts the InstaMAT SDK itself via
# GetInstaMAT from Studio's InstaMAT.dll — see the ExportWorker.cpp header
# comment for why IElementExecution::Execute cannot run inside Studio's
# process (render-thread GL context affinity, qFatal/__fastfail on 3.1+).
add_executable(InstaMAT2RemixExport ExportWorker.cpp)
target_include_directories(InstaMAT2RemixExport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(InstaMAT2RemixExport PRIVATE _CRT_SECURE_NO_WARNINGS)
add_custom_command(TARGET InstaMAT2RemixExport POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:InstaMAT2RemixExport>"
        "$<TARGET_FILE_DIR:InstaMAT2Remix>/InstaMAT2RemixExport.exe"
)

# Enable testing
enable_testing()

# Test target.
#
# The plugin SHARED library does not export RemixConnector's C++ symbols
# (no Q_DECL_EXPORT / __declspec(dllexport) on the class), so linking the
# test exe against the DLL fails with unresolved externals for the ctor,
# dtor, and SetRemixApiBaseUrl. To avoid widening the DLL's ABI just for
# tests, we compile the minimum subset of sources the test exercises
# directly into the test exe.
add_executable(TestRemixConnector
    tests/TestRemixConnector.cpp
    RemixConnector.cpp
    ExternalTools.cpp
    MeshData.cpp
    Logger.cpp
    PluginPaths.cpp
)
target_include_directories(TestRemixConnector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(TestRemixConnector PRIVATE
    INSTAMATPLUGIN_LIB
    _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(TestRemixConnector PRIVATE
    Qt6::Test Qt6::Core Qt6::Network Qt6::Gui Qt6::Widgets
)
add_test(NAME TestRemixConnector COMMAND TestRemixConnector)

# The test exe needs Qt's runtime DLLs and the qwindows platform plugin
# (QTEST_MAIN expands to a QApplication). Use Qt's bundled windeployqt to
# stage them next to the exe so `ctest` works both locally and in CI without
# depending on PATH.
if(WIN32)
    get_target_property(_qt_qmake_loc Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_loc}" DIRECTORY)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET TestRemixConnector POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-quick-import
                --no-compiler-runtime
                "$<TARGET_FILE:TestRemixConnector>"
            VERBATIM)
    endif()
endif()
