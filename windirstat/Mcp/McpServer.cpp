// WinDirStat - Directory Statistics
#include "pch.h"
#include "Mcp/McpServer.h"

namespace
{
    constexpr size_t MaxRequestBytes = 1024 * 1024;
    constexpr size_t MaxTreeItems = 5000;

    std::string Utf8(const std::wstring_view value)
    {
        if (value.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring Wide(const std::string_view value)
    {
        if (value.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (size == 0) return {};
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }

    std::string EscapeJson(const std::string_view value)
    {
        std::string result;
        result.reserve(value.size() + 16);
        for (const unsigned char ch : value)
        {
            switch (ch)
            {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (ch < 0x20) result += std::format("\\u{:04x}", ch);
                else result += static_cast<char>(ch);
            }
        }
        return result;
    }

    std::optional<std::string> JsonString(const std::string& json, const std::string_view name)
    {
        const std::regex pattern(std::format(R"json("{}"\s*:\s*"((?:\\.|[^"\\])*)")json", name));
        std::smatch match;
        if (!std::regex_search(json, match, pattern)) return std::nullopt;
        std::string value = match[1].str();
        std::string result;
        for (size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] != '\\' || ++i == value.size()) { result += value[i]; continue; }
            switch (value[i])
            {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case '/': result += '/'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            default: return std::nullopt; // Reject unsupported escape sequences rather than reinterpret them.
            }
        }
        return result;
    }

    uint64_t JsonNumber(const std::string& json, const std::string_view name, const uint64_t fallback)
    {
        const std::regex pattern(std::format(R"json("{}"\s*:\s*([0-9]+))json", name));
        std::smatch match;
        if (!std::regex_search(json, match, pattern)) return fallback;
        try { return std::stoull(match[1].str()); }
        catch (const std::exception&) { return fallback; }
    }

    std::string JsonId(const std::string& json)
    {
        const std::regex pattern(R"json("id"\s*:\s*("(?:\\.|[^"\\])*"|[0-9]+|null))json");
        std::smatch match;
        return std::regex_search(json, match, pattern) ? match[1].str() : {};
    }

    std::string ItemJson(const CItem* item, const uint64_t depth, const uint64_t maxDepth, size_t& itemCount)
    {
        if (item == nullptr || itemCount++ >= MaxTreeItems) return "null";
        std::string result = std::format(R"({{"path":"{}","name":"{}","size":{},"type":"{}")",
            EscapeJson(Utf8(item->GetPath())), EscapeJson(Utf8(item->GetName())),
            item->GetSizePhysical(), item->IsTypeOrFlag(IT_FILE) ? "file" : "directory");
        if (depth < maxDepth && item->HasChildren())
        {
            result += R"(,"children":[)";
            bool first = true;
            for (const auto* child : item->GetChildren())
            {
                if (itemCount >= MaxTreeItems) break;
                if (!first) result += ',';
                result += ItemJson(child, depth + 1, maxDepth, itemCount);
                first = false;
            }
            result += ']';
        }
        return result + '}';
    }

    void CollectLargeItems(const CItem* item, const uint64_t minimumSize, std::vector<const CItem*>& result)
    {
        if (item == nullptr) return;
        if (item->IsTypeOrFlag(IT_FILE) && item->GetSizePhysical() >= minimumSize) result.push_back(item);
        for (const auto* child : item->GetChildren()) CollectLargeItems(child, minimumSize, result);
    }
}

CMcpServer::CMcpServer(CWinDirStatModel& model) : m_model(model) {}
CMcpServer::~CMcpServer() { Stop(); }

void CMcpServer::Start()
{
    if (!m_thread.joinable()) m_thread = std::jthread([this](const std::stop_token stopToken) { Run(stopToken); });
}

void CMcpServer::Stop()
{
    if (!m_thread.joinable()) return;
    m_thread.request_stop();
    // The input handle is a synchronous console/pipe read. Cancelling it makes shutdown
    // independent of whether the MCP client closes stdin.
    (void)CancelSynchronousIo(m_thread.native_handle());
    m_thread.join();
}

void CMcpServer::Run(const std::stop_token stopToken)
{
    std::string line;
    while (!stopToken.stop_requested() && std::getline(std::cin, line))
    {
        if (line.size() > MaxRequestBytes)
        {
            std::cout << R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Request exceeds 1 MiB"}})" << std::endl;
            continue;
        }
        if (const std::string response = HandleRequest(line); !response.empty()) std::cout << response << std::endl;
    }
}

std::string CMcpServer::HandleRequest(const std::string& request)
{
    const std::string id = JsonId(request);
    const auto method = JsonString(request, "method");
    const auto error = [&id](const int code, const std::string_view message)
    {
        if (id.empty()) return std::string{};
        return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":{},"message":"{}"}}}})", id, code, EscapeJson(message));
    };
    const auto success = [&id](const std::string& result)
    {
        if (id.empty()) return std::string{};
        return std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, result);
    };
    if (JsonString(request, "jsonrpc") != std::optional<std::string>{"2.0"}) return error(-32600, "JSON-RPC 2.0 is required");
    if (!method) return error(-32600, "Invalid JSON-RPC request");

    if (*method == "initialize")
    {
        return success(R"({"protocolVersion":"2024-11-05","serverInfo":{"name":"windirstat","version":"2.x"},"capabilities":{"tools":{}}})");
    }
    if (*method == "notifications/initialized") return {};
    if (*method == "tools/list")
    {
        return success(R"({"tools":[{"name":"get_scan_status","description":"Get the state of the current scan.","inputSchema":{"type":"object","properties":{}}},{"name":"get_tree","description":"Read a bounded portion of the scanned directory tree.","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"depth":{"type":"integer","minimum":0,"maximum":32}}}},{"name":"find_large_items","description":"Find large files in the current scan.","inputSchema":{"type":"object","properties":{"minimum_size":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":1000}}}},{"name":"get_selection","description":"Read the current UI selection.","inputSchema":{"type":"object","properties":{}}},{"name":"scan_path","description":"Start a scan and return immediately; use get_scan_status to poll.","inputSchema":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"]}}]})");
    }
    if (*method != "tools/call") return error(-32601, "Method not found");

    const auto tool = JsonString(request, "name");
    if (!tool) return error(-32602, "tools/call requires a tool name");
    std::string payload;
    bool valid = true;
    CMainFrame::Get()->InvokeInMessageThread([&]
    {
        if (*tool == "get_scan_status")
        {
            payload = std::format(R"({{"running":{},"settled":{},"root_done":{},"has_root":{}}})",
                m_model.IsScanRunning() ? "true" : "false", m_model.IsScanSettled() ? "true" : "false",
                m_model.IsRootDone() ? "true" : "false", m_model.HasRootItem() ? "true" : "false");
        }
        else if (*tool == "scan_path")
        {
            const auto path = JsonString(request, "path");
            valid = path && !path->empty() && m_model.StartScan(Wide(*path));
            payload = R"({"started":true})";
        }
        else if (*tool == "get_tree")
        {
            CItem* root = m_model.GetRootItem();
            if (const auto path = JsonString(request, "path"); path && root) root = root->FindItemByPath(Wide(*path));
            size_t count = 0;
            payload = std::format(R"({{"tree":{},"truncated":{}}})", ItemJson(root, 0, std::min(JsonNumber(request, "depth", 2), uint64_t{32}), count),
                count >= MaxTreeItems ? "true" : "false");
        }
        else if (*tool == "find_large_items")
        {
            std::vector<const CItem*> items;
            CollectLargeItems(m_model.GetRootItem(), JsonNumber(request, "minimum_size", 1024ULL * 1024 * 1024), items);
            std::ranges::sort(items, std::ranges::greater{}, &CItem::GetSizePhysical);
            const size_t limit = static_cast<size_t>(std::clamp(JsonNumber(request, "limit", 100), uint64_t{1}, uint64_t{1000}));
            payload = R"({"items":[)";
            for (size_t i = 0; i < std::min(limit, items.size()); ++i)
            {
                if (i != 0) payload += ',';
                payload += std::format(R"({{"path":"{}","size":{}}})", EscapeJson(Utf8(items[i]->GetPath())), items[i]->GetSizePhysical());
            }
            payload += "]}";
        }
        else if (*tool == "get_selection")
        {
            payload = R"({"items":[)";
            const auto items = m_model.GetAllSelected();
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i != 0) payload += ',';
                payload += std::format(R"({{"path":"{}","size":{}}})", EscapeJson(Utf8(items[i]->GetPath())), items[i]->GetSizePhysical());
            }
            payload += "]}";
        }
        else valid = false;
    });
    if (!valid) return error(-32602, *tool == "scan_path" ? "A valid path is required and the scan could not start" : "Unknown tool");
    return success(std::format(R"({{"content":[{{"type":"text","text":"{}"}}],"structuredContent":{}}})", EscapeJson(payload), payload));
}
