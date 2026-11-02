# Layer 3: Qt 6 GUI (optional, depends on Qt6::Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Everything but main.cpp, so the GUI tests (tests/gui, FEMM_GUI_TESTS) can
# link the same objects the application does.
set(FEMM_GUI_SOURCES
    mainwindow.cpp
    editor_widget.cpp
    editor_sketch.cpp
    editor_tools.cpp
    region_map.cpp
    material_picker.cpp
    problem_check.cpp
    model_panel.cpp
    results_summary.cpp
    welcome_page.cpp
    command_palette.cpp
    sweep_dialog.cpp
    pattern_dialog.cpp
    report_export.cpp
    getting_started.cpp
    mouse_release_gate.cpp
    results_widget.cpp
    scalar_results_widget.cpp
    scalar_doc_view.cpp
    magnetics_handler.cpp
    electrostatics_handler.cpp
    heat_handler.cpp
    heat_prop_editors.cpp
    heat_matlib_dialog.cpp
    curr_prop_editors.cpp
    curr_matlib_dialog.cpp
    curr_handler.cpp
    py_line_edit.cpp
    prop_dialogs.cpp
    point_prop_editor_base.cpp
    prop_editors.cpp
    elec_prop_editors.cpp
    prop_list_dialog.cpp
    problem_dialog.cpp
    curve_dialog.cpp
    bh_dialog.cpp
    tk_dialog.cpp
    prefs_dialog.cpp
    scalar_prefs_dialog.cpp
    pre_prefs_dialog.cpp
    script_prefs_dialog.cpp
    solver_progress_dialog.cpp
    view_colors.cpp
    py_console.cpp
    matlib_dialog_base.cpp
    matlib_dialog.cpp
    matlib_tree.cpp
    elec_matlib_dialog.cpp
    plot_widget.cpp
    output_dialog.cpp
    copy_move_dialog.cpp
    arc_dialog.cpp
)

add_executable(femm_gui
    main.cpp
    ${FEMM_GUI_SOURCES}
    icons.qrc
    $<$<PLATFORM_ID:Windows>:femm.rc>
)
femm_add_version_resource(femm_gui "FEMM")

# Pre-production builds of this UI fork say so, prominently: results are not
# yet validated against the upstream release.
option(FEMM_PRERELEASE "Mark the build as a pre-production release (warning banner)" ON)
if(FEMM_PRERELEASE)
    target_compile_definitions(femm_gui PRIVATE FEMM_PRERELEASE=1)
endif()

# --- Translations (gui/translations/femm_<lang>.ts) -------------------------
# Compiled to .qm and embedded under :/i18n. main.cpp picks the language from
# the ui/language setting, else the system locale; English needs no file.
find_package(Qt6 COMPONENTS LinguistTools QUIET)
if(Qt6LinguistTools_FOUND)
    qt_add_translations(femm_gui
        TS_FILES translations/femm_de.ts
        RESOURCE_PREFIX "/i18n"
        LUPDATE_OPTIONS -locations none)
    # Qt's own strings (standard dialog buttons): bundled from the Qt in use,
    # since macdeployqt and the installers do not ship Qt's translations.
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    execute_process(COMMAND "${_qmake}" -query QT_INSTALL_TRANSLATIONS
                    OUTPUT_VARIABLE _qt_tr OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(EXISTS "${_qt_tr}/qtbase_de.qm")
        set_source_files_properties("${_qt_tr}/qtbase_de.qm" PROPERTIES QT_RESOURCE_ALIAS qtbase_de.qm)
        qt_add_resources(femm_gui "qt_translations" PREFIX "/i18n" FILES "${_qt_tr}/qtbase_de.qm")
    endif()
else()
    message(STATUS "Qt6 LinguistTools not found -- building without translations")
endif()
# femm.rc says ICON "icons/femm.ico", but nothing scans a .rc for what it
# includes -- ninja reported "#deps 0" for femm.rc.obj -- so replacing the icon
# rebuilt nothing and the executable kept the old one.
#
# It hid well: icons.qrc embeds the SAME file for the window icon and that path
# does rebuild, so the icon inside the running program updated while the one
# Explorer and the taskbar show stayed four days stale.
if(WIN32)
    set_property(SOURCE femm.rc APPEND PROPERTY OBJECT_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/icons/femm.ico")
endif()

target_include_directories(femm_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(femm_gui PRIVATE femm_ipc Qt6::Widgets)
target_compile_options(femm_gui PRIVATE -Wall
    -Wno-unused-variable -Wno-sign-compare -Wno-unused-result
)

# The minimum macOS the bundle declares (LSMinimumSystemVersion in
# Info.plist.in). macos_build.sh passes FEMM_MACOS_MIN alongside
# CMAKE_OSX_DEPLOYMENT_TARGET -- the Qt's own floor unless overridden -- and
# step 4c there refuses a bundle whose binaries declare anything higher. A
# bare cmake run without either falls back to the deployment target, and
# failing that to the macOS this is being built on, which is what clang
# targets when told nothing.
if(APPLE)
    if(NOT FEMM_MACOS_MIN)
        set(FEMM_MACOS_MIN "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    if(NOT FEMM_MACOS_MIN)
        execute_process(COMMAND sw_vers -productVersion
                        OUTPUT_VARIABLE _host_macos OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX REPLACE "^([0-9]+)\\.([0-9]+).*" "\\1.\\2" FEMM_MACOS_MIN "${_host_macos}")
    endif()
    set(MACOSX_BUNDLE_MIN_MACOS "${FEMM_MACOS_MIN}")
endif()

# Suppress console window on Windows/macOS
set_target_properties(femm_gui PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")

# The application icon, macOS only. Two halves are both required and neither
# reports the other missing: Info.plist.in names femm.icns in CFBundleIconFile,
# and the file itself has to be IN Contents/Resources. Without both, Finder
# draws the blank placeholder icon and says nothing about why -- which is what
# it did until 2026-08-23.
#
# MACOSX_PACKAGE_LOCATION is what puts it there, and it is why the file is
# listed as a source: CMake treats a source with that property as a bundle
# resource to copy rather than something to compile. Guarded by APPLE so the
# other platforms never see a .icns in their source list. Windows takes its
# icon from the .rc instead, and Linux from the .desktop entry's Icon= key
# (see packaging/unix_install.sh).
if(APPLE)
    set(FEMM_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/icons/femm.icns")
    target_sources(femm_gui PRIVATE "${FEMM_ICNS}")
    set_source_files_properties("${FEMM_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
endif()

# --- Offscreen GUI tests (tests/gui) ----------------------------------------
# Drive EditorWidget with synthetic mouse/keyboard events under Qt's offscreen
# platform. Off by default; enable with -DFEMM_GUI_TESTS=ON, run with
#   QT_QPA_PLATFORM=offscreen build/bin/femm_gui_tests
option(FEMM_GUI_TESTS "Build the offscreen GUI tests (needs Qt6 Test)" OFF)
if(FEMM_GUI_TESTS)
    find_package(Qt6 COMPONENTS Test REQUIRED)
    add_executable(femm_gui_tests
        "${CMAKE_SOURCE_DIR}/tests/gui/gui_tests.cpp"
        ${FEMM_GUI_SOURCES}
        icons.qrc
    )
    set_target_properties(femm_gui_tests PROPERTIES MACOSX_BUNDLE OFF)
    target_include_directories(femm_gui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(femm_gui_tests PRIVATE femm_ipc Qt6::Widgets Qt6::Test)
    if(Qt6LinguistTools_FOUND)
        qt_add_translations(femm_gui_tests TS_FILES translations/femm_de.ts
                            RESOURCE_PREFIX "/i18n" LUPDATE_TARGET femm_gui_tests_lupdate
                            LRELEASE_TARGET femm_gui_tests_lrelease)
    endif()
endif()
