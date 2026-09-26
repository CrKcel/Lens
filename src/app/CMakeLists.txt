find_package(Qt6 REQUIRED COMPONENTS LinguistTools Widgets)

qt_add_executable(lens
    main.cpp
    AppSettings.hpp
    AppSettings.cpp
    ChatController.hpp
    ChatController.cpp
    E2eDriver.hpp
    E2eDriver.cpp
    ConversationListModel.hpp
    ConversationListModel.cpp
    MessageListModel.hpp
    MessageListModel.cpp
    TitleBarLayout.hpp
    TitleBarLayout.cpp
)

qt_add_qml_module(lens
    URI Lens.app
    VERSION 1.0
    QML_FILES
        Main.qml
        AccentButton.qml
        ChatView.qml
        ContextInspector.qml
        NavButton.qml
        SettingFieldBg.qml
        SettingsView.qml
        Sidebar.qml
        SlimScrollBar.qml
        Theme.qml
)

qt_add_translations(lens
    TS_FILES translations/lens_en.ts
    QM_FILES_OUTPUT_VARIABLE LENS_QM_FILES)
set_source_files_properties(${LENS_QM_FILES}
    PROPERTIES QT_RESOURCE_ALIAS "lens_en.qm")
qt_add_resources(lens "lens_translations"
    PREFIX "/i18n"
    FILES ${LENS_QM_FILES})

target_compile_definitions(lens PRIVATE LENS_VERSION="${PROJECT_VERSION}")
target_link_libraries(lens PRIVATE Lens::Core Qt6::Quick Qt6::QuickControls2 Qt6::Widgets)
