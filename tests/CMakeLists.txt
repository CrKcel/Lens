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
