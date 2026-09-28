add_executable(test_chat_completions test_chat_completions.cpp)
target_link_libraries(test_chat_completions PRIVATE Lens::Core Qt6::Test)
add_test(NAME chat_completions COMMAND test_chat_completions)

add_executable(test_session_store test_session_store.cpp)
target_link_libraries(test_session_store PRIVATE Lens::Core Qt6::Test)
add_test(NAME session_store COMMAND test_session_store)

add_executable(test_tools test_tools.cpp)
target_link_libraries(test_tools PRIVATE Lens::Core Qt6::Test)
add_test(NAME tools COMMAND test_tools)

add_executable(test_agent_session test_agent_session.cpp)
target_link_libraries(test_agent_session PRIVATE Lens::Core Qt6::Test)
add_test(NAME agent_session COMMAND test_agent_session)

# AgentSession 事件契约
add_executable(test_agent_session_events test_agent_session_events.cpp)
target_link_libraries(test_agent_session_events PRIVATE Lens::Core Qt6::Test)
add_test(NAME agent_session_events COMMAND test_agent_session_events)

add_executable(test_real_server test_real_server.cpp)
target_link_libraries(test_real_server PRIVATE Lens::Core Qt6::Test)
add_test(NAME real_server COMMAND test_real_server)

add_executable(test_message_list test_message_list.cpp ../src/app/MessageListModel.cpp)
target_include_directories(test_message_list PRIVATE ../src/app)
target_link_libraries(test_message_list PRIVATE Lens::Core Qt6::Test)
add_test(NAME message_list COMMAND test_message_list)

# ConversationListModel（app 层）：工作文件夹分组的扁平行结构
add_executable(test_conversation_list test_conversation_list.cpp
    ../src/app/ConversationListModel.cpp)
target_include_directories(test_conversation_list PRIVATE ../src/app)
target_link_libraries(test_conversation_list PRIVATE Lens::Core Qt6::Test)
add_test(NAME conversation_list COMMAND test_conversation_list)

add_executable(test_context test_context.cpp)
target_link_libraries(test_context PRIVATE Lens::Core Qt6::Test)
add_test(NAME context COMMAND test_context)

add_executable(test_protocols test_protocols.cpp)
target_link_libraries(test_protocols PRIVATE Lens::Core Qt6::Test)
add_test(NAME protocols COMMAND test_protocols)

add_executable(test_mcp test_mcp.cpp)
target_link_libraries(test_mcp PRIVATE Lens::Core Qt6::Test)
add_test(NAME mcp COMMAND test_mcp)

# McpManager（app 层）：异步连接管线，需 AppSettings 一起编译
add_executable(test_mcp_manager test_mcp_manager.cpp
    ../src/app/McpManager.cpp ../src/app/AppSettings.cpp)
target_include_directories(test_mcp_manager PRIVATE ../src/app)
target_link_libraries(test_mcp_manager PRIVATE Lens::Core Qt6::Gui Qt6::Test)
add_test(NAME mcp_manager COMMAND test_mcp_manager)

add_executable(test_model_list test_model_list.cpp)
target_link_libraries(test_model_list PRIVATE Lens::Core Qt6::Test)
add_test(NAME model_list COMMAND test_model_list)

add_executable(test_app_settings test_app_settings.cpp ../src/app/AppSettings.cpp)
target_include_directories(test_app_settings PRIVATE ../src/app)
# 调色板 token 一致性用例要读源码里的 Theme.qml / SettingsAppearancePage.qml
target_compile_definitions(test_app_settings PRIVATE LENS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_app_settings PRIVATE Lens::Core Qt6::Gui Qt6::Test)
add_test(NAME app_settings COMMAND test_app_settings)
