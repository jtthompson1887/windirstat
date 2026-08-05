// WinDirStat - Directory Statistics
#pragma once

class CWinDirStatModel;

// Local, read-only Model Context Protocol server using the MCP stdio transport.
class CMcpServer final
{
public:
    explicit CMcpServer(CWinDirStatModel& model);
    ~CMcpServer();

    CMcpServer(const CMcpServer&) = delete;
    CMcpServer& operator=(const CMcpServer&) = delete;

    void Start();
    void Stop();

private:
    void Run(std::stop_token stopToken);
    std::string HandleRequest(const std::string& request);

    CWinDirStatModel& m_model;
    std::jthread m_thread;
};
