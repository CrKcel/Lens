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
)

qt_add_qml_module(lens
    URI Lens.app
    VERSION 1.0
    QML_FILES
        Main.qml
        SettingFieldBg.qml
)

target_compile_definitions(lens PRIVATE LENS_VERSION="${PROJECT_VERSION}")
target_link_libraries(lens PRIVATE Lens::Core Qt6::Quick)
