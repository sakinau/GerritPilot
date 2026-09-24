cmake_minimum_required(VERSION 3.21)

project(GerritPilotNext VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Network)

qt_standard_project_setup(REQUIRES 6.8)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(gerritpilot
    src/main.cpp
    src/taskreporter.cpp
    src/taskreporter.h
    src/projectmanager.cpp
    src/projectmanager.h
    src/repoproject.cpp
    src/repoproject.h
    src/gerritservice.cpp
    src/gerritservice.h
    src/commitworkflowservice.cpp
    src/commitworkflowservice.h
    src/commitmessageaiservice.cpp
    src/commitmessageaiservice.h
    src/processrunner.cpp
    src/processrunner.h
    src/repositorymodel.cpp
    src/repositorymodel.h
    src/repositoryproxymodel.cpp
    src/repositoryproxymodel.h
    src/workspacecontroller.cpp
    src/workspacecontroller.h
    src/gitservice.cpp
    src/gitservice.h
    src/workspacesettings.h
    src/repositorywatcher.cpp
    src/repositorywatcher.h
    src/gitstatusparser.h
    src/githistoryparser.h
    src/gitqueries.h
    src/repositorycache.h
    src/discardplan.h
)

qt_add_qml_module(gerritpilot
    URI GerritPilot
    VERSION 1.0
    QML_FILES
        qml/App.qml
        qml/Theme.qml
        qml/pages/WorkspacePage.qml
        qml/features/navigation/Sidebar.qml
        qml/features/navigation/ProjectManagerDialog.qml
        qml/features/overlays/ActionDialogs.qml
        qml/features/overlays/BranchPanel.qml
        qml/features/overlays/OperationErrorDialog.qml
        qml/features/overlays/SettingsPanel.qml
        qml/features/overlays/RepoProjectPane.qml
        qml/features/overlays/WorkspaceFolderDialog.qml
        qml/features/repositories/RepositoryDetailPane.qml
        qml/features/repositories/ChangesFileList.qml
        qml/features/repositories/CommitEditor.qml
        qml/features/repositories/StashDialog.qml
        qml/features/repositories/HistoryPane.qml
        qml/components/AppIcon.qml
        qml/components/AppDialog.qml
        qml/components/DiffView.qml
        qml/components/DiffParser.js
        qml/components/FlatCheckBox.qml
        qml/components/FileActionButton.qml
        qml/components/GlassCard.qml
        qml/components/IconButton.qml
        qml/components/NavItem.qml
        qml/components/Pill.qml
        qml/components/PrimaryButton.qml
        qml/components/RightScrollBar.qml
)

# Qt Creator 20 can mistake CMake's empty .qt/qmltypes stamp for the module
# type information. Keep that stamp in sync with the actual generated file.
add_custom_command(TARGET gerritpilot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/.qt/qmltypes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/GerritPilot/gerritpilot.qmltypes"
        "${CMAKE_CURRENT_BINARY_DIR}/.qt/qmltypes/gerritpilot.qmltypes"
    VERBATIM
)

target_include_directories(gerritpilot PRIVATE src)



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
install(TARGETS gerritpilot
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_qml_app_script(
    TARGET gerritpilot
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
