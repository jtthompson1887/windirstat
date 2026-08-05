# DeepWiki Q&A with Code Context for Repository: windirstat/windirstat
## Q1
How could local MPC server be added to let an AI Agent navigate the tree and find out where to clean up space.
### Answer
Assuming you mean **MCP** (Model Context Protocol, Anthropic’s open protocol for AI-tool integration) rather than “MPC,” here is how a local MCP server could be added to WinDirStat to let an AI agent navigate the scan tree and decide what to clean up.

## 1. Transport layer

WinDirStat currently has no HTTP, socket, named-pipe, or JSON-RPC infrastructure. You would add a small transport adapter. The two most natural choices for a local MCP server are:

- **stdio** — the simplest for a local agent that launches WinDirStat as a subprocess.
- **Named pipes or local HTTP/SSE** — if the agent is a separate process that connects to an already-running WinDirStat instance.

Because WinDirStat is an MFC GUI application with a message-loop thread, the transport must be non-blocking or run on its own thread and marshal requests onto the main/UI thread.

## 2. Where to attach the server

The server should talk to the existing singleton model, not to the views directly.

- `CWinDirStatModel` is the singleton owner of the scan root and all actions such as `StartScan`, `GetRootItem`, `GetAllSelected`, `DeletePhysicalItems`, and `RefreshItem` windirstat:82-160 .
- The tree itself is a hierarchy of `CItem` objects exposed through `GetChildren()`, `GetParent()`, `IsLeaf()`, `GetPath()`, `GetSizeLogical()`, `GetSizePhysical()`, etc. windirstat:118-236 .
- Scan state is available via `IsScanRunning()`, `IsScanSettled()`, and `IsRootDone()` windirstat:111-115 .

A thin `CMcpServer` class could be added next to `CWinDirStatModel`, registered during `CDirStatApp::InitInstance()` after the model is created windirstat:354-414 .

## 3. MCP tools to expose

The agent needs read-only navigation tools plus a small set of safe mutation tools.

**Navigation / analysis**

| Tool | What it would call |
|---|---|
| `scan_path` | `CWinDirStatModel::StartScan(pathSpec)` windirstat:92-154  |
| `get_tree` | `CItem::GetChildren()` recursion, returning path, size, type, attributes, last change windirstat:143-203  |
| `find_large_items` | `CItem::GetItemsRecursive()` with a size predicate windirstat:221-222  |
| `find_duplicates` | `CFileDupeControl::Get()->GetRootItem()` (existing duplicate detection runs during scan) windirstat:1077-1078  |
| `get_extensions` | `CWinDirStatModel::GetExtensionData()` windirstat:104-106  |
| `get_selection` | `CWinDirStatModel::GetAllSelected()` windirstat:156-156  |

**Cleanup actions**

| Tool | What it would call |
|---|---|
| `delete_to_recycle_bin` | `CWinDirStatModel::DeletePhysicalItems(items, true)` windirstat:617-641  |
| `delete_permanently` | `CWinDirStatModel::DeletePhysicalItems(items, false)` windirstat:617-641  |
| `empty_folder` | `CWinDirStatModel::DeletePhysicalItems(items, false, true)` windirstat:592-605  |
| `move_to` | `CWinDirStatModel::OnCleanupMoveTo()` pattern using `IFileOperation` windirstat:607-666  |
| `run_user_cleanup` | `CWinDirStatModel::OnUserDefinedCleanup(id)` windirstat:782-817  |
| `refresh` | `CWinDirStatModel::RefreshItem(items)` windirstat:129-130  |

## 4. Serialization shortcut

WinDirStat already exports and imports the entire tree as CSV or JSON via `SaveResults()` and `LoadResults()` in `CsvLoader.cpp` windirstat:396-568 . For an early prototype, the MCP server could simply:

1. Accept a `scan_path` request.
2. Run `StartScan()` and wait for `IsScanSettled()`.
3. Call `SaveResults()` to a temp file.
4. Stream the JSON back to the agent.

This avoids writing a custom tree serializer. The agent can then ask follow-up questions and send back item paths for cleanup.

## 5. Command-line integration

The existing CLI parser in `WinDirStat.cpp` already supports `/saveto`, `/savedupesto`, `/savepermsto`, and `/loadfrom` and runs the app hidden when those flags are present windirstat:231-412 . A new flag such as `/mcp` or `/mcp-port` could be added to `CWinDirStatCommandLineInfo` so the server starts in headless mode.

## 6. Threading and safety considerations

- `CItem` and the model are not thread-safe for mutation. Scanning already runs on worker threads (`CItem::ScanItems`) and finalizes on the model thread windirstat:1147-1199 , but UI-driven actions assume the main thread.
- MCP requests should be queued to the main thread with `CMainFrame::InvokeInMessageThread()` or a similar mechanism windirstat:1162-1165 .
- Cleanup tools must respect the same confirmation and capability checks the UI uses, e.g. `UserDefinedCleanupWorksForItem()` windirstat:763-780 .

## 7. Security

Because the server can delete files and read the entire filesystem, it should be local-only and authenticated. Options:

- Bind to `127.0.0.1` only, or use stdio/named pipes with ACLs.
- Require the user to opt in via a setting, similar to how elevation is gated windirstat:451-463 .
- For destructive tools, mirror the existing confirmation prompts or require an explicit `confirm: true` parameter.

## Summary

The cleanest path is to add a small `CMcpServer` module that reuses `CWinDirStatModel` and `CsvLoader` rather than reimplementing the tree. It would expose tools for scanning, navigating the `CItem` tree, querying duplicates/extensions, and invoking the existing cleanup actions, with all mutations marshaled to the main thread and guarded by the same validation the UI already uses.
### Citations
**File:** windirstat/WinDirStatModel.h (L82-160)
```text
class CWinDirStatModel final : public CCmdTarget
{
    friend class CWinDirStatPane;

public:
    static CWinDirStatModel* Get() { return s_singleton; }
    CWinDirStatModel();
    ~CWinDirStatModel() override;

    void ClearScanState();
    BOOL ResetScan();
    BOOL StartScan(const std::wstring& pathSpec);
    BOOL OpenLoadedScan(CItem* loadedRoot);
    void SetScanPathSpec(const std::wstring& pathSpec);
    const std::wstring& GetScanPathSpec() const { return m_scanPathSpec; }
    const std::wstring& GetScanTitle() const { return m_scanTitle; }
    void SetScanTitle(const std::wstring& title) { m_scanTitle = title; }
    void SetScanTitlePrefix(const std::wstring& prefix) const;

    COLORREF GetCushionColor(const std::wstring& ext);
    COLORREF GetZoomColor() const;

    CExtensionData* GetExtensionData();
    SExtensionRecord* GetExtensionDataRecord(const std::wstring& ext);
    bool IsExtensionRegistered(const std::wstring& ext) const;
    ULONGLONG GetRootSize() const;

    void RefreshReparsePointItems();

    bool HasRootItem() const;
    bool IsRootDone() const;
    bool IsScanRunning() const;
    bool IsScanSettled() const;
    CItem* GetRootItem() const;
    CItem* GetZoomItem() const;
    bool IsZoomed() const;

    void SetHighlightExtension(const std::wstring& ext, bool unregistered = false);
    std::wstring GetHighlightExtension() const;
    bool IsHighlightUnregistered() const;
    const std::unordered_set<std::wstring>& GetHighlightExtensions() const;

    void UnlinkRoot();
    bool UserDefinedCleanupWorksForItem(USERDEFINEDCLEANUP* udc, const CItem* item) const;
    void StartScanningEngine(std::vector<CItem*> items);
    enum StopReason : uint8_t { Default, Stop, Abort };
    void StopScanningEngine(StopReason stopReason = Stop);
    void RefreshItem(const std::vector<CItem*>& item) const;
    void RefreshItem(CItem* item) const { RefreshItem(std::vector{ item }); }

    static void OpenItem(const CItem* item, const std::wstring& verb = {});

    void RecurseRefreshReparsePoints(CItem* items) const;
    void RebuildExtensionData();
    void RebuildRegisteredExtensions();
    void DeletePhysicalItems(const std::vector<CItem*>& items, bool toTrashBin, bool emptyOnly = false) const;
    void SetZoomItem(CItem* item);
    static void AskForConfirmation(USERDEFINEDCLEANUP* udc, const CItem* item);
    void PerformUserDefinedCleanup(USERDEFINEDCLEANUP* udc, const CItem* item);
    void RefreshAfterUserDefinedCleanup(const USERDEFINEDCLEANUP* udc, CItem* item, std::vector<CItem*> & refreshQueue) const;
    void RecursiveUserDefinedCleanup(USERDEFINEDCLEANUP* udc, const std::wstring& rootPath, const std::wstring& currentPath);
    static void CallUserDefinedCleanup(bool isDirectory, const std::wstring& format, const std::wstring& rootPath, const std::wstring& currentPath, bool showConsoleWindow, bool wait);
    static std::wstring BuildUserDefinedCleanupCommandLine(const std::wstring& format, const std::wstring& rootPath, const std::wstring& currentPath);
    void PushReselectChild(CItem* item);
    CItem* PopReselectChild();
    void ClearReselectChildStack();
    bool IsReselectChildAvailable() const;
    static CompressionAlgorithm CompressionIdToAlg(UINT id);
    static bool FileTreeHasFocus();
    static bool DupeListHasFocus();
    static bool TopListHasFocus();
    static bool SearchListHasFocus();
    static bool WatcherListHasFocus();
    static bool PermsListHasFocus();
    std::vector<CItem*> GetAllSelected();
    void InvalidateSelectionCache();
    static CTreeListControl* GetFocusControl();
    void NotifyPanes(MODEL_CHANGE change = MODEL_CHANGE_NONE, CItem* item = nullptr);

```
**File:** windirstat/Item.h (L118-236)
```text
class __declspec(empty_bases) CItem final : public CTreeListItem
{
public:
    CItem(const CItem&) = delete;
    CItem(CItem&&) = delete;
    CItem& operator=(const CItem&) = delete;
    CItem& operator=(CItem&&) = delete;

    // Construction / Destruction
    CItem(ITEMTYPE type, const std::wstring& name);
    CItem(ITEMTYPE type, const std::wstring& name, FILETIME lastChange, ULONGLONG sizePhysical,
        ULONGLONG sizeLogical, ULONGLONG index, DWORD attributes, ULONG files, ULONG subdirs);
    ~CItem() override;

    // CTreeListItem Interface
    bool DrawSubItem(int subitem, CDC* pdc, CRect rc, UINT state, int* width, int* focusLeft) override;
    std::wstring GetText(int subitem) const override;
    COLORREF GetItemTextColor() const override;
    int CompareSibling(const CTreeListItem* tlib, int subitem) const override;
    int GetTreeListChildCount() const noexcept override { return IsLeaf() ? 0 : static_cast<int>(GetChildren().size()); }
    CTreeListItem* GetTreeListChild(const int i) const noexcept override { return GetChildren()[i]; }
    HICON GetIcon() override;
    void DrawAdditionalState(CDC* pdc, const CRect& rcLabel) const override;
    CItem* GetLinkedItem() noexcept override;

    // Hierarchy / Navigation
    const std::vector<CItem*>& GetChildren() const noexcept;
    bool IsLeaf() const noexcept { return m_folderInfo == nullptr; }
    bool HasChildren() const noexcept { return m_folderInfo != nullptr && !m_folderInfo->m_children.empty(); }
    CItem* GetParent() const noexcept;
    CItem* GetParentDrive() const noexcept;
    CItem* GetVolumeRoot() const noexcept;
    void AddChild(CItem* child, bool addOnly = false);
    void RemoveChild(CItem* child);
    void RemoveAllChildren();

    // Size & Statistics
    ULONGLONG GetSizePhysical() const noexcept;
    ULONGLONG GetSizeLogical() const noexcept;
    ULONGLONG GetSizePhysicalRaw() const noexcept;
    void SetSizePhysical(ULONGLONG size) noexcept;
    void SetSizeLogical(ULONGLONG size) noexcept;
    void UpwardAddSizePhysical(ULONGLONG bytes) noexcept;
    void UpwardSubtractSizePhysical(ULONGLONG bytes) noexcept;
    void UpwardAddSizeLogical(ULONGLONG bytes) noexcept;
    void UpwardSubtractSizeLogical(ULONGLONG bytes) noexcept;
    void UpwardAddFolders(ULONG dirCount) noexcept;
    void UpwardSubtractFolders(ULONG dirCount) noexcept;
    void UpwardAddFiles(ULONG fileCount) noexcept;
    void UpwardSubtractFiles(ULONG fileCount) noexcept;
    double GetFraction() const noexcept;
    double GetAbsoluteFraction() const noexcept;
    ULONG GetFilesCount() const noexcept;
    ULONG GetFoldersCount() const noexcept;
    ULONGLONG GetItemsCount() const noexcept;
    void ExtensionDataAdd();
    void ExtensionDataRemove();
    void ExtensionDataProcessChildren(bool remove = false);

    // Attributes & Properties
    FILETIME GetLastChange() const noexcept;
    void SetLastChange(const FILETIME& t) noexcept;
    void SetAttributes(DWORD attr) noexcept;
    DWORD GetAttributes() const noexcept;
    USHORT GetSortAttributes() const noexcept;
    void SetIndex(ULONGLONG index) noexcept;
    ULONGLONG GetIndex() const noexcept;
    DWORD GetReparseTag() const noexcept;
    void SetReparseTag(DWORD reparseType) noexcept;
    std::wstring GetOwner(bool force = false) const;
    void UpwardUpdateLastChange(const FILETIME& t) noexcept;
    void UpwardRecalcLastChange();

    // Paths & Names
    void SetName(std::wstring_view name);
    std::wstring GetName(const bool stripDrivePrefix = false) const noexcept;
    std::wstring_view GetNameView(const bool stripDrivePrefix = false) const noexcept;
    bool HasExtension(std::wstring_view extension) const noexcept;
    std::wstring GetExtension() const;
    std::wstring GetPath() const;
    int ComparePath(const CItem* other) const;
    std::wstring GetPathLong() const;
    std::wstring GetFolderPath() const;
    bool HasUncPath() const;
    CItem* FindItemByPath(const std::wstring& path) const;

    // Scanning & Done State
    void SetDone();
    bool IsDone() const noexcept { return IsTypeOrFlag(ITF_DONE); }
    void UpwardSetDone() noexcept;
    void UpwardSetUndone() noexcept;
    ULONG GetReadJobs() const noexcept;
    void UpwardAddReadJobs(ULONG count) noexcept;
    void UpwardSubtractReadJobs(ULONG count) noexcept;
    ULONGLONG GetTicksWorked() const noexcept;
    void ResetScanStartTime() const noexcept;
    static void SuspendScanClock() noexcept;
    static void ResumeScanClock() noexcept;
    void SortItemsBySizePhysical() const;
    void SortItemsBySizeLogical() const;
    void UpdateStatsFromDisk();
    static void ScanItems(BlockingQueue<CItem*>*, FinderNtfsContext& contextNtfs, FinderBasicContext& contextBasic);
    static void ScanItemsFinalize(CItem* item);
    static std::vector<CItem*> GetItemsRecursive(const std::vector<CItem*>& initialItems,
        const std::function<bool(CItem*)>& task = [](const CItem* item) { return item->IsTypeOrFlag(IT_FILE); });

    // CTreeMap Interface
    bool TmiIsLeaf() const noexcept { return IsLeaf() || IsTypeOrFlag(IT_HLINKS_IDX); }
    CRect TmiGetRectangle() const noexcept { return tmiRect; };
    void TmiSetRectangle(const CRect& rc) noexcept { tmiRect = rc; }
    COLORREF TmiGetGraphColor() const { return GetGraphColor(); }
    int TmiGetChildCount() const noexcept;
    CItem* TmiGetChild(const int c) const noexcept { return m_folderInfo->m_children[c]; }
    ULONGLONG TmiGetSize() const noexcept;

    // Drive/Volume Specific
    bool IsRootItem() const noexcept;
    std::vector<CItem*> GetDriveItems() const;
    ULONGLONG GetProgressRange() const;
```
**File:** windirstat/WinDirStat.cpp (L231-414)
```cpp
class CWinDirStatCommandLineInfo final : public CCommandLineInfo
{
    std::wstring m_pendingFlag;
    std::wstring m_operationFlag;
    bool m_hasParsedParam = false;
    bool m_hasPathParam = false;
    bool m_malformedFlag = false;
    bool m_invalidPath = false;
    const std::wstring saveToFlag = L"saveto";
    const std::wstring saveDupesToFlag = L"savedupesto";
    const std::wstring savePermsToFlag = L"savepermsto";
    const std::wstring loadFromFlag = L"loadfrom";
    const std::wstring legacyUninstallFlag = L"legacyuninstall";

public:

    bool HasMalformedCommandLine() const noexcept
    {
        return m_malformedFlag || !m_pendingFlag.empty() ||
            (m_operationFlag == loadFromFlag && m_hasPathParam);
    }
    bool HasInvalidPath() const noexcept { return m_invalidPath; }
    bool IsLegacyUninstallRequested() const noexcept { return m_operationFlag == legacyUninstallFlag; }

    void ParseParam(const WCHAR* pszParam, BOOL bFlag, BOOL bLast) override
    {
        const bool hadPriorParam = m_hasParsedParam;
        m_hasParsedParam = true;

        // Normalize string for parsing
        std::wstring param{ pszParam };
        TrimString(param, wds::chrDoubleQuote);
        TrimString(param, wds::chrBackslash, true);

        // If we have a pending flag, this non-flag param is its value
        if (!m_pendingFlag.empty() && !bFlag)
        {
            if (param.empty())
            {
                m_malformedFlag = true;
            }
            else if (m_pendingFlag == saveToFlag)
            {
                CDirStatApp::Get()->m_saveToPath = param;
                COptions::ScanForDuplicates = false;
            }
            else if (m_pendingFlag == saveDupesToFlag)
            {
                CDirStatApp::Get()->m_saveDupesToPath = param;
                COptions::ScanForDuplicates = true;
            }
            else if (m_pendingFlag == savePermsToFlag)
            {
                CDirStatApp::Get()->m_savePermsToPath = param;
            }
            else if (m_pendingFlag == loadFromFlag)
            {
                CDirStatApp::Get()->m_loadFromPath = param;
            }

            m_pendingFlag.clear();
            return;
        }

        // Handle any non-flags as paths
        if (!bFlag)
        {
            m_hasPathParam = true;
            if (param.empty())
            {
                m_invalidPath = true;
                m_malformedFlag = true;
                return;
            }
            for (const auto& paramSpilt : SplitString(param))
            {
                if (paramSpilt.empty())
                {
                    m_invalidPath = true;
                    continue;
                }
                std::error_code ec;
                const std::wstring fullPath = std::filesystem::absolute(paramSpilt + L"\\", ec).wstring();
                if (!ec && FolderExists(fullPath))
                {
                    if (!m_strFileName.IsEmpty()) m_strFileName += wds::chrPipe;
                    m_strFileName += fullPath.c_str();
                }
                else
                {
                    m_invalidPath = true;
                }
            }
            return;
        }

        // Handle flags
        // Value-taking flags require an immediate non-flag value.
        if (!m_pendingFlag.empty())
        {
            m_malformedFlag = true;
            return;
        }
        param = MakeLower(param);
        if (param == saveToFlag || param == saveDupesToFlag || param == savePermsToFlag || param == loadFromFlag)
        {
            if (!m_operationFlag.empty()) m_malformedFlag = true;
            else m_operationFlag = param;
            m_pendingFlag = param;
            if (bLast) m_malformedFlag = true;
        }
        else if (param == legacyUninstallFlag)
        {
            // Defer this destructive standalone action until parsing is validated.
            if (hadPriorParam || !bLast || !m_operationFlag.empty()) m_malformedFlag = true;
            else
            {
                m_operationFlag = param;
            }
        }
    }
};

BOOL CDirStatApp::InitInstance()
{
    // Restrict DLL search to System32 — prevents DLL hijacking from CWD or PATH
    if (const auto pSetDefaultDllDirectories = reinterpret_cast<decltype(&SetDefaultDllDirectories)>(
        GetProcAddress(GetModuleHandle(L"kernel32.dll"), "SetDefaultDllDirectories"));
        pSetDefaultDllDirectories && IsWindows8OrGreater())
    {
        pSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    }

    // Prevent state saving
    m_bSaveState = FALSE;

    // Load default language just to get bootstrapped
    Localization::LoadResource(MAKELANGID(LANG_ENGLISH, SUBLANG_NEUTRAL));

    // If a local config file is available, use that for settings
    SetPortableMode(true, true);

    COptions::LoadAppSettings();
    SetProcessPriority(COptions::ProcessPriority);
    LoadStdProfileSettings(0);

    // Silently restart elevated conditionally before any expensive initialization
    if (IsElevationAvailable() && COptions::AutoElevate && !COptions::ShowElevationPrompt) // only if user doesn't want to be prompted
    {
        RunElevated(m_lpCmdLine);
    }

    // Set app to prefer dark mode
    DarkMode::SetAppDarkMode();
    CWinAppEx::InitInstance();

    // Initialize visual controls
    constexpr INITCOMMONCONTROLSEX ctrls = { sizeof(INITCOMMONCONTROLSEX) , ICC_STANDARD_CLASSES };
    (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    (void)InitCommonControlsEx(&ctrls);
    (void)AfxInitRichEdit2();

    // Initialize GDI Plus
    const Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    CWinDirStatCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (cmdInfo.HasMalformedCommandLine()) ExitProcess(1);

    if (cmdInfo.IsLegacyUninstallRequested())
    {
        LegacyUninstall();
        // Terminate even if cleanup returned early.
        ExitProcess(0);
    }

    // Check if we should hide the app window
    const bool hideApp = !m_saveToPath.empty() || !m_saveDupesToPath.empty() || !m_savePermsToPath.empty();
    if (hideApp && (cmdInfo.m_strFileName.IsEmpty() || cmdInfo.HasInvalidPath())) ExitProcess(1);
    if (hideApp) m_nCmdShow = SW_HIDE;

    m_model = std::make_unique<CWinDirStatModel>();
```
**File:** windirstat/WinDirStat.cpp (L451-463)
```cpp
    // Allow user to elevate if desired
    if (IsElevationAvailable() && COptions::ShowElevationPrompt && !hideApp)
    {
        if (const auto [nID, isChecked] =
            CMessageBoxDlg::Show(Localization::Lookup(IDS_ELEVATION_QUESTION), Localization::Lookup(IDS_DONT_SHOW_AGAIN),
                false, MB_YESNO | MB_ICONQUESTION, m_pMainWnd);
            (COptions::ShowElevationPrompt = !isChecked, nID == IDYES))
        {
            if (isChecked) COptions::AutoElevate = true;
            RunElevated(m_lpCmdLine);
            return FALSE;
        }
    }
```
**File:** windirstat/WinDirStatModel.cpp (L92-154)
```cpp
BOOL CWinDirStatModel::StartScan(const std::wstring& pathSpec)
{
    // Expand All Files view to full window during scan
    CMainFrame::Get()->ExpandFileTabbedView();

    // Decode list of folders to scan
    std::vector<std::wstring> selections = SplitString(pathSpec);

    // Prepare for new root and delete any existing data
    ClearScanState();

    // Persist the full scan spec, which may contain pipe-separated roots.
    Get()->SetScanPathSpec(pathSpec);

    // Count number of drives for validation
    const std::wregex driveMatch(LR"(^[A-Za-z]:[\\]?$)", std::regex_constants::optimize);
    const auto driveCount = static_cast<size_t>(std::ranges::count_if(selections, [&](const std::wstring& str) {
        return std::regex_match(str, driveMatch);
    }));

    // Reject an empty path list
    if (selections.empty()) return false;

    // Multiple selections are supported only when every selection is a drive
    if (selections.size() >= 2 && selections.size() != driveCount) return false;

    // Determine if we should add multiple drives under a single node
    if (selections.size() >= 2)
    {
        // Fetch the localized string for the root computer object
        SmartPointer pidl(CoTaskMemFree, static_cast<LPITEMIDLIST>(nullptr));
        SmartPointer ppszName(CoTaskMemFree, static_cast<LPWSTR>(nullptr));
        CComPtr<IShellItem> psi;
        if (FAILED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &pidl)) ||
            SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&psi)) != S_OK ||
            FAILED(psi->GetDisplayName(SIGDN_NORMALDISPLAY, &ppszName)))
        {
            ASSERT(FALSE);
        }

        const std::wstring name = ppszName != nullptr ? *ppszName : Localization::Lookup(IDS_THISPC);
        m_rootItem = new CItem(IT_MYCOMPUTER | ITF_ROOTITEM, name);
        for (const auto& rootFolder : selections)
        {
            const auto drive = new CItem(IT_DRIVE, rootFolder);
            m_rootItem->AddChild(drive);
        }
    }
    else
    {
        const ITEMTYPE type = driveCount == 1 ? IT_DRIVE : IT_DIRECTORY;
        m_rootItem = new CItem(type | ITF_ROOTITEM, selections.front());
        m_rootItem->UpdateStatsFromDisk();
    }

    // Restore zoom scope to be the root
    m_zoomItem = m_rootItem;

    // Update new root for display
    NotifyPanes(MODEL_CHANGE_NEW_ROOT);
    StartScanningEngine(std::vector({ Get()->GetRootItem() }));
    return true;
}
```
**File:** windirstat/WinDirStatModel.cpp (L617-641)
```cpp

    if (!cancelled && !itemsToDelete.empty())
    {
        DWORD flags = FOFX_SHOWELEVATIONPROMPT | FOF_NOCONFIRMATION;
        if (toTrashBin) flags |= FOFX_ADDUNDORECORD | FOFX_RECYCLEONDELETE;

        const auto doDelete = [&](HWND hwnd, DWORD opFlags)
        {
            CComPtr<IFileOperation> fileOperation;
            if (FAILED(::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&fileOperation))) ||
                FAILED(fileOperation->SetOwnerWindow(hwnd)) ||
                FAILED(fileOperation->SetOperationFlags(opFlags)))
                return;
            if (const CComPtr<IShellItemArray> psia = CreateShellItemArray(itemsToDelete))
                fileOperation->DeleteItems(psia);
            const HRESULT res = fileOperation->PerformOperations();
            if (res != S_OK) VTRACE(L"File Operation Failed: {}", TranslateError(res));
        };

        if (COptions::ShowMicrosoftProgress)
            doDelete(*AfxGetMainWnd(), flags);
        else
            CProgressDlg(0, CProgressDlg::Flags::None, AfxGetMainWnd(), [&](const CProgressDlg* pdlg)
                { doDelete(*pdlg, flags | FOF_NO_UI); }).DoModal();
    }
```
**File:** windirstat/Item.cpp (L1077-1078)
```cpp
                    CItem* newitem = item->AddFile(*finder);
                    CFileDupeControl::Get()->ProcessDuplicate(newitem, queue);
```
**File:** windirstat/WinDirStatModel.Actions.cpp (L592-605)
```cpp
void CWinDirStatModel::OnCleanupDeleteToBin()
{
    DeletePhysicalItems(GetAllSelected(), true);
}

void CWinDirStatModel::OnCleanupDelete()
{
    DeletePhysicalItems(GetAllSelected(), false);
}

void CWinDirStatModel::OnCleanupEmptyFolder()
{
    DeletePhysicalItems(GetAllSelected(), false, true);
}
```
**File:** windirstat/WinDirStatModel.Actions.cpp (L607-666)
```cpp
void CWinDirStatModel::OnCleanupMoveTo()
{
    const auto& items = GetAllSelected();
    if (items.empty()) return;

    // Show folder browser dialog to get destination directory
    CFolderPickerDialog dlg(nullptr, OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_DONTADDTORECENT);
    dlg.m_ofn.lpstrTitle = wds::strWinDirStat;

    if (dlg.DoModal() != IDOK) return;
    const std::wstring destFolder = dlg.GetPathName().GetString();

    // Verify destination exists
    if (!FolderExists(destFolder)) return;

    // Show progress dialog and move files
    CProgressDlg(0, CProgressDlg::Flags::None, AfxGetMainWnd(), [&](const CProgressDlg* pdlg)
    {
        // Create file operation object
        CComPtr<IFileOperation> fileOperation;
        CComPtr<IShellItem> destShellItem;
        const auto flags = FOFX_SHOWELEVATIONPROMPT | FOF_NOCONFIRMATION | (COptions::ShowMicrosoftProgress ? 0 : FOF_NO_UI);
        if (FAILED(::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&fileOperation))) ||
            FAILED(fileOperation->SetOwnerWindow(*pdlg)) ||
            FAILED(fileOperation->SetOperationFlags(flags)) ||
            FAILED(SHCreateItemFromParsingName(destFolder.c_str(), nullptr, IID_PPV_ARGS(&destShellItem))))
        {
            return;
        }

        if (const CComPtr<IShellItemArray> psia = CreateShellItemArray(items))
            fileOperation->MoveItems(psia, destShellItem);

        // Do all moves
        const HRESULT res = fileOperation->PerformOperations();
        if (res != S_OK) VTRACE(L"File Operation Failed: {}", TranslateError(res));
    }).DoModal();

    // Refresh the parent items of the moved files
    std::vector<CItem*> refresh;
    for (const auto& item : items)
    {
        if (item->GetParent() != nullptr &&
            std::ranges::find(refresh, item->GetParent()) == refresh.end())
        {
            refresh.push_back(item->GetParent());
        }
    }

    // Add in destination directory as a refresh location if present
    if (const auto destItem = GetRootItem()->FindItemByPath(destFolder); destItem != nullptr)
    {
        refresh.push_back(destItem);
    }

    if (!refresh.empty())
    {
        RefreshItem(refresh);
    }
}
```
**File:** windirstat/WinDirStatModel.Actions.cpp (L763-780)
```cpp
void CWinDirStatModel::OnUpdateUserDefinedCleanup(CCmdUI* pCmdUI)
{
    const int i = pCmdUI->m_nID - ID_USERDEFINEDCLEANUP0;
    if (!IsScanSettled())
    {
        return pCmdUI->Enable(FALSE);
    }

    const auto & items = GetAllSelected();
    bool allowControl = (FileTreeHasFocus() || DupeListHasFocus() || TopListHasFocus()) &&
        COptions::UserDefinedCleanups.at(i).Enabled && !items.empty();
    if (allowControl) for (const auto & item : items)
    {
        allowControl &= UserDefinedCleanupWorksForItem(&COptions::UserDefinedCleanups[i], item);
    }

    pCmdUI->Enable(allowControl);
}
```
**File:** windirstat/WinDirStatModel.Actions.cpp (L782-817)
```cpp
void CWinDirStatModel::OnUserDefinedCleanup(const UINT id)
{
    if (!IsScanSettled())
    {
        return;
    }

    USERDEFINEDCLEANUP* udc = &COptions::UserDefinedCleanups[id - ID_USERDEFINEDCLEANUP0];
    const auto & items = GetAllSelected();
    std::vector<CItem*> refreshQueue;
    for (const auto & item : items)
    {
        ASSERT(UserDefinedCleanupWorksForItem(udc, item));
        if (!UserDefinedCleanupWorksForItem(udc, item))
        {
            return;
        }

        try
        {
            AskForConfirmation(udc, item);
            PerformUserDefinedCleanup(udc, item);
            RefreshAfterUserDefinedCleanup(udc, item, refreshQueue);
        }
        catch (...)
        {
            // error would have been displayed already
        }
    }

    // process refresh queue
    if (!refreshQueue.empty())
    {
        RefreshItem(refreshQueue);
    }
}
```
**File:** windirstat/WinDirStatModel.Actions.cpp (L1147-1199)
```cpp
    m_thread = std::jthread([this,items, visualInfo] () mutable
    {
        // Add items to processing queue
        for (const auto & item : items)
        {
            // Skip any items we should not follow
            if (!item->IsTypeOrFlag(ITF_ROOTITEM) && !CDirStatApp::Get()->IsFollowingAllowed(item->GetReparseTag()))
            {
                continue;
            }

            item->UpwardAddReadJobs(1);
            item->UpwardSetUndone();

            // Create status progress bar
            CMainFrame::Get()->InvokeInMessageThread([]
            {
                CMainFrame::Get()->UpdateProgress();
            });

            // Separate into separate m_queues per volume
            m_queues[item->GetVolumeRoot()->GetPath()].Push(item);
        }

        // Create subordinate threads if there is work to do
        std::unordered_map<std::wstring, FinderNtfsContext> queueContextNtfs;
        std::unordered_map<std::wstring, FinderBasicContext> queueContextBasic;
        for (auto& queue : m_queues)
        {
            queueContextNtfs.try_emplace(queue.first);
            queueContextBasic.try_emplace(queue.first);

            auto* queuePtr = &queue.second;
            auto* ntfsCtx = &queueContextNtfs[queue.first];
            auto* basicCtx = &queueContextBasic[queue.first];
            queue.second.StartThreads(COptions::ScanningThreads, [queuePtr, ntfsCtx, basicCtx]()
            {
                CItem::ScanItems(queuePtr, *ntfsCtx, *basicCtx);
            });
        }

        // Ensure toolbar buttons reflect scanning status
        CMainFrame::Get()->InvokeInMessageThread([]
        {
            CDirStatApp::Get()->OnIdle(0);
            CMainFrame::Get()->Invalidate();
        });

        // Wait for all threads to run out of work
        StopReason stopReason = Default;
        for (auto& queue : m_queues | std::views::values)
            stopReason = static_cast<StopReason>(queue.WaitForCompletion());

```
**File:** windirstat/CsvLoader.cpp (L396-568)
```cpp
CItem* LoadResults(const std::wstring& path)
{
    std::ifstream reader(path);
    std::vector<char> buffer(1ul * wds::Mi);
    reader.rdbuf()->pubsetbuf(buffer.data(), buffer.size());
    if (!reader.is_open()) return nullptr;

    char bom[3] = {};
    if (reader.read(bom, 3); std::string_view(bom, 3) != "\xEF\xBB\xBF")
    {
        reader.clear();
        reader.seekg(0);
    }

    return IsJsonPath(path) ? LoadResultsJson(reader) : LoadResultsCsv(reader);
}

// Walk the item tree breadth-first and return a flat ordered list
static std::vector<const CItem*> CollectItems(CItem* rootItem)
{
    std::vector<const CItem*> items;
    items.reserve(static_cast<size_t>(rootItem->GetItemsCount()));

    std::vector<CItem*> queue({ rootItem });
    while (!queue.empty())
    {
        const CItem* qitem = queue.back();
        queue.pop_back();

        if (qitem->IsTypeOrFlag(IT_HLINKS)) continue;

        items.push_back(qitem);
        if (qitem->IsLeaf()) continue;

        std::vector<CItem*> children = qitem->GetChildren();
        std::ranges::sort(children, [](auto a, auto b)
        {
            return _wcsicmp(a->GetNameView().data(), b->GetNameView().data()) > 0;
        });

        queue.insert(queue.end(), children.begin(), children.end());
    }
    return items;
}

// Compute adjusted physical sizes (undoes hardlink accounting)
static std::unordered_map<const CItem*, LONGLONG>
    ComputeAdjustedSizes(const std::vector<const CItem*>& items)
{
    std::unordered_map<const CItem*, LONGLONG> adjustedSizes;
    for (const auto* item : items)
    {
        if (item->IsTypeOrFlag(IT_DRIVE))
            if (const auto hlinks = item->FindHardlinksItem(); hlinks != nullptr)
            {
                adjustedSizes[item] -= hlinks->GetSizePhysicalRaw();
                if (const auto root = item->GetParent(); root != nullptr)
                    adjustedSizes[root] -= hlinks->GetSizePhysicalRaw();
            }

        if (!item->IsTypeOrFlag(ITF_HARDLINK)) continue;
        for (const CItem* p = item->GetParent(); p != nullptr; p = p->GetParent())
        {
            adjustedSizes[p] += item->GetSizePhysicalRaw();
        }
    }
    return adjustedSizes;
}

static bool SaveResultsCsv(std::ofstream& outf, const std::vector<const CItem*>& items,
    const std::vector<std::wstring>& cols, const std::unordered_map<const CItem*, LONGLONG>& adjustedSizes,
    const bool includeOwner)
{
    // Header
    for (size_t i = 0; i < cols.size(); ++i)
        outf << QuoteAndConvert(cols[i]) << (i + 1 < cols.size() ? "," : "");

    // Rows
    for (const auto* item : items)
    {
        const bool nonPathItem = item->IsTypeOrFlag(IT_MYCOMPUTER);
        const ITEMTYPE itemType = item->GetRawType() & ~ITF_HARDLINK & ~ITHASH_MASK & ~ITF_EXTDATA;
        const auto adjIt = adjustedSizes.find(item);
        const auto adjustedSize = adjIt != adjustedSizes.end() ? adjIt->second : 0;
        std::format_to(std::ostreambuf_iterator<char>(outf), "\r\n{},{},{},{},{},{},{},0x{:08X},0x{:016X}",
            QuoteAndConvert(nonPathItem ? item->GetName() : item->GetPath()),
            item->GetFilesCount(),
            item->GetFoldersCount(),
            item->GetSizeLogical(),
            item->GetSizePhysicalRaw() + adjustedSize,
            QuoteAndConvert(FormatAttributes(item->GetAttributes())),
            ToTimePoint(item->GetLastChange()),
            static_cast<std::uint32_t>(itemType),
            item->GetIndex());
        if (includeOwner) outf << "," << QuoteAndConvert(item->GetOwner(true));
    }
    outf.flush();
    return outf.good();
}

// ── JSON save ─────────────────────────────────────────────────────────────────

static bool SaveResultsJson(std::ofstream& outf,
    const std::vector<const CItem*>& items,
    const std::vector<std::wstring>& cols,
    const std::unordered_map<const CItem*, LONGLONG>& adjustedSizes,
    const bool includeOwner)
{
    // Pre-quote all column key strings once (cols are in FIELD_* index order)
    std::array<std::string, FIELD_COUNT> jk;
    for (size_t i = 0; i < cols.size(); ++i) jk[i] = JsonQuoteW(cols[i]);
    const std::string jkOwner = includeOwner ? JsonQuoteW(cols[FIELD_OWNER]) : std::string{};

    outf << "[\r\n";
    bool firstItem = true;
    for (const auto* item : items)
    {
        if (!firstItem) outf << ",\r\n";
        firstItem = false;

        const bool nonPathItem = item->IsTypeOrFlag(IT_MYCOMPUTER);
        const ITEMTYPE itemType = item->GetRawType() & ~ITF_HARDLINK & ~ITHASH_MASK & ~ITF_EXTDATA;
        const auto adjIt = adjustedSizes.find(item);
        const auto adjustedSize = adjIt != adjustedSizes.end() ? adjIt->second : 0;

        // Write one JSON object per item
        outf << "{\r\n";
        outf << "  " << jk[FIELD_NAME]           << ": " << JsonQuoteW(nonPathItem ? item->GetName() : item->GetPath()) << ",\r\n";
        outf << "  " << jk[FIELD_FILES]          << ": " << item->GetFilesCount()                                       << ",\r\n";
        outf << "  " << jk[FIELD_FOLDERS]        << ": " << item->GetFoldersCount()                                     << ",\r\n";
        outf << "  " << jk[FIELD_SIZE_LOGICAL]   << ": " << item->GetSizeLogical()                                      << ",\r\n";
        outf << "  " << jk[FIELD_SIZE_PHYSICAL]  << ": " << (item->GetSizePhysicalRaw() + adjustedSize)                << ",\r\n";
        outf << "  " << jk[FIELD_ATTRIBUTES]     << ": " << JsonQuoteW(FormatAttributes(item->GetAttributes()))        << ",\r\n";
        outf << "  " << jk[FIELD_LAST_CHANGE]    << ": " << JsonQuote(ToTimePoint(item->GetLastChange()))              << ",\r\n";
        std::format_to(std::ostreambuf_iterator<char>(outf), "  {}: \"0x{:08X}\",\r\n  {}: \"0x{:016X}\"",
            jk[FIELD_ATTRIBUTES_WDS], static_cast<std::uint32_t>(itemType),
            jk[FIELD_INDEX], item->GetIndex());
        if (includeOwner)
            outf << ",\r\n  " << jkOwner << ": " << JsonQuoteW(item->GetOwner(true));
        outf << "\r\n}";
    }
    outf << "\r\n]\r\n";
    outf.flush();
    return outf.good();
}

bool SaveResults(const std::wstring& path, CItem* rootItem)
{
    const std::vector<const CItem*> items       = CollectItems(rootItem);
    const auto                      adjustedSizes = ComputeAdjustedSizes(items);
    const bool includeOwner = COptions::IsColumnVisible(COptions::FileTreeColumnVisibility.Obj(), COL_OWNER);

    std::vector<std::wstring> cols =
    {
        Localization::Lookup(IDS_COL_NAME),
        Localization::Lookup(IDS_COL_FILES),
        Localization::Lookup(IDS_COL_FOLDERS),
        Localization::Lookup(IDS_COL_SIZE_LOGICAL),
        Localization::Lookup(IDS_COL_SIZE_PHYSICAL),
        Localization::Lookup(IDS_COL_ATTRIBUTES),
        Localization::Lookup(IDS_COL_LAST_CHANGE),
        Localization::LookupNeutral(AFX_IDS_APP_TITLE) + L" " + Localization::Lookup(IDS_COL_ATTRIBUTES),
        Localization::Lookup(IDS_COL_INDEX)
    };
    if (includeOwner) cols.push_back(Localization::Lookup(IDS_COL_OWNER));

    std::ofstream outf(path, std::ios::binary);
    if (!outf.is_open()) return false;

    return IsJsonPath(path)
        ? SaveResultsJson(outf, items, cols, adjustedSizes, includeOwner)
        : SaveResultsCsv (outf, items, cols, adjustedSizes, includeOwner);
}
```
## Q2
1. Detail where to config the MCP in the UI (e.g. copy url, allow delete operations)
2. Create a full plan for a coding agent to take on, ready for production
### Answer
## 1. Where to add MCP configuration in the UI

WinDirStat's settings live in a tabbed `CMFCPropertySheet` (`COptionsPropertySheet`), built in `COptionsPropertySheet::ShowSettings()`, which currently instantiates and adds pages for General, Filtering, File Tree, Treemap, Permissions, Cleanups, Prompts, and Advanced windirstat:126-151 . Each page is a `COptionsPage`-derived class (e.g. `CPageCleanups`) backed by a `.rc` dialog template and bound to `COptions::*` static `Setting<T>` members via `DoDataExchange` windirstat:26-43 .

The natural place for MCP settings is a **new page**, e.g. `CPageMcp` ("MCP Server" or "AI Agent"), added alongside the others:

```cpp
auto mcp = std::make_unique<CPageMcp>();
sheet->AddPage(mcp.get()); // new tab, e.g. after "Advanced"
```

This new page's dialog template (in `windirstat.rc`, following the pattern of `IDD_PAGE_ADVANCED` / `IDD_PAGE_CLEANUPS`) windirstat:566-599  would contain:

- **Enable MCP Server** checkbox — master on/off switch.
- **Transport** combo box: `stdio` / `Named Pipe` / `Local TCP (127.0.0.1)`.
- **Port / Pipe name** edit box (enabled only when a listening transport is chosen), plus a **"Copy URL"** or **"Copy Connection Info"** push button that copies a ready-to-paste endpoint string (e.g. `http://127.0.0.1:<port>/mcp` or the pipe path) to the clipboard — analogous to the existing color/help push buttons on other pages.
- **Auth token** edit box (read-only, generated) with a **"Regenerate"** button, and a **"Copy Token"** button.
- A **"Allow destructive operations"** checkbox group, mirroring the granularity of existing cleanup toggles:
  - Allow delete to Recycle Bin
  - Allow permanent delete
  - Allow move/rename
  - Allow running user-defined cleanups
  - (Each unchecked by default — read-only navigation/analysis tools should be allowed by default, but all mutation tools opt-in.)
- **"Require confirmation prompt for destructive actions"** checkbox, reusing the existing prompt-gating pattern already used for delete/empty-folder warnings (`ShowDeletePermanentlyWarning`, `ShowDeleteToRecycleBinWarning`, etc.) windirstat:185-188 .

These would all be new `Setting<T>` entries in `COptions`, following the exact declarative pattern already used for every other option (section, key, default, min/max) windirstat:163-230 , and persisted automatically the same way as `bool`/`int`/`std::wstring` settings are today via `Setting<T>::ReadPersistedProperty`/`WritePersistedProperty` windirstat:40-118 . No new persistence mechanism is needed — just add entries under a new `OptionsMcp` section constant next to `OptionsGeneral`, `OptionsAdvanced`, etc.

Command-line/headless control (for launching WinDirStat already listening) would extend `CWinDirStatCommandLineInfo` in `WinDirStat.cpp`, which already parses flags like `/saveto`, `/loadfrom` and toggles hidden-window mode windirstat:231-329 .

---

## 2. Full production plan


Implement a local Model Context Protocol (MCP) server inside the WinDirStat MFC application (repo `windirstat/windirstat`) so an AI agent can navigate the scanned directory tree and safely trigger cleanup actions. Do the work in the following stages.

### Stage 1 — Transport and process plumbing
1. Add a new source module `windirstat/Mcp/McpServer.h` / `.cpp` implementing a minimal JSON-RPC 2.0 server compliant with MCP's `stdio` transport first (simplest, no networking/firewall concerns), then optionally a local TCP/loopback (`127.0.0.1`) transport with SSE for streaming responses, and/or a Windows named pipe transport. Use a background `std::jthread` for the accept/read loop so it never blocks the MFC message pump, following the existing threading pattern used for scanning (`CWinDirStatModel::StartScanningEngine` using `std::jthread` and `m_queues`) in `windirstat/WinDirStatModel.Actions.cpp` (search for `m_thread = std::jthread`).
2. All calls that touch `CWinDirStatModel`, `CItem`, or UI state MUST be marshaled onto the main UI thread. Reuse the existing `CMainFrame::InvokeInMessageThread()` mechanism already used to post UI updates from worker threads (see its use in `windirstat/WinDirStatModel.Actions.cpp` around the scan-progress callback). Implement a synchronous "call on UI thread and wait for result" helper (condition variable + result future) for MCP request handlers, since MCP requests are request/response.
3. Add `/mcp`, `/mcp-transport=<stdio|tcp|pipe>`, `/mcp-port=<n>` command-line flags to `CWinDirStatCommandLineInfo` in `windirstat/WinDirStat.cpp` (extend the existing flag-parsing switch that already handles `saveto`/`savedupesto`/`savepermsto`/`loadfrom`). When present, start the MCP server during `CDirStatApp::InitInstance()` right after `m_model = std::make_unique<CWinDirStatModel>();` is constructed.

### Stage 2 — Options / persisted settings
4. In `windirstat/Options.h`, add a new section constant `OptionsMcp = L"McpServer"` and new `Setting<T>` members (following the exact pattern of existing settings such as `ShowDeletePermanentlyWarning`):
   - `Setting<bool> McpEnabled`
   - `Setting<int> McpTransport` (0=stdio, 1=TCP, 2=named pipe)
   - `Setting<int> McpPort` (default e.g. 38271, with min/max validation)
   - `Setting<std::wstring> McpAuthToken` (generated via a secure random token on first enable; store hashed if feasible, or plaintext with a strong warning, consistent with how other secrets/paths are stored today)
   - `Setting<bool> McpAllowDeleteToRecycleBin`
   - `Setting<bool> McpAllowDeletePermanently`
   - `Setting<bool> McpAllowMove`
   - `Setting<bool> McpAllowUserDefinedCleanups`
   - `Setting<bool> McpRequireConfirmationPrompt`
   These are automatically persisted through the existing `PersistedSetting::ReadPersistedProperties()` mechanism in `windirstat/Property.cpp` — no extra serialization code needed.

### Stage 3 — Settings UI page
5. Create `windirstat/Pages/PageMcp.h` / `.cpp` (`CPageMcp : public COptionsPage`) modeled directly on `windirstat/Pages/PageCleanups.h`/`.cpp` and `windirstat/Pages/PageAdvanced.*`. Include:
   - Enable checkbox, transport combo, port/pipe edit box.
   - "Copy Connection Info" button that builds a connection string (`stdio` command line to launch, or `http://127.0.0.1:<port>/mcp`, or the pipe path) and puts it on the clipboard via `SetClipboardData`/`CString` clipboard helpers (check for an existing clipboard helper utility in the codebase before writing a new one).
   - Auth token display (masked) + "Regenerate" + "Copy Token" buttons.
   - Checkboxes for each `McpAllow*` setting and the confirmation-prompt setting.
   - Add the corresponding `IDD_PAGE_MCP` dialog template to `windirstat/windirstat.rc` (copy layout style from `IDD_PAGE_ADVANCED`), and add all new string resources (`IDS_PAGE_MCP_*`) to the localization resource files used by `Localization::Lookup` (check `windirstat/Localization/` or equivalent resource string tables for the existing pattern, and update all shipped language files consistently, or at minimum the English base and mark others for translation).
6. Register the new page in `COptionsPropertySheet::ShowSettings()` in `windirstat/MainFrame.cpp`: add `auto mcp = std::make_unique<CPageMcp>();` and `sheet->AddPage(mcp.get());`.
7. Update the settings-dialog automation test list in `tests/Test-WinDirStat.ps1` (`$expectedPages` array in `Test-SettingsDialog`) to include the new tab name so existing UI test coverage doesn't regress.

### Stage 4 — MCP protocol/tool implementation
8. Implement MCP `initialize`, `tools/list`, and `tools/call` handlers. Expose the following tools, each implemented as a thin adapter over existing model/item APIs (do not duplicate logic — call into `CWinDirStatModel`/`CItem` directly):
   - `scan_path(path)` → `CWinDirStatModel::StartScan()`; poll/await `IsScanSettled()` before responding, with a reasonable timeout and a "still scanning" status response for long scans (consider an async job pattern with a `scan_status`/`get_scan_status` tool since large scans can take a long time and MCP requests should not block indefinitely).
   - `get_scan_status()` → `IsScanRunning()`, `IsScanSettled()`, `IsRootDone()`.
   - `get_tree(path, max_depth, min_size)` → resolve the `CItem*` via `CItem::FindItemByPath`, then walk `GetChildren()` recursively up to `max_depth`/`min_size`, returning path, size (logical+physical), file/folder counts, attributes, and last-change time per node. Reuse the JSON emission style already implemented in `windirstat/CsvLoader.cpp`'s `SaveResultsJson` for field naming consistency.
   - `find_large_items(path, top_n)` → `CItem::GetItemsRecursive()` with a predicate, sorted by size, capped at `top_n`.
   - `find_duplicates()` → expose duplicate-group data already computed during scan (locate the duplicate-tracking structure, e.g. `CFileDupeControl`, used by `Item.cpp`'s `ProcessDuplicate`, and serialize its groups).
   - `get_extension_summary()` → `CWinDirStatModel::GetExtensionData()`.
   - `get_selection()` → `CWinDirStatModel::GetAllSelected()` (useful if the human has pre-selected items in the GUI for the agent to act on).
   - `delete_items(paths[], mode)` (mode = `recycle_bin` | `permanent` | `empty_folder`) → resolve paths to `CItem*` and call `CWinDirStatModel::DeletePhysicalItems()`. **Gate on `COptions::McpAllowDeleteToRecycleBin` / `McpAllowDeletePermanently` respectively; reject with a clear error if disabled.**
   - `move_items(paths[], destination)` → adapt the logic in `CWinDirStatModel::OnCleanupMoveTo()` into a reusable non-UI-driven method (extract the `IFileOperation` logic so it can be called both from the menu handler and from MCP without a folder-picker dialog). Gate on `McpAllowMove`.
   - `run_user_defined_cleanup(id, paths[])` → `CWinDirStatModel::PerformUserDefinedCleanup()`/`OnUserDefinedCleanup` pattern, validated with `UserDefinedCleanupWorksForItem()`. Gate on `McpAllowUserDefinedCleanups`.
   - `refresh(paths[])` → `CWinDirStatModel::RefreshItem()`.
9. For every mutating tool, if `COptions::McpRequireConfirmationPrompt` is true, surface the same confirmation dialog the UI already shows (e.g. `CMessageBoxDlg`/`AskForConfirmation`) on the UI thread before proceeding, and fail/timeout the MCP call if the user dismisses it — mirroring `CWinDirStatModel::AskForConfirmation` used by `OnUserDefinedCleanup`.
10. Enforce the auth token: every incoming MCP request (for TCP/pipe transports) must include/validate `McpAuthToken`; reject otherwise. For `stdio` transport, auth may be skipped since the process is launched directly by the trusted local agent.

### Stage 5 — Safety, logging, and lifecycle
11. Add structured logging of every MCP tool call (tool name, arguments, allow/deny decision, result) to WinDirStat's existing trace/log facility (check for `VTRACE` usage already present in `WinDirStatModel.cpp`, e.g. the `VTRACE(L"File Operation Failed: {}", ...)` call, and use the same mechanism).
12. Ensure the MCP server thread is cleanly shut down in `CDirStatApp::ExitInstance()` (or equivalent teardown path) — stop listening, join the thread, and release any pipe/socket handles.
13. Ensure enabling/disabling the server at runtime (via the new settings page, without app restart) starts/stops the listener thread immediately, consistent with how other live-toggle settings (e.g., dark mode) apply without restart, and only requests an app restart if genuinely necessary (unlikely for this feature; avoid adding it to the restart-required setting list unless a technical constraint forces it).

### Stage 6 — Tests
14. Add unit/integration tests (or extend `tests/Test-WinDirStat.ps1` UI automation and any C++ unit test project) covering: settings persistence round-trip for all new `McpEnabled`/`McpAllow*` settings, tool-call gating (deny when disabled, allow when enabled), `get_tree` correctness against a known scanned fixture, and delete/move tools correctly refusing to operate when the corresponding `Allow*` flag is off.
15. Document the new feature (README or `docs/`), including how to launch WinDirStat headless with `/mcp`, the settings page walkthrough, and the security model (local-only, opt-in, token-gated, destructive actions off by default).
### Citations
**File:** windirstat/MainFrame.cpp (L126-151)
```cpp
bool COptionsPropertySheet::ShowSettings(const int initialPage, const bool refreshOnFilteringChange)
{
    auto sheet = std::make_unique<COptionsPropertySheet>();
    sheet->m_initialPage = initialPage; // -1 means restore last-used tab

    auto general = std::make_unique<CPageGeneral>();
    auto filtering = std::make_unique<CPageFiltering>(refreshOnFilteringChange);
    auto treelist = std::make_unique<CPageFileTree>();
    auto treemap = std::make_unique<CPageTreeMap>();
    auto permissions = std::make_unique<CPagePermissions>();
    auto cleanups = std::make_unique<CPageCleanups>();
    auto prompts = std::make_unique<CPagePrompts>();
    auto advanced = std::make_unique<CPageAdvanced>();

    sheet->AddPage(general.get());
    sheet->AddPage(filtering.get()); // index 1
    sheet->AddPage(treelist.get());
    sheet->AddPage(treemap.get());
    sheet->AddPage(permissions.get());
    sheet->AddPage(cleanups.get());
    sheet->AddPage(prompts.get());
    sheet->AddPage(advanced.get());

    sheet->DoModal();
    return sheet->m_restartApplication;
}
```
**File:** windirstat/Pages/PageCleanups.h (L26-43)
```text
class CPageCleanups final : public COptionsPage
{
    DECLARE_DYNAMIC(CPageCleanups)

    enum : std::uint8_t { IDD = IDD_PAGE_CLEANUPS };

    CPageCleanups();
    ~CPageCleanups() override = default;

protected:
    void DoDataExchange(CDataExchange* pDX) override;
    void InitializePage() override;
    void OnOK() override;

    void CurrentUdcToDialog();
    void DialogToCurrentUdc();
    void OnSomethingChanged();
    void UpdateControlStatus();
```
**File:** windirstat/windirstat.rc (L566-599)
```text
IDD_PAGE_ADVANCED DIALOGEX 0, 0, 400, 205
STYLE DS_SETFONT | WS_CHILD | WS_DISABLED | WS_CAPTION | WS_SYSMENU
CAPTION "IDS_PAGE_ADVANCED_TITLE"
FONT 8, "MS Shell Dlg 2", 400, 0, 0x1
BEGIN
    CONTROL         "IDS_PAGE_ADVANCED_USE_PRIVILEGES",IDC_BACKUP_RESTORE,
                    "Button",BS_AUTOCHECKBOX | WS_TABSTOP,7,7,373,12
    CONTROL         "IDS_PAGE_ADVANCED_SKIP_CLOUD_LINKS",IDC_SKIP_CLOUD_LINKS,
                    "Button",BS_AUTOCHECKBOX | WS_TABSTOP,7,22,373,12
    CONTROL         "IDS_PAGE_ADVANCED_PROCESS_HARDLINKS",IDC_PROCESS_HARDLINKS,
                    "Button",BS_AUTOCHECKBOX | WS_TABSTOP,7,37,373,12
    GROUPBOX        "IDS_SCANNING_EXCLUSIONS_DIRECTORY",IDC_STATIC,7,51,175,87
    CONTROL         "IDS_VOLUME_MOUNT_POINTS",IDC_EXCLUDE_VOLUME_MOUNT_POINTS,
                    "Button",BS_AUTOCHECKBOX | WS_TABSTOP,15,64,165,12
    CONTROL         "IDS_JUNCTIONS",IDC_EXCLUDE_JUNCTIONS,"Button",BS_AUTOCHECKBOX | WS_TABSTOP,15,78,164,12
    CONTROL         "IDS_SYMLINKS",IDC_EXCLUDE_SYMLINKS_DIRECTORY,"Button",BS_AUTOCHECKBOX | WS_TABSTOP,15,92,164,12
    CONTROL         "IDS_HIDDEN",IDC_EXCLUDE_HIDDEN_DIRECTORY,"Button",BS_AUTOCHECKBOX | WS_TABSTOP,15,106,164,12
    CONTROL         "IDS_PROTECTED",IDC_EXCLUDE_PROTECTED_DIRECTORY,"Button",BS_AUTOCHECKBOX | WS_TABSTOP,15,120,164,12
    GROUPBOX        "IDS_SCANNING_EXCLUSIONS_FILE",IDC_STATIC,191,51,175,87
    CONTROL         "IDS_SYMLINKS",IDC_EXCLUDE_SYMLINKS_FILE,"Button",BS_AUTOCHECKBOX | WS_TABSTOP,199,64,164,12
    CONTROL         "IDS_HIDDEN",IDC_EXCLUDE_HIDDEN_FILE,"Button",BS_AUTOCHECKBOX | WS_TABSTOP,199,78,164,12
    CONTROL         "IDS_PROTECTED",IDC_EXCLUDE_PROTECTED_FILE,"Button",BS_AUTOCHECKBOX | WS_TABSTOP,199,92,164,12
    LTEXT           "IDS_PAGE_ADVANCED_THREADS",IDC_STATIC,7,147,135,10
    LTEXT           "IDS_PAGE_ADVANCED_LARGEST_COUNT",IDC_STATIC,7,165,135,10
    LTEXT           "IDS_PAGE_ADVANCED_FOLDER_HISTORY",IDC_STATIC,7,183,135,10
    LTEXT           "IDS_PAGE_ADVANCED_HASH_ALGORITHM",IDC_STATIC,191,147,102,10
    COMBOBOX        IDC_COMBO_THREADS,151,146,30,52,CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP
    EDITTEXT        IDC_LARGEST_FILE_COUNT,151,163,30,12,ES_NUMBER
    EDITTEXT        IDC_FOLDER_HISTORY_COUNT,151,181,30,12,ES_NUMBER
    COMBOBOX        IDC_HASH_ALGORITHM,320,146,46,60,CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP
    LTEXT           "IDS_PAGE_ADVANCED_PROCESS_PRIORITY",IDC_STATIC,191,165,125,10
    COMBOBOX        IDC_PROCESS_PRIORITY,320,163,46,52,CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP
    PUSHBUTTON      "IDS_RESET_ALL_PREFERENCES",IDC_RESET_PREFERENCES,241,180,125,15
END
```
**File:** windirstat/Options.h (L163-230)
```text
public:
    inline static Setting<bool> AutomaticallyResizeColumns{ OptionsGeneral, L"AutomaticallyResizeColumns", true };
    inline static Setting<bool> ExcludeJunctions{ OptionsGeneral, L"ExcludeJunctions", true };
    inline static Setting<bool> ExcludeSymbolicLinksDirectory{ OptionsGeneral, L"ExcludeSymbolicLinksDirectory", true };
    inline static Setting<bool> ExcludeVolumeMountPoints{ OptionsGeneral, L"ExcludeVolumeMountPoints", true };
    inline static Setting<bool> ExcludeHiddenDirectory{ OptionsGeneral, L"ExcludeHiddenDirectory", false };
    inline static Setting<bool> ExcludeProtectedDirectory{ OptionsGeneral, L"ExcludeProtectedDirectory", false };
    inline static Setting<bool> ExcludeSymbolicLinksFile{ OptionsGeneral, L"ExcludeSymbolicLinksFile", true };
    inline static Setting<bool> ExcludeHiddenFile{ OptionsGeneral, L"ExcludeHiddenFile", false };
    inline static Setting<bool> ExcludeProtectedFile{ OptionsGeneral, L"ExcludeProtectedFile", false };
    inline static Setting<bool> FilteringUseRegex{ OptionsGeneral, L"FilteringUseRegex", false };
    inline static Setting<bool> FollowVolumeMountPoints{ OptionsGeneral, L"FollowVolumeMountPoints", false };
    inline static Setting<bool> UseSizeSuffixes{ OptionsGeneral, L"UseSizeSuffixes", true };
    inline static Setting<bool> ListFullRowSelection{ OptionsGeneral, L"ListFullRowSelection", true };
    inline static Setting<bool> ListGrid{ OptionsGeneral, L"ListGrid", false };
    inline static Setting<bool> ListStripes{ OptionsGeneral, L"ListStripes", false };
    inline static Setting<bool> PacmanAnimation{ OptionsGeneral, L"PacmanAnimation", true };
    inline static Setting<bool> ScanForDuplicates{ OptionsDupeTree, L"ScanForDuplicates", false };
    inline static Setting<bool> SearchWholePhrase{ OptionsSearch, L"SearchWholePhrase", false };
    inline static Setting<bool> SearchRegex{ OptionsSearch, L"SearchRegex", false };
    inline static Setting<bool> SearchCase{ OptionsSearch, L"SearchCase", false };
    inline static Setting<int> SearchMaxResults{ OptionsSearch, L"SearchMaxResults", 10000, 1, 1000000 };
    inline static Setting<bool> ShowDeletePermanentlyWarning{ OptionsGeneral, L"ShowDeletePermanentlyWarning", true };
    inline static Setting<bool> ShowDeleteToRecycleBinWarning{ OptionsGeneral, L"ShowDeleteToRecycleBinWarning", true };
    inline static Setting<bool> ShowElevationPrompt{ OptionsGeneral, L"ShowElevationPrompt", true };
    inline static Setting<bool> ShowEmptyRecycleBinPrompt{ OptionsGeneral, L"ShowEmptyRecycleBinPrompt", true };
    inline static Setting<bool> ShowCreateHardlinkPrompt{ OptionsGeneral, L"ShowCreateHardlinkPrompt", true };
    inline static Setting<bool> ShowRemoveMotwPrompt{ OptionsGeneral, L"ShowRemoveMotwPrompt", true };
    inline static Setting<bool> ShowDisableHibernatePrompt{ OptionsGeneral, L"ShowDisableHibernatePrompt", true };
    inline static Setting<bool> ShowRemoveShadowCopiesPrompt{ OptionsGeneral, L"ShowRemoveShadowCopiesPrompt", true };
    inline static Setting<bool> ShowDismCleanupPrompt{ OptionsGeneral, L"ShowDismCleanupPrompt", true };
    inline static Setting<bool> ShowDismResetPrompt{ OptionsGeneral, L"ShowDismResetPrompt", true };
    inline static Setting<bool> ShowSetDatesPrompt{ OptionsGeneral, L"ShowSetDatesPrompt", true };
    inline static Setting<bool> ShowRemoveEmptyFoldersPrompt{ OptionsGeneral, L"ShowRemoveEmptyFoldersPrompt", true };
    inline static Setting<bool> ShowMicrosoftProgress{ OptionsGeneral, L"ShowMicrosoftProgress", false };
    inline static Setting<bool> ShowFileTypes{ OptionsGeneral, L"ShowFileTypes", true };
    inline static Setting<bool> GroupUnregisteredTypes{ OptionsGeneral, L"GroupUnregisteredTypes", false };
    inline static Setting<bool> ShowFreeSpace{ OptionsGeneral, L"ShowFreeSpace", false };
    inline static Setting<bool> ShowStatusBar{ OptionsGeneral, L"ShowStatusBar", true };
    inline static Setting<bool> ShowTimeSpent{ OptionsFileTree, L"ShowTimeSpent", true };
    inline static Setting<bool> ShowToolBar{ OptionsGeneral, L"ShowToolBar", true };
    inline static Setting<bool> LargeToolBar{ OptionsGeneral, L"LargeToolBar", false };
    inline static Setting<bool> ShowVisualization{ OptionsTreeMap, L"ShowVisualization", true };
    inline static Setting<bool> ShowUnknown{ OptionsGeneral, L"ShowUnknown", false };
    inline static Setting<bool> SkipDupeDetectionCloudLinks{ OptionsGeneral, L"SkipDupeDetectionCloudLinks", true };
    inline static Setting<bool> ShowDupeDetectionCloudLinksWarning{ OptionsGeneral, L"ShowDupeDetectionCloudLinksWarning", true };
    inline static Setting<bool> AutoElevate{ OptionsGeneral, L"AutoElevate", false };
    inline static Setting<bool> AutoMapDrivesWhenElevated{ OptionsGeneral, L"AutoMapDrivesWhenElevated", true };
    inline static Setting<bool> TreeMapGrid{ OptionsTreeMap, L"TreeMapGrid", (CTreeMap::GetDefaults().grid) };
    inline static Setting<bool> TreeMapShowExtensions{ OptionsTreeMap, L"TreeMapShowExtensions", (CTreeMap::GetDefaults().showExtensions) };
    inline static Setting<bool> TreeMapShowFolderFrames{ OptionsTreeMap, L"TreeMapShowFolderFrames", (CTreeMap::GetDefaults().showFolderFrames) };
    inline static Setting<bool> TreeMapUseLogical{ OptionsTreeMap, L"TreeMapUseLogicalSize", false };
    inline static Setting<bool> UseAbsolutePercentages{ OptionsFileTree, L"UseAbsolutePercentages", true };
    inline static Setting<bool> UseBackupRestore{ OptionsGeneral, L"UseBackupRestore", true };
    inline static Setting<bool> UseDrawTextCache{ OptionsGeneral, L"UseDrawTextCache", true };
    inline static Setting<bool> UseFastScanEngine{ OptionsGeneral, L"UseFastScanEngine", true };
    inline static Setting<bool> UseWindowsLocaleSetting{ OptionsGeneral, L"UseWindowsLocaleSetting", true };
    inline static Setting<bool> ProcessHardlinks{ OptionsGeneral, L"ProcessHardlinks", true };
    inline static Setting<COLORREF> FileTreeColors[TREELISTCOLORCOUNT] =
    {
        { OptionsFileTree, L"FileTreeColor0", RGB(64, 64, 140) },
        { OptionsFileTree, L"FileTreeColor1", RGB(140, 64, 64) },
        { OptionsFileTree, L"FileTreeColor2", RGB(64, 140, 64) },
        { OptionsFileTree, L"FileTreeColor3", RGB(140, 140, 64) },
        { OptionsFileTree, L"FileTreeColor4", RGB(0, 0, 255) },
        { OptionsFileTree, L"FileTreeColor5", RGB(255, 0, 0) },
        { OptionsFileTree, L"FileTreeColor6", RGB(0, 255, 0) },
        { OptionsFileTree, L"FileTreeColor7", RGB(255, 255, 0) }
```
**File:** windirstat/Property.cpp (L40-118)
```cpp
template <> void Setting<int>::ReadPersistedProperty()
{
    const int def = m_value;
    m_value = CDirStatApp::Get()->GetProfileInt(m_section.c_str(), m_entry.c_str(), m_value);
    if (m_value != def && m_min != m_max) m_value = std::clamp(m_value, m_min, m_max);
}

template <> void Setting<int>::WritePersistedProperty()
{
    CDirStatApp::Get()->WriteProfileInt(m_section.c_str(), m_entry.c_str(), m_value);
}

// Setting<bool> Processing

template <> void Setting<bool>::ReadPersistedProperty()
{
    m_value = CDirStatApp::Get()->GetProfileInt(m_section.c_str(), m_entry.c_str(), m_value == 0 ? 0 : 1) != 0;
}

template <> void Setting<bool>::WritePersistedProperty()
{
    CDirStatApp::Get()->WriteProfileInt(m_section.c_str(), m_entry.c_str(), m_value == 0 ? 0 : 1);
}

// Setting<std::wstring> Processing

template <> void Setting<std::wstring>::ReadPersistedProperty()
{
    m_value = CDirStatApp::Get()->GetProfileString(m_section.c_str(), m_entry.c_str(), m_value.c_str());
    static const std::wregex reRead(LR"(\x1e)");
    m_value = std::regex_replace(m_value, reRead, L"\r\n");
}

template <> void Setting<std::wstring>::WritePersistedProperty()
{
    static const std::wregex reWrite(LR"((\r|\n)+)");
    const std::wstring valueCleaned = std::regex_replace(m_value, reWrite, L"\x1e");
    CDirStatApp::Get()->WriteProfileString(m_section.c_str(), m_entry.c_str(), valueCleaned.c_str());
}

// Setting<WINDOWPLACEMENT> Processing

template <> void Setting<WINDOWPLACEMENT>::ReadPersistedProperty()
{
    if (ReadBinaryProperty(m_section, m_entry, &m_value, sizeof(WINDOWPLACEMENT)))
    {
        // Scale rcNormalPosition from stored 96 DPI values to current DPI
        m_value.rcNormalPosition.left = DpiRest(m_value.rcNormalPosition.left);
        m_value.rcNormalPosition.top = DpiRest(m_value.rcNormalPosition.top);
        m_value.rcNormalPosition.right = DpiRest(m_value.rcNormalPosition.right);
        m_value.rcNormalPosition.bottom = DpiRest(m_value.rcNormalPosition.bottom);
    }
}

template <> void Setting<WINDOWPLACEMENT>::WritePersistedProperty()
{
    // Scale rcNormalPosition from current DPI to 96 DPI for storage
    WINDOWPLACEMENT normalizedWp = m_value;
    normalizedWp.rcNormalPosition.left = DpiSave(m_value.rcNormalPosition.left);
    normalizedWp.rcNormalPosition.top = DpiSave(m_value.rcNormalPosition.top);
    normalizedWp.rcNormalPosition.right = DpiSave(m_value.rcNormalPosition.right);
    normalizedWp.rcNormalPosition.bottom = DpiSave(m_value.rcNormalPosition.bottom);
    CDirStatApp::Get()->WriteProfileBinary(m_section.c_str(), m_entry.c_str(),
        reinterpret_cast<LPBYTE>(&normalizedWp), sizeof(WINDOWPLACEMENT));
}

// Setting<std::vector<std::wstring>> Processing

template <> void Setting<std::vector<std::wstring>>::ReadPersistedProperty()
{
    const std::wstring s = CDirStatApp::Get()->GetProfileString(m_section.c_str(), m_entry.c_str()).GetString();
    m_value = SplitString(s);
}

template <> void Setting<std::vector<std::wstring>>::WritePersistedProperty()
{
    const std::wstring result = JoinString(m_value);
    CDirStatApp::Get()->WriteProfileString(m_section.c_str(), m_entry.c_str(), result.c_str());
}
```
**File:** windirstat/WinDirStat.cpp (L231-329)
```cpp
class CWinDirStatCommandLineInfo final : public CCommandLineInfo
{
    std::wstring m_pendingFlag;
    std::wstring m_operationFlag;
    bool m_hasParsedParam = false;
    bool m_hasPathParam = false;
    bool m_malformedFlag = false;
    bool m_invalidPath = false;
    const std::wstring saveToFlag = L"saveto";
    const std::wstring saveDupesToFlag = L"savedupesto";
    const std::wstring savePermsToFlag = L"savepermsto";
    const std::wstring loadFromFlag = L"loadfrom";
    const std::wstring legacyUninstallFlag = L"legacyuninstall";

public:

    bool HasMalformedCommandLine() const noexcept
    {
        return m_malformedFlag || !m_pendingFlag.empty() ||
            (m_operationFlag == loadFromFlag && m_hasPathParam);
    }
    bool HasInvalidPath() const noexcept { return m_invalidPath; }
    bool IsLegacyUninstallRequested() const noexcept { return m_operationFlag == legacyUninstallFlag; }

    void ParseParam(const WCHAR* pszParam, BOOL bFlag, BOOL bLast) override
    {
        const bool hadPriorParam = m_hasParsedParam;
        m_hasParsedParam = true;

        // Normalize string for parsing
        std::wstring param{ pszParam };
        TrimString(param, wds::chrDoubleQuote);
        TrimString(param, wds::chrBackslash, true);

        // If we have a pending flag, this non-flag param is its value
        if (!m_pendingFlag.empty() && !bFlag)
        {
            if (param.empty())
            {
                m_malformedFlag = true;
            }
            else if (m_pendingFlag == saveToFlag)
            {
                CDirStatApp::Get()->m_saveToPath = param;
                COptions::ScanForDuplicates = false;
            }
            else if (m_pendingFlag == saveDupesToFlag)
            {
                CDirStatApp::Get()->m_saveDupesToPath = param;
                COptions::ScanForDuplicates = true;
            }
            else if (m_pendingFlag == savePermsToFlag)
            {
                CDirStatApp::Get()->m_savePermsToPath = param;
            }
            else if (m_pendingFlag == loadFromFlag)
            {
                CDirStatApp::Get()->m_loadFromPath = param;
            }

            m_pendingFlag.clear();
            return;
        }

        // Handle any non-flags as paths
        if (!bFlag)
        {
            m_hasPathParam = true;
            if (param.empty())
            {
                m_invalidPath = true;
                m_malformedFlag = true;
                return;
            }
            for (const auto& paramSpilt : SplitString(param))
            {
                if (paramSpilt.empty())
                {
                    m_invalidPath = true;
                    continue;
                }
                std::error_code ec;
                const std::wstring fullPath = std::filesystem::absolute(paramSpilt + L"\\", ec).wstring();
                if (!ec && FolderExists(fullPath))
                {
                    if (!m_strFileName.IsEmpty()) m_strFileName += wds::chrPipe;
                    m_strFileName += fullPath.c_str();
                }
                else
                {
                    m_invalidPath = true;
                }
            }
            return;
        }

        // Handle flags
        // Value-taking flags require an immediate non-flag value.
        if (!m_pendingFlag.empty())
```
