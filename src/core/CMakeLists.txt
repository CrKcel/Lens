add_library(lens_core STATIC
    include/lens/core/Conversation.hpp
    include/lens/core/agent/AgentSession.hpp
    include/lens/core/context/AgentDocs.hpp
    include/lens/core/context/EnvironmentPrompt.hpp
    include/lens/core/context/PromptAssembler.hpp
    include/lens/core/context/Skills.hpp
    include/lens/core/mcp/McpClient.hpp
    include/lens/core/mcp/McpTool.hpp
    include/lens/core/providers/ITransport.hpp
    include/lens/core/providers/QNetworkTransport.hpp
    include/lens/core/providers/SseParser.hpp
    include/lens/core/providers/ChatCompletionsClient.hpp
    include/lens/core/providers/ProtocolAdapter.hpp
    include/lens/core/providers/AnthropicClient.hpp
    include/lens/core/providers/ResponsesClient.hpp
    include/lens/core/providers/ModelListClient.hpp
    include/lens/core/storage/SessionStore.hpp
    include/lens/core/tools/Shell.hpp
    include/lens/core/tools/ToolArgs.hpp
    include/lens/core/tools/BuiltinTool.hpp
    include/lens/core/tools/ToolRegistry.hpp
    include/lens/core/tools/builtins/BashTool.hpp
    include/lens/core/tools/builtins/EditTool.hpp
    include/lens/core/tools/builtins/ReadTool.hpp
    include/lens/core/tools/builtins/WriteTool.hpp
    include/lens/core/tools/builtins/WebSearchTool.hpp
    src/AgentSession.cpp
    src/QNetworkTransport.cpp
    src/ChatCompletionsClient.cpp
    src/ProtocolAdapter.cpp
    src/AnthropicClient.cpp
    src/ResponsesClient.cpp
    src/ModelListClient.cpp
    src/McpClient.cpp
    src/WebSearchTool.cpp
    src/SessionStore.cpp
)
add_library(Lens::Core ALIAS lens_core)

target_include_directories(lens_core PUBLIC include)

target_link_libraries(lens_core
    PUBLIC Qt6::Core Qt6::Network Qt6::Sql nlohmann_json::nlohmann_json)
