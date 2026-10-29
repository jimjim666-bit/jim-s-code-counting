#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cctype>
using namespace std;

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// 开启 Windows 通用控件 6.0 样式
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// 本程序是 Windows 图形界面程序，入口为 WinMain
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

// ---------- 各语言统计结果 ----------
struct LangStat {
    int files = 0;
    int blank = 0;
    int comment = 0;
    int code = 0;
};

// 支持统计的语言
enum Lang { LANG_C = 0, LANG_CPP, LANG_JAVA, LANG_PY, LANG_COUNT };
static const wchar_t* LANG_NAME[LANG_COUNT] = { L"C", L"C++", L"Java", L"Python" };

// ---------- C / C++ / Java 行统计 ----------
// 状态: 0=普通代码, 1=块注释内, 2=双引号字符串, 3=行注释内(//), 4=单引号字符字面量
static void CountCStyle(const string& text, LangStat& st)
{
    int state = 0;
    size_t i = 0;
    bool lineHasCode = false, lineHasComment = false, lineIsBlank = true;

    auto flushLine = [&]() {
        if (lineIsBlank) st.blank++;
        else if (lineHasComment && !lineHasCode) st.comment++;
        else st.code++;
        lineHasCode = lineHasComment = false;
        lineIsBlank = true;
        if (state == 3) state = 0; // 行注释随换行结束；其余状态跨换行保留
    };

    while (i < text.size()) {
        char c = text[i];
        if (c == '\n') { flushLine(); i++; continue; }
        if (c == '\r') { i++; continue; }

        if (!isspace((unsigned char)c)) lineIsBlank = false;

        if (state == 3) { // 行注释内
            lineHasComment = true;
            i++; continue;
        }
        if (state == 1) { // 块注释内
            lineHasComment = true;
            if (c == '*' && i + 1 < text.size() && text[i + 1] == '/') { state = 0; i += 2; continue; }
            i++; continue;
        }
        if (state == 2) { // 双引号字符串内
            lineHasCode = true;
            if (c == '\\' && i + 1 < text.size()) { i += 2; continue; }
            if (c == '"') state = 0;
            i++; continue;
        }
        if (state == 4) { // 单引号字符字面量内
            lineHasCode = true;
            if (c == '\\' && i + 1 < text.size()) { i += 2; continue; }
            if (c == '\'') state = 0;
            i++; continue;
        }

        // state == 0
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            lineHasComment = true; state = 3; i += 2; continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            lineHasComment = true; state = 1; i += 2; continue;
        }
        if (c == '"') { state = 2; lineHasCode = true; i++; continue; }
        if (c == '\'') { state = 4; lineHasCode = true; i++; continue; }
        if (!isspace((unsigned char)c)) lineHasCode = true;
        i++;
    }
    flushLine();
}

// ---------- Python 行统计 ----------
static void CountPython(const string& text, LangStat& st)
{
    // # 为注释；三引号字符串按文档注释处理
    int triple = 0; // 0=普通, 1=单三引号, 2=双三引号
    int lineCmt = 0; // 1=当前行处于 # 注释中
    size_t i = 0;
    bool lineHasCode = false, lineHasComment = false, lineIsBlank = true;

    auto flushLine = [&]() {
        if (lineIsBlank) st.blank++;
        else if (lineHasComment && !lineHasCode) st.comment++;
        else st.code++;
        lineHasCode = lineHasComment = false; lineIsBlank = true;
        lineCmt = 0; // # 注释随换行结束；三引号跨换行保留
    };

    while (i < text.size()) {
        char c = text[i];
        if (c == '\n') { flushLine(); i++; continue; }
        if (c == '\r') { i++; continue; }
        if (!isspace((unsigned char)c)) lineIsBlank = false;

        if (lineCmt) {
            lineHasComment = true;
            i++; continue;
        }

        if (triple) {
            lineHasComment = true;
            char q = (triple == 1) ? '\'' : '"';
            if (c == q && i + 2 < text.size() && text[i + 1] == q && text[i + 2] == q) { triple = 0; i += 3; continue; }
            i++; continue;
        }

        if (c == '#') { lineHasComment = true; lineCmt = 1; i++; continue; }
        if (c == '\'' && i + 2 < text.size() && text[i + 1] == '\'' && text[i + 2] == '\'') {
            lineHasComment = true; triple = 1; i += 3; continue;
        }
        if (c == '"' && i + 2 < text.size() && text[i + 1] == '"' && text[i + 2] == '"') {
            lineHasComment = true; triple = 2; i += 3; continue;
        }
        if (!isspace((unsigned char)c)) lineHasCode = true;
        i++;
    }
    flushLine();
}

// ---------- 按扩展名判断语言 ----------
static Lang DetectLang(const wstring& ext)
{
    if (ext == L".c" || ext == L".h") return LANG_C;
    if (ext == L".cpp" || ext == L".hpp" || ext == L".cxx" || ext == L".cc" || ext == L".hh") return LANG_CPP;
    if (ext == L".java") return LANG_JAVA;
    if (ext == L".py") return LANG_PY;
    return LANG_COUNT;
}

// ---------- 递归遍历目录 ----------
static void ScanDir(const wstring& dir, LangStat stats[LANG_COUNT])
{
    vector<wstring> subdirs;
    wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        wstring full = dir + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            subdirs.push_back(full);
        }
        else {
            size_t dot = name.find_last_of(L'.');
            if (dot == wstring::npos) continue;
            wstring ext = name.substr(dot);
            for (auto& c : ext) c = towlower(c);
            Lang lang = DetectLang(ext);
            if (lang == LANG_COUNT) continue;

            ifstream f(full, ios::binary);
            if (!f) continue;
            ostringstream ss; ss << f.rdbuf();
            string content = ss.str();
            // 去掉 UTF-8 BOM
            if (content.size() >= 3 && (unsigned char)content[0] == 0xEF
                && (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF)
                content.erase(0, 3);

            stats[lang].files++;
            if (lang == LANG_PY) CountPython(content, stats[lang]);
            else CountCStyle(content, stats[lang]);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    for (auto& d : subdirs) ScanDir(d, stats);
}

// ---------- 全局窗口控件 ----------
static HWND g_hWnd;
static HWND g_hEdit, g_hList, g_hBtnScan, g_hBrowse, g_hStatus;
static HFONT g_hFont;

static void DoScan()
{
    wchar_t path[MAX_PATH];
    GetWindowTextW(g_hEdit, path, MAX_PATH);
    wstring dir = path;
    if (dir.empty()) {
        wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
        dir = cwd;
        SetWindowTextW(g_hEdit, dir.c_str());
    }

    LangStat stats[LANG_COUNT] = {};
    ScanDir(dir, stats);

    // 刷新 ListView
    ListView_DeleteAllItems(g_hList);
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    LangStat total = {};
    for (int i = 0; i < LANG_COUNT; i++) {
        wchar_t buf[64];
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = (LPWSTR)LANG_NAME[i];
        ListView_InsertItem(g_hList, &item);
        wsprintfW(buf, L"%d", stats[i].files);
        ListView_SetItemText(g_hList, i, 1, buf);
        wsprintfW(buf, L"%d", stats[i].blank);
        ListView_SetItemText(g_hList, i, 2, buf);
        wsprintfW(buf, L"%d", stats[i].comment);
        ListView_SetItemText(g_hList, i, 3, buf);
        wsprintfW(buf, L"%d", stats[i].code);
        ListView_SetItemText(g_hList, i, 4, buf);

        total.files += stats[i].files;
        total.blank += stats[i].blank;
        total.comment += stats[i].comment;
        total.code += stats[i].code;
    }
    // 合计行
    int row = LANG_COUNT;
    wchar_t buf[64];
    item.iItem = row;
    item.pszText = (LPWSTR)L"TOTAL";
    ListView_InsertItem(g_hList, &item);
    wsprintfW(buf, L"%d", total.files);   ListView_SetItemText(g_hList, row, 1, buf);
    wsprintfW(buf, L"%d", total.blank);    ListView_SetItemText(g_hList, row, 2, buf);
    wsprintfW(buf, L"%d", total.comment); ListView_SetItemText(g_hList, row, 3, buf);
    wsprintfW(buf, L"%d", total.code);    ListView_SetItemText(g_hList, row, 4, buf);

    wsprintfW(buf, L"扫描完成: %s  |  %d 个文件, %d 行代码",
        dir.c_str(), total.files, total.code);
    SetWindowTextW(g_hStatus, buf);
}

static void BrowseFolder()
{
    IFileOpenDialog* pfd = NULL;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr)) {
        DWORD flags;
        pfd->GetOptions(&flags);
        pfd->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        pfd->SetTitle(L"选择要统计的目录");
        hr = pfd->Show(g_hWnd);
        if (SUCCEEDED(hr)) {
            IShellItem* psi = NULL;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR path = NULL;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    SetWindowTextW(g_hEdit, path);
                    CoTaskMemFree(path);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        g_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            10, 10, 500, 24, hWnd, (HMENU)1001, ((LPCREATESTRUCTW)lParam)->hInstance, NULL);

        g_hBrowse = CreateWindowExW(0, L"BUTTON", L"浏览...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            520, 8, 90, 28, hWnd, (HMENU)1002,
            ((LPCREATESTRUCTW)lParam)->hInstance, NULL);

        g_hBtnScan = CreateWindowExW(0, L"BUTTON", L"开始统计",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            620, 8, 90, 28, hWnd, (HMENU)1003,
            ((LPCREATESTRUCTW)lParam)->hInstance, NULL);

        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            10, 50, 700, 380, hWnd, (HMENU)1004,
            ((LPCREATESTRUCTW)lParam)->hInstance, NULL);

        // 设置 ListView 样式
        ListView_SetExtendedListViewStyle(g_hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        // 添加列
        LVCOLUMNW col = {};
        col.mask = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH;
        col.fmt = LVCFMT_LEFT;
        col.cx = 120;
        col.pszText = (LPWSTR)L"Language";
        ListView_InsertColumn(g_hList, 0, &col);
        col.cx = 90;
        col.pszText = (LPWSTR)L"Files";
        ListView_InsertColumn(g_hList, 1, &col);
        col.pszText = (LPWSTR)L"Blank";
        ListView_InsertColumn(g_hList, 2, &col);
        col.pszText = (LPWSTR)L"Comment";
        ListView_InsertColumn(g_hList, 3, &col);
        col.pszText = (LPWSTR)L"Code";
        ListView_InsertColumn(g_hList, 4, &col);

        g_hStatus = CreateWindowExW(0, L"STATIC", L"输入目录后点「开始统计」, 留空则统计当前目录",
            WS_CHILD | WS_VISIBLE,
            10, 445, 700, 20, hWnd, (HMENU)1005,
            ((LPCREATESTRUCTW)lParam)->hInstance, NULL);

        // 初始填入当前目录
        wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
        SetWindowTextW(g_hEdit, cwd);

        // 统一字体: 微软雅黑 9pt
        g_hFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hBrowse, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hBtnScan, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        return 0;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(hWnd, &rc);
        int W = rc.right - rc.left, H = rc.bottom - rc.top;
        MoveWindow(g_hEdit, 10, 10, W - 220, 24, TRUE);
        MoveWindow(g_hBrowse, W - 200, 8, 90, 28, TRUE);
        MoveWindow(g_hBtnScan, W - 100, 8, 90, 28, TRUE);
        MoveWindow(g_hList, 10, 50, W - 20, H - 90, TRUE);
        MoveWindow(g_hStatus, 10, H - 30, W - 20, 20, TRUE);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == 1002) BrowseFolder();
        else if (id == 1003) DoScan();
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    InitCommonControls();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CodeStatClass";
    RegisterClassExW(&wc);

    int W = 740, H = 520;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - W) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - H) / 2;
    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"代码量统计",
        WS_OVERLAPPEDWINDOW, sx, sy, W, H,
        NULL, NULL, hInstance, NULL);

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
