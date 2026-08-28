cmake_minimum_required(VERSION 3.23)
project(FluentUI VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "FluentUI supports Windows only")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(FLUENTUI_BUILD_SETTINGS "Build FluentSettings" ON)
option(FLUENTUI_BUILD_EXAMPLES "Build FluentUI examples" OFF)
option(FLUENTUI_STATIC_RUNTIME "Use the static MSVC runtime" ON)

set(FLUENTUI_SOURCES
    FluentUI/Application.cpp
    FluentUI/animation/Animation.cpp
    FluentUI/animation/AnimationRegistry.cpp
    FluentUI/composition/DCompCompositionBackend.cpp
    FluentUI/composition/ScrollContentHost.cpp
    FluentUI/controls/Button.cpp
    FluentUI/controls/Calendar.cpp
    FluentUI/controls/Chart.cpp
    FluentUI/controls/CheckBox.cpp
    FluentUI/controls/ComboBox.cpp
    FluentUI/controls/ContentDialog.cpp
    FluentUI/controls/DataGrid.cpp
    FluentUI/controls/DatePicker.cpp
    FluentUI/controls/Expander.cpp
    FluentUI/controls/GridSplitter.cpp
    FluentUI/controls/Hyperlink.cpp
    FluentUI/controls/Image.cpp
    FluentUI/controls/InfoBar.cpp
    FluentUI/controls/ListBox.cpp
    FluentUI/controls/MenuBar.cpp
    FluentUI/controls/MenuFlyout.cpp
    FluentUI/controls/MessageDialog.cpp
    FluentUI/controls/Metric.cpp
    FluentUI/controls/NumericUpDown.cpp
    FluentUI/controls/PasswordBox.cpp
    FluentUI/controls/ProgressBar.cpp
    FluentUI/controls/ProgressSweepVisual.cpp
    FluentUI/controls/RadioButton.cpp
    FluentUI/controls/Rating.cpp
    FluentUI/controls/RepeatButton.cpp
    FluentUI/controls/Separator.cpp
    FluentUI/controls/Slider.cpp
    FluentUI/controls/StatusBar.cpp
    FluentUI/controls/TabControl.cpp
    FluentUI/controls/TextArea.cpp
    FluentUI/controls/TextBlock.cpp
    FluentUI/controls/TextBox.cpp
    FluentUI/controls/TextEditBase.cpp
    FluentUI/controls/ToggleSwitch.cpp
    FluentUI/controls/ToggleVisuals.cpp
    FluentUI/controls/ToolBar.cpp
    FluentUI/controls/TreeView.cpp
    FluentUI/core/Control.cpp
    FluentUI/core/UIElement.cpp
    FluentUI/core/Visual.cpp
    FluentUI/diagnostics/DisplaySyncScheduler.cpp
    FluentUI/diagnostics/LayoutCostProbe.cpp
    FluentUI/diagnostics/PerformanceCounters.cpp
    FluentUI/diagnostics/SessionLog.cpp
    FluentUI/dialogs/FileDialog.cpp
    FluentUI/graphics/D2DContext.cpp
    FluentUI/graphics/DCompHost.cpp
    FluentUI/graphics/DWriteContext.cpp
    FluentUI/graphics/DeviceLostController.cpp
    FluentUI/graphics/ResourceCache.cpp
    FluentUI/input/DragDrop.cpp
    FluentUI/input/FocusManager.cpp
    FluentUI/input/InputManager.cpp
    FluentUI/layout/Border.cpp
    FluentUI/layout/Canvas.cpp
    FluentUI/layout/DockPanel.cpp
    FluentUI/layout/Grid.cpp
    FluentUI/layout/GroupBox.cpp
    FluentUI/layout/Panel.cpp
    FluentUI/layout/ScrollPanel.cpp
    FluentUI/layout/ScrollViewer.cpp
    FluentUI/layout/StackPanel.cpp
    FluentUI/layout/UniformGrid.cpp
    FluentUI/layout/Viewbox.cpp
    FluentUI/layout/WrapPanel.cpp
    FluentUI/services/PopupHost.cpp
    FluentUI/services/TooltipService.cpp
    FluentUI/styling/ThemeManager.cpp
    FluentUI/text/LineIndex.cpp
    FluentUI/text/LineLayoutCache.cpp
    FluentUI/text/LogSink.cpp
    FluentUI/text/UndoStack.cpp
    FluentUI/text/WrapExtentMap.cpp
    FluentUI/window/NativeWindowHost.cpp
    FluentUI/window/GraphicsHost.cpp
    FluentUI/window/InputHost.cpp
    FluentUI/window/FrameHost.cpp
    FluentUI/window/ThemeHost.cpp
    FluentUI/window/DiagnosticsHost.cpp
    FluentUI/window/DialogHost.cpp
    FluentUI/window/DialogWindow.cpp
    FluentUI/window/FrameScheduler.cpp
    FluentUI/window/Window.cpp
    FluentUI/window/WindowAppearance.cpp
)

add_library(FluentUI STATIC ${FLUENTUI_SOURCES})
add_library(FluentUI::FluentUI ALIAS FluentUI)
target_compile_features(FluentUI PUBLIC cxx_std_20)
target_include_directories(FluentUI PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(FluentUI PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(FluentUI PUBLIC
    d3d11 d2d1 dxgi dcomp dwrite dwmapi user32 shcore imm32 psapi)

if(MSVC)
    target_compile_options(FluentUI PRIVATE /W3 /permissive-)
    # /utf-8 must be PUBLIC, not PRIVATE. This repo's convention is "comments
    # explain why, at length", and ~80 installed headers carry Chinese prose. On a
    # non-UTF-8 default code page (936 here) MSVC reads those bytes as the ANSI page
    # and warns C4819 once per header. Measured on this machine: flipping this line
    # back to PRIVATE takes the FluentUIMinimal build from 0 to 74 C4819 warnings.
    # The flag has to reach whoever INCLUDES the headers, so it belongs on the
    # interface, not on our own object files.
    #
    # Note on how to verify: an installed-package consumer does NOT show this. CMake
    # puts imported-target include dirs behind /external:I with
    # ExternalWarningLevel=TurnOffAllWarnings, which suppresses C4819 whether or not
    # this flag is present. Check against an in-tree consumer (FluentUIMinimal) —
    # that is the configuration where the warning is observable.
    target_compile_options(FluentUI PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    if(FLUENTUI_STATIC_RUNTIME)
        set_property(TARGET FluentUI PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

if(FLUENTUI_BUILD_SETTINGS)
    add_library(FluentSettings STATIC
        FluentSettings/ConfigStore.cpp
        FluentSettings/ConfigStore.h)
    add_library(FluentUI::Settings ALIAS FluentSettings)
    target_compile_features(FluentSettings PUBLIC cxx_std_20)
    target_include_directories(FluentSettings PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE FluentSettings/third_party)
    target_compile_definitions(FluentSettings PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(FluentSettings PUBLIC shell32)
    if(MSVC)
        target_compile_options(FluentSettings PRIVATE /W3 /permissive-)
        # PUBLIC for the same reason as FluentUI above: the installed ConfigStore.h
        # carries non-ASCII prose, so consumers need the flag too.
        target_compile_options(FluentSettings PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
        if(FLUENTUI_STATIC_RUNTIME)
            set_property(TARGET FluentSettings PROPERTY MSVC_RUNTIME_LIBRARY
                "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    endif()
endif()

if(FLUENTUI_BUILD_EXAMPLES)
    add_executable(FluentUIMinimal WIN32
        examples/Minimal/main.cpp
        examples/Minimal/app.rc)
    target_link_libraries(FluentUIMinimal PRIVATE FluentUI::FluentUI)
    if(MSVC AND FLUENTUI_STATIC_RUNTIME)
        set_property(TARGET FluentUIMinimal PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

install(TARGETS FluentUI
    EXPORT FluentUITargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(FLUENTUI_BUILD_SETTINGS)
    install(TARGETS FluentSettings
        EXPORT FluentUITargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES FluentSettings/ConfigStore.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/FluentSettings)
endif()
# Install ALL headers, deliberately: the static-library build puts a great deal
# of inline and template code in headers, so a consumer's compiler needs to see
# the internal ones too (Window.h derives from NativeWindowHost.h, controls include
# graphics/ and composition/ headers from inline methods, and so on). Splitting
# the install into public/internal trees would break the build rather than
# protect anyone. The public/internal DISTINCTION is documented instead — see
# docs/api-surface.md and the FLUENTUI_INTERNAL marker in Api.h.
#
# EXCLUDE the MSBuild output directories. The Visual Studio projects write
# intermediates into FluentUI/x64/ and FluentUI/FluentUI/x64/, and an unfiltered
# recursive install copies the .tlog files sitting there into include/ — the
# installed tree ends up with include/FluentUI/x64/Release/*.tlog. Harmless but
# unmistakably sloppy in a package a stranger is evaluating.
install(DIRECTORY FluentUI/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/FluentUI
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "x64" EXCLUDE
        PATTERN "Win32" EXCLUDE
        PATTERN "Debug" EXCLUDE
        PATTERN "Release" EXCLUDE)
install(EXPORT FluentUITargets
    FILE FluentUITargets.cmake
    NAMESPACE FluentUI::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FluentUI)

configure_package_config_file(
    cmake/FluentUIConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/FluentUIConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FluentUI)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/FluentUIConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/FluentUIConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/FluentUIConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FluentUI)
