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

add_executable(test_real_server test_real_server.cpp)
target_link_libraries(test_real_server PRIVATE Lens::Core Qt6::Test)
add_test(NAME real_server COMMAND test_real_server)

add_executable(test_message_list test_message_list.cpp ../src/app/MessageListModel.cpp)
target_include_directories(test_message_list PRIVATE ../src/app)
target_link_libraries(test_message_list PRIVATE Lens::Core Qt6::Test)
add_test(NAME message_list COMMAND test_message_list)

add_executable(test_context test_context.cpp)
target_link_libraries(test_context PRIVATE Lens::Core Qt6::Test)
add_test(NAME context COMMAND test_context)

add_executable(test_protocols test_protocols.cpp)
target_link_libraries(test_protocols PRIVATE Lens::Core Qt6::Test)
add_test(NAME protocols COMMAND test_protocols)

add_executable(test_protocol_session test_protocol_session.cpp)
target_link_libraries(test_protocol_session PRIVATE Lens::Core Qt6::Test)
add_test(NAME protocol_session COMMAND test_protocol_session)

# MCP mock 服务器（纯 stdin/stdout 进程），路径传给 test_mcp
add_executable(mcp_mock_server mcp_mock_server.cpp)
target_link_libraries(mcp_mock_server PRIVATE nlohmann_json::nlohmann_json)

add_executable(test_mcp test_mcp.cpp)
target_compile_definitions(test_mcp PRIVATE
    MOCK_SERVER_PATH="$<TARGET_FILE:mcp_mock_server>")
target_link_libraries(test_mcp PRIVATE Lens::Core Qt6::Test)
add_test(NAME mcp COMMAND test_mcp)

add_executable(test_web_search test_web_search.cpp)
target_link_libraries(test_web_search PRIVATE Lens::Core Qt6::Test)
add_test(NAME web_search COMMAND test_web_search)

add_executable(test_model_list test_model_list.cpp)
target_link_libraries(test_model_list PRIVATE Lens::Core Qt6::Test)
add_test(NAME model_list COMMAND test_model_list)

add_executable(test_app_settings test_app_settings.cpp ../src/app/AppSettings.cpp)
target_include_directories(test_app_settings PRIVATE ../src/app)
target_link_libraries(test_app_settings PRIVATE Lens::Core Qt6::Gui Qt6::Test)
add_test(NAME app_settings COMMAND test_app_settings)
