# Production target and QML tooling share one directory scope.
# Scanning src/ excludes QA; production QML enters through generated QRC.
qt_add_executable(gerritpilot MANUAL_FINALIZATION
    main.cpp
    taskreporter.h
    projectmanager.cpp
    projectmanager.h
    repoproject.cpp
    repoproject.h
    gerritservice.cpp
    gerritservice.h
    commitcommandbuilder.cpp
    commitcommandbuilder.h
    commitmessageaiservice.cpp
    commitmessageaiservice.h
    processrunner.cpp
    processrunner.h
    repositorymodel.cpp
    repositorymodel.h
    repositoryproxymodel.cpp
    repositoryproxymodel.h
    workspacecontroller.cpp
    workspacecontroller.h
    gitservice.cpp
    gitservice.h
    workspacesettings.h
    repositorywatcher.cpp
    repositorywatcher.h
    gitstatusparser.h
    githistoryparser.h
    gitqueries.h
    repositorycache.h
    discardplan.h
)


set(GERRITPILOT_QML_FILES
    ../qml/App.qml
    ../qml/Theme.qml
    ../qml/pages/WorkspacePage.qml
    ../qml/features/navigation/Sidebar.qml
    ../qml/features/navigation/ProjectManagerDialog.qml
    ../qml/features/overlays/ActionDialogs.qml
    ../qml/features/overlays/BranchPanel.qml
    ../qml/features/overlays/OperationErrorDialog.qml
    ../qml/features/overlays/SettingsPanel.qml
    ../qml/features/repositories/RepositoryDetailPane.qml
    ../qml/features/repositories/ChangesFileList.qml
    ../qml/features/repositories/CommitEditor.qml
    ../qml/features/repositories/StashDialog.qml
    ../qml/features/repositories/HistoryPane.qml
    ../qml/components/AppIcon.qml
    ../qml/components/AppDialog.qml
    ../qml/components/DiffView.qml
    ../qml/components/DiffParser.js
    ../qml/components/FlatCheckBox.qml
    ../qml/components/FileActionButton.qml
    ../qml/components/GlassCard.qml
    ../qml/components/IconButton.qml
    ../qml/components/PrimaryButton.qml
    ../qml/components/CherryPickSkipAction.qml
    ../qml/components/RightScrollBar.qml
)
foreach(file IN LISTS GERRITPILOT_QML_FILES)
    string(SUBSTRING "${file}" 3 -1 alias)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS "${alias}")
endforeach()
set_source_files_properties(../qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(gerritpilot
    URI GerritPilot
    VERSION 1.0
    OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/GerritPilot"
    QML_FILES ${GERRITPILOT_QML_FILES}
)

set_target_properties(gerritpilot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)

# TARGET POST_BUILD must be declared in the target's creation directory.
# Keep Qt Creator's stamp in sync with the actual generated type information.
add_custom_command(TARGET gerritpilot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${PROJECT_BINARY_DIR}/.qt/qmltypes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_BINARY_DIR}/GerritPilot/gerritpilot.qmltypes"
        "${PROJECT_BINARY_DIR}/.qt/qmltypes/gerritpilot.qmltypes"
    VERBATIM
)

target_include_directories(gerritpilot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")



target_link_libraries(gerritpilot
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Network
)

set_target_properties(gerritpilot PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
# Qt 6.8 supports filtering dynamic deployment plugins via the target API.
# Exclude debugging/profiling services only; keep platform, TLS and image plugins.
qt_import_plugins(gerritpilot EXCLUDE_BY_TYPE qmltooling)
qt_finalize_executable(gerritpilot)
install(TARGETS gerritpilot
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/GerritPilot")

qt_generate_deploy_qml_app_script(
    TARGET gerritpilot
    OUTPUT_SCRIPT deploy_script
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
