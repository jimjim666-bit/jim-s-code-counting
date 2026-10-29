#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
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

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

enum Lang { LANG_C = 0, LANG_CPP, LANG_JAVA, LANG_PY, LANG_SIZE };
static const wchar_t* LANG_NAME[LANG_SIZE] = { L"C", L"C++", L"Java", L"Python" };

struct LangStat {
    int files = 0;
    int blank = 0;
    int comment = 0;
    int code = 0;
};

enum CtrlId { ID_EDIT = 1001, ID_BROWSE, ID_SCAN, ID_LIST, ID_STATUS };
enum Col { COL_LANG = 0, COL_FILES, COL_BLANK, COL_COMMENT, COL_CODE, COL_SIZE };

enum CS { CS_PLAIN = 0, CS_BLOCK, CS_DQ, CS_LINE, CS_SQ };
enum PyTri { PY_NONE = 0, PY_SINGLE, PY_DOUBLE };

static void AddTotal(LangStat& total, const LangStat& item)
{
    total.files += item.files;
    total.blank += item.blank;
    total.comment += item.comment;
    total.code += item.code;
}

static void CountCStyle(const string& text, LangStat& stat)
{
    CS state = CS_PLAIN;
    size_t i = 0;
    bool hasCode = false, hasComment = false, isBlank = true;

    auto finishLine = [&]() {
        if (isBlank) stat.blank++;
        else if (hasComment && !hasCode) stat.comment++;
        else stat.code++;
        hasCode = hasComment = false;
        isBlank = true;
        if (state == CS_LINE) state = CS_PLAIN;
    };

    while (i < text.size()) {
        char c = text[i];
        if (c == '\n') { finishLine(); i++; continue; }
        if (c == '\r') { i++; continue; }
        if (!isspace((unsigned char)c)) isBlank = false;

        if (state == CS_LINE) {
            hasComment = true;
            i++;
            continue;
        }
        if (state == CS_BLOCK) {
            hasComment = true;
            if (c == '*' && i + 1 < text.size() && text[i + 1] == '/') {
                state = CS_PLAIN;
                i += 2;
                continue;
            }
            i++;
            continue;
        }
        if (state == CS_DQ) {
            hasCode = true;
            if (c == '\\' && i + 1 < text.size()) { i += 2; continue; }
            if (c == '"') state = CS_PLAIN;
            i++;
            continue;
        }
        if (state == CS_SQ) {
            hasCode = true;
            if (c == '\\' && i + 1 < text.size()) { i += 2; continue; }
            if (c == '\'') state = CS_PLAIN;
            i++;
            continue;
        }

        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            hasComment = true;
            state = CS_LINE;
            i += 2;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            hasComment = true;
            state = CS_BLOCK;
            i += 2;
            continue;
        }
        if (c == '"') { state = CS_DQ; hasCode = true; i++; continue; }
        if (c == '\'') { state = CS_SQ; hasCode = true; i++; continue; }
        if (!isspace((unsigned char)c)) hasCode = true;
        i++;
    }
    finishLine();
}

static void CountPython(const string& text, LangStat& stat)
{
    PyTri triple = PY_NONE;
    bool inLineComment = false;
    size_t i = 0;
    bool hasCode = false, hasComment = false, isBlank = true;

    auto finishLine = [&]() {
        if (isBlank) stat.blank++;
        else if (hasComment && !hasCode) stat.comment++;
        else stat.code++;
        hasCode = hasComment = false;
        isBlank = true;
        inLineComment = false;
    };

    while (i < text.size()) {
        char c = text[i];
        if (c == '\n') { finishLine(); i++; continue; }
        if (c == '\r') { i++; continue; }
        if (!isspace((unsigned char)c)) isBlank = false;

        if (inLineComment) {
            hasComment = true;
            i++;
            continue;
        }

        if (triple != PY_NONE) {
            hasComment = true;
            char quote = triple == PY_SINGLE ? '\'' : '"';
            if (c == quote && i + 2 < text.size() && text[i + 1] == quote && text[i + 2] == quote) {
                triple = PY_NONE;
                i += 3;
                continue;
            }
            i++;
            continue;
        }

        if (c == '#') { hasComment = true; inLineComment = true; i++; continue; }
        if (c == '\'' && i + 2 < text.size() && text[i + 1] == '\'' && text[i + 2] == '\'') {
            hasComment = true;
            triple = PY_SINGLE;
            i += 3;
            continue;
        }
        if (c == '"' && i + 2 < text.size() && text[i + 1] == '"' && text[i + 2] == '"') {
            hasComment = true;
            triple = PY_DOUBLE;
            i += 3;
            continue;
        }
        if (!isspace((unsigned char)c)) hasCode = true;
        i++;
    }
    finishLine();
}

static Lang DetectLang(const wstring& ext)
{
    if (ext == L".c" || ext == L".h") return LANG_C;
    if (ext == L".cpp" || ext == L".hpp" || ext == L".cxx" || ext == L".cc" || ext == L".hh") return LANG_CPP;
    if (ext == L".java") return LANG_JAVA;
    if (ext == L".py") return LANG_PY;
    return LANG_SIZE;
}

static bool ReadText(const wstring& path, string& content)
{
    ifstream file(path, ios::binary);
    if (!file) return false;

    ostringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();

    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF
        && (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF)
        content.erase(0, 3);
    return true;
}

static void ScanDir(const wstring& dir, LangStat stats[LANG_SIZE])
{
    vector<wstring> folders;
    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) return;

    do {
        wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        wstring full = dir + L"\\" + name;

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            folders.push_back(full);
            continue;
        }

        size_t dot = name.find_last_of(L'.');
        if (dot == wstring::npos) continue;
        wstring ext = name.substr(dot);
        for (auto& ch : ext) ch = towlower(ch);

        Lang lang = DetectLang(ext);
        if (lang == LANG_SIZE) continue;

        string content;
        if (!ReadText(full, content)) continue;

        stats[lang].files++;
        if (lang == LANG_PY) CountPython(content, stats[lang]);
        else CountCStyle(content, stats[lang]);
    } while (FindNextFileW(handle, &data));
    FindClose(handle);

    for (const auto& folder : folders) ScanDir(folder, stats);
}

static HWND g_main, g_edit, g_browse, g_scan, g_list, g_status;
static HFONT g_font;

static void SetNumber(int row, int col, int value)
{
    wchar_t text[64];
    wsprintfW(text, L"%d", value);
    ListView_SetItemText(g_list, row, col, text);
}

static void FillRow(int row, const wchar_t* name, const LangStat& stat)
{
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = COL_LANG;
    item.pszText = (LPWSTR)name;
    ListView_InsertItem(g_list, &item);

    SetNumber(row, COL_FILES, stat.files);
    SetNumber(row, COL_BLANK, stat.blank);
    SetNumber(row, COL_COMMENT, stat.comment);
    SetNumber(row, COL_CODE, stat.code);
}

static void DoScan()
{
    wchar_t path[MAX_PATH] = { 0 };
    GetWindowTextW(g_edit, path, MAX_PATH);
    wstring dir = path;

    if (dir.empty()) {
        wchar_t cwd[MAX_PATH] = { 0 };
        GetCurrentDirectoryW(MAX_PATH, cwd);
        dir = cwd;
        SetWindowTextW(g_edit, dir.c_str());
    }

    LangStat stats[LANG_SIZE] = {};
    ScanDir(dir, stats);

    LangStat total = {};
    ListView_DeleteAllItems(g_list);
    for (int i = 0; i < LANG_SIZE; i++) {
        FillRow(i, LANG_NAME[i], stats[i]);
        AddTotal(total, stats[i]);
    }
    FillRow(LANG_SIZE, L"TOTAL", total);

    wchar_t status[256];
    wsprintfW(status, L"扫描完成: %s  |  %d 个文件, %d 行代码", dir.c_str(), total.files, total.code);
    SetWindowTextW(g_status, status);
}

static void BrowseFolder()
{
    IFileOpenDialog* dialog = NULL;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) return;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"选择要统计的目录");

    if (SUCCEEDED(dialog->Show(g_main))) {
        IShellItem* item = NULL;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = NULL;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                SetWindowTextW(g_edit, path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
}

static void CreateControls(HWND parent, HINSTANCE instance)
{
    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        10, 10, 500, 24, parent, (HMENU)ID_EDIT, instance, NULL);

    g_browse = CreateWindowExW(0, L"BUTTON", L"浏览...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        520, 8, 90, 28, parent, (HMENU)ID_BROWSE, instance, NULL);

    g_scan = CreateWindowExW(0, L"BUTTON", L"开始统计",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        620, 8, 90, 28, parent, (HMENU)ID_SCAN, instance, NULL);

    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        10, 50, 700, 380, parent, (HMENU)ID_LIST, instance, NULL);

    g_status = CreateWindowExW(0, L"STATIC", L"输入目录后点「开始统计」, 留空则统计当前目录",
        WS_CHILD | WS_VISIBLE,
        10, 445, 700, 20, parent, (HMENU)ID_STATUS, instance, NULL);

    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    const wchar_t* titles[COL_SIZE] = { L"Language", L"Files", L"Blank", L"Comment", L"Code" };
    int widths[COL_SIZE] = { 120, 90, 90, 100, 90 };

    LVCOLUMNW col = {};
    col.mask = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH;
    col.fmt = LVCFMT_LEFT;
    for (int c = 0; c < COL_SIZE; c++) {
        col.cx = widths[c];
        col.pszText = (LPWSTR)titles[c];
        ListView_InsertColumn(g_list, c, &col);
    }

    wchar_t cwd[MAX_PATH] = { 0 };
    GetCurrentDirectoryW(MAX_PATH, cwd);
    SetWindowTextW(g_edit, cwd);

    g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

    HWND controls[5] = { g_edit, g_browse, g_scan, g_list, g_status };
    for (HWND window : controls) SendMessageW(window, WM_SETFONT, (WPARAM)g_font, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        CreateControls(hwnd, ((LPCREATESTRUCTW)lParam)->hInstance);
        return 0;

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        MoveWindow(g_edit, 10, 10, width - 220, 24, TRUE);
        MoveWindow(g_browse, width - 200, 8, 90, 28, TRUE);
        MoveWindow(g_scan, width - 100, 8, 90, 28, TRUE);
        MoveWindow(g_list, 10, 50, width - 20, height - 90, TRUE);
        MoveWindow(g_status, 10, height - 30, width - 20, 20, TRUE);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BROWSE: BrowseFolder(); break;
        case ID_SCAN: DoScan(); break;
        }
        return 0;

    case WM_DESTROY:
        DeleteObject(g_font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
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

    const int windowWidth = 740, windowHeight = 520;
    int x = (GetSystemMetrics(SM_CXSCREEN) - windowWidth) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2;

    g_main = CreateWindowExW(0, wc.lpszClassName, L"代码量统计",
        WS_OVERLAPPEDWINDOW, x, y, windowWidth, windowHeight,
        NULL, NULL, hInstance, NULL);

    ShowWindow(g_main, nCmdShow);
    UpdateWindow(g_main);

    MSG message;
    while (GetMessage(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    CoUninitialize();
    return (int)message.wParam;
}
