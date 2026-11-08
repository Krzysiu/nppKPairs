// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <fstream>
#include <sstream>
#include <map>
#include <numeric>
#include <memory>
#include "PluginInterface.h"
#include "Docking.h"
#include "model.h"
#include "localization.h"
#include "resource.h"
#include "app_meta.h"
namespace {
HINSTANCE instance;
NppData npp;
HWND panel = nullptr, scratch = nullptr, settingsWindow = nullptr, groupsWindow = nullptr;
Library library;
Locale locale;
std::filesystem::path configFile, moduleDir;
bool loaded = false, loadFailed = false, shuttingDown = false;
FuncItem commands[6];
std::vector<HWND> modeless;
HFONT boldFont = nullptr;
HIMAGELIST rowImages = nullptr;
int hoverRow = -1;
bool heartPressed = false;
constexpr int regexFlags = SCFIND_REGEXP | SCFIND_POSIX | 0x20000000 | 0x08000000;
struct Row {
    int index;
    std::wstring title;
    bool line = false;
};
std::vector<Row> rows;
LRESULT sci(HWND h, UINT m, WPARAM w = 0, LPARAM l = 0) { return SendMessageW(h, m, w, l); }
std::wstring tr(const std::wstring &s) { return locale.tr(s); }
std::wstring textOf(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
std::wstring field(HWND h, int id) { return textOf(GetDlgItem(h, id)); }
void field(HWND h, int id, const std::string &s) { SetDlgItemTextW(h, id, wide(s).c_str()); }
bool checked(HWND h, int id) { return IsDlgButtonChecked(h, id) == BST_CHECKED; }
void check(HWND h, int id, bool b) { CheckDlgButton(h, id, b ? BST_CHECKED : BST_UNCHECKED); }
int choice(HWND h, int id) { return (int)SendDlgItemMessageW(h, id, CB_GETCURSEL, 0, 0); }
void combo(HWND h, int id, const std::vector<std::wstring> &values, int selected = 0, bool translate = true) {
    auto c = GetDlgItem(h, id);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (auto &v : values) {
        auto s = translate ? tr(v) : v;
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)s.c_str());
    }
    SendMessageW(c, CB_SETCURSEL, selected, 0);
}
void error(const std::exception &e, HWND owner = nullptr) {
    MessageBoxW(owner ? owner : npp._nppHandle, tr(wide(e.what())).c_str(), L"KPairs", MB_OK | MB_ICONERROR);
}
void theme(HWND h) { SendMessageW(npp._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, 0xB, (LPARAM)h); }
BOOL CALLBACK translateChild(HWND child, LPARAM) {
    wchar_t cls[32]{};
    GetClassNameW(child, cls, 32);
    if (_wcsicmp(cls, L"Button") == 0 || _wcsicmp(cls, L"Static") == 0)
        SetWindowTextW(child, tr(textOf(child)).c_str());
    return TRUE;
}
void translateDialog(HWND h) {
    SetWindowTextW(h, tr(textOf(h)).c_str());
    EnumChildWindows(h, translateChild, 0);
}
void center(HWND h) {
    RECT r, o;
    GetWindowRect(h, &r);
    GetWindowRect(npp._nppHandle, &o);
    SetWindowPos(h, nullptr, o.left + ((o.right - o.left) - (r.right - r.left)) / 2,
                 o.top + ((o.bottom - o.top) - (r.bottom - r.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}
void registerDialog(HWND h) {
    modeless.push_back(h);
    SendMessageW(npp._nppHandle, NPPM_MODELESSDIALOG, MODELESSDIALOGADD, (LPARAM)h);
}
void unregisterDialog(HWND h) {
    modeless.erase(std::remove(modeless.begin(), modeless.end(), h), modeless.end());
    if (!shuttingDown)
        SendMessageW(npp._nppHandle, NPPM_MODELESSDIALOG, MODELESSDIALOGREMOVE, (LPARAM)h);
}
HWND openModeless(int id, DLGPROC proc, void *context = nullptr) {
    HWND h = CreateDialogParamW(instance, MAKEINTRESOURCEW(id), npp._nppHandle, proc, (LPARAM)context);
    if (!h)
        throw std::runtime_error("Cannot create dialog.");
    registerDialog(h);
    ShowWindow(h, SW_SHOW);
    return h;
}
HWND editor() {
    int i = 0;
    SendMessageW(npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&i);
    return i ? npp._scintillaSecondHandle : npp._scintillaMainHandle;
}
std::wstring currentPath() {
    std::wstring p(32768, 0);
    SendMessageW(npp._nppHandle, NPPM_GETFULLCURRENTPATH, p.size(), (LPARAM)p.data());
    p.resize(wcslen(p.c_str()));
    return p;
}
#include "engine.h"
void refresh();
void action(int);
void show();
void settings();
void groups();
void statistics();
void guide();
void about();
void editPair(int, bool = false);
void editGroup(int);
void run(bool);
void load() {
    if (loaded)
        return;
    wchar_t mod[32768]{};
    GetModuleFileNameW(instance, mod, 32768);
    moduleDir = std::filesystem::path(mod).parent_path();
    std::wstring p(32768, 0);
    SendMessageW(npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, p.size(), (LPARAM)p.data());
    p.resize(wcslen(p.c_str()));
    if (p.empty())
        throw std::runtime_error("Cannot locate plugin configuration directory.");
    configFile = std::filesystem::path(p) / L"KPairs.json";
    std::filesystem::create_directories(configFile.parent_path());
    try {
        if (std::filesystem::exists(configFile))
            library = readFile(configFile);
        else {
            auto legacy = configFile.parent_path() / L"RegexLibrary.json";
            if (std::filesystem::exists(legacy))
                library = readFile(legacy);
        }
    } catch (...) {
        loadFailed = true;
        loaded = true;
        throw;
    }
    locale.scan(moduleDir / L"locale");
    locale.load(library.language);
    loaded = true;
}
void commit(Library next) {
    if (loadFailed)
        throw std::runtime_error(
            "Cannot read the original library. Fix or rename KPairs.json, then restart Notepad++.");
    if (next.pairs.size() > 10000 || next.groups.size() > 1000)
        throw std::runtime_error("Library limit exceeded.");
    saveFile(configFile, next);
    library = std::move(next);
}
int selected() {
    if (!panel)
        return -1;
    int row = ListView_GetNextItem(GetDlgItem(panel, P_LIST), -1, LVNI_SELECTED);
    return row >= 0 && row < (int)rows.size() ? rows[row].index : -1;
}
int pairById(const std::string &id) {
    for (size_t i = 0; i < library.pairs.size(); ++i)
        if (library.pairs[i].id == id)
            return (int)i;
    return -1;
}
void status(const std::wstring &s) {
    if (panel)
        SetDlgItemTextW(panel, P_STATUS, s.c_str());
}
int temporaryScope() { return panel ? choice(panel, P_SCOPE) : 0; }
Pair resolved(int i) {
    auto p = effective(library.pairs.at(i), library);
    int scope = temporaryScope();
    if (scope > 0)
        p.scope = scope - 1;
    return p;
}
std::array<int, 5> variants(const Pair &p) {
    return {p.mode, p.ignoreCase ? 3 : 4, p.maskEnabled ? 6 : 5,
            p.directoryEnabled ? (p.recursive ? 9 : 8) : 7, p.scope ? 11 : 10};
}
const std::array<int, 5> defaultVariants{2, 3, 5, 7, 10};
void refreshGroupList() {
    if (!IsWindow(groupsWindow))
        return;
    auto list = GetDlgItem(groupsWindow, G_LIST);
    int old = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (auto &g : library.groups)
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)wide(g.name).c_str());
    SendMessageW(list, LB_SETCURSEL, old, 0);
}
void refresh() {
    if (!panel)
        return;
    int old = selected();
    std::string previous = old >= 0 && old < (int)library.pairs.size() ? library.pairs[old].id : "";
    auto list = GetDlgItem(panel, P_LIST);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    rows.clear();
    hoverRow = -1;
    auto path = currentPath(), query = field(panel, P_SEARCH);
    auto add = [&](int index, std::wstring title, bool line = false) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = (int)rows.size();
        item.pszText = title.data();
        ListView_InsertItem(list, &item);
        rows.push_back({index, title, line});
        if (index >= 0 && library.pairs[index].id == previous)
            ListView_SetItemState(list, item.iItem, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
    };
    auto sort = [&](int a, int b) {
        auto &x = library.pairs[a];
        auto &y = library.pairs[b];
        int order = 0;
        if (library.sortMode == 0) {
            auto u = lower(label(x)), v = lower(label(y));
            order = u < v ? -1 : u > v ? 1 : 0;
        } else
            order = x.created < y.created ? -1 : x.created > y.created ? 1 : 0;
        if (!order)
            order = x.id < y.id ? -1 : 1;
        return library.sortDescending ? order > 0 : order < 0;
    };
    std::vector<std::string> groupIds{""};
    for (auto &g : library.groups)
        groupIds.push_back(g.id);
    for (bool favourites : {true, false}) {
        bool section = false;
        for (auto &gid : groupIds) {
            std::vector<int> entries;
            for (size_t i = 0; i < library.pairs.size(); ++i) {
                auto &raw = library.pairs[i];
                if (raw.group != gid)
                    continue;
                if (favourites ? !raw.favourite : raw.favourite && !library.repeatFavourites)
                    continue;
                auto p = effective(raw, library);
                if (visible(p, path, query, library.searchMode, library.showAll))
                    entries.push_back((int)i);
            }
            if (entries.empty())
                continue;
            std::sort(entries.begin(), entries.end(), sort);
            if (!section) {
                add(-1, tr(favourites ? L"Favourites" : L"Pairs"));
                section = true;
            }
            if (library.groupDisplay) {
                std::wstring name = tr(L"Ungrouped");
                for (auto &g : library.groups)
                    if (g.id == gid)
                        name = wide(g.name);
                add(-1, name, library.groupDisplay == 2);
            }
            for (int i : entries)
                add(i, label(library.pairs[i]));
        }
    }
    if (rows.empty() && !query.empty())
        add(-1, tr(L"No results"));
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    if (settingsWindow)
        SetDlgItemTextW(settingsWindow, S_COUNT,
                        (tr(L"Pairs: ") + std::to_wstring(library.pairs.size())).c_str());
    refreshGroupList();
}
void toggleFavourite(int i) {
    if (i < 0 || i >= (int)library.pairs.size())
        return;
    auto next = library;
    next.pairs[i].favourite = !next.pairs[i].favourite;
    commit(std::move(next));
    refresh();
}
void clipboard(HWND h, const std::wstring &text) {
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (!mem)
        throw std::runtime_error("Cannot allocate clipboard data.");
    auto p = GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        throw std::runtime_error("Cannot access clipboard data.");
    }
    memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(mem);
    if (!OpenClipboard(h)) {
        GlobalFree(mem);
        throw std::runtime_error("Cannot open clipboard.");
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        CloseClipboard();
        GlobalFree(mem);
        throw std::runtime_error("Cannot copy report.");
    }
    CloseClipboard();
}
struct Report {
    std::wstring title, text;
    bool copy = true;
};
INT_PTR CALLBACK reportProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto ctx = (Report *)GetWindowLongPtrW(h, DWLP_USER);
    try {
        if (m == WM_INITDIALOG) {
            ctx = (Report *)l;
            SetWindowLongPtrW(h, DWLP_USER, l);
            translateDialog(h);
            SetWindowTextW(h, ctx->title.c_str());
            SendDlgItemMessageW(h, V_TEXT, EM_SETLIMITTEXT, 8000000, 0);
            SetDlgItemTextW(h, V_TEXT, ctx->text.c_str());
            ShowWindow(GetDlgItem(h, V_COPY), ctx->copy ? SW_SHOW : SW_HIDE);
            SendMessageW(h, WM_SIZE, 0, 0);
            theme(h);
            center(h);
            return TRUE;
        }
        if (m == WM_SIZE && ctx) {
            RECT r;
            GetClientRect(h, &r);
            int b = 36;
            MoveWindow(GetDlgItem(h, V_TEXT), 8, 8, std::max(10L, r.right - 16),
                       std::max(10L, r.bottom - b - 16), TRUE);
            if (ctx->copy) {
                MoveWindow(GetDlgItem(h, V_COPY), 8, r.bottom - b, std::max(50L, r.right - 140), 28, TRUE);
                MoveWindow(GetDlgItem(h, IDCANCEL), r.right - 124, r.bottom - b, 116, 28, TRUE);
            } else
                MoveWindow(GetDlgItem(h, IDCANCEL), 8, r.bottom - b, std::max(10L, r.right - 16), 28, TRUE);
            return TRUE;
        }
        if (m == WM_COMMAND) {
            if (LOWORD(w) == V_COPY) {
                clipboard(h, field(h, V_TEXT));
                return TRUE;
            }
            if (LOWORD(w) == IDCANCEL || LOWORD(w) == IDOK) {
                DestroyWindow(h);
                return TRUE;
            }
        }
        if (m == WM_CLOSE) {
            DestroyWindow(h);
            return TRUE;
        }
        if (m == WM_NCDESTROY) {
            unregisterDialog(h);
            delete ctx;
            return FALSE;
        }
    } catch (const std::exception &e) {
        error(e, h);
    }
    return FALSE;
}
void report(const std::wstring &title, const std::wstring &text, bool copy = true) {
    auto ctx = std::make_unique<Report>(Report{title, text, copy});
    openModeless(ID_REPORT, reportProc, ctx.get());
    ctx.release();
}
std::wstring escaped(const std::string &s, int cp) {
    auto t = wide(s, cp ? cp : GetACP());
    std::wstring r;
    for (auto c : t) {
        if (r.size() > 400) {
            r += L" ...";
            break;
        }
        if (c == L'\r')
            r += L"\\r";
        else if (c == L'\n')
            r += L"\\n";
        else if (c == L'\t')
            r += L"\\t";
        else if (c == 0)
            r += L"\\0";
        else
            r += c;
    }
    return r;
}
void run(bool preview) {
    int i = selected();
    if (i < 0)
        return;
    auto p = resolved(i);
    HWND h = editor();
    if (!preview && sci(h, SCI_GETREADONLY))
        throw std::runtime_error("The document is read-only.");
    auto len = sci(h, SCI_GETLENGTH);
    if (len > 32 * 1024 * 1024)
        throw std::runtime_error("This version supports documents up to 32 MiB.");
    intptr_t a = 0, b = len;
    if (p.scope == 0) {
        if (sci(h, SCI_GETSELECTIONS) != 1 || sci(h, SCI_SELECTIONISRECTANGLE))
            throw std::runtime_error("Use one normal text selection.");
        a = sci(h, SCI_GETSELECTIONSTART);
        b = sci(h, SCI_GETSELECTIONEND);
        if (a == b)
            throw std::runtime_error("Select text first, or choose Whole document.");
    }
    if (!visible(p, currentPath(), L"", 0, library.showAll)) {
        refresh();
        return;
    }
    status(tr(L"Calculating replacements..."));
    auto plan = makePlan(p, range(h, 0, len), (int)sci(h, SCI_GETCODEPAGE), a, b);
    if (preview) {
        std::wstring s = label(p) + L"\r\n" + tr(L"Matches: ") + std::to_wstring(plan.edits.size()) +
                         L"\r\n" + tr(L"No changes applied. First 100 matches:") + L"\r\n\r\n";
        for (size_t n = 0; n < std::min<size_t>(100, plan.edits.size()); ++n) {
            auto &e = plan.edits[n];
            s += L"#" + std::to_wstring(n + 1) + L"  @" + std::to_wstring(e.start) + L"\r\n" +
                 tr(L"Before: ") + escaped(e.before, plan.cp) + L"\r\n" + tr(L"After: ") +
                 escaped(e.after, plan.cp) + L"\r\n\r\n";
        }
        report(tr(L"Replacement preview"), s);
    } else {
        applyPlan(h, plan);
        if (library.collectStats) {
            auto next = library;
            next.pairs[i].executed++;
            next.pairs[i].replaced += plan.edits.size();
            next.executions++;
            next.replacements += plan.edits.size();
            try {
                commit(std::move(next));
            } catch (const std::exception &) {
                throw std::runtime_error("Replacement completed, but statistics could not be saved.");
            }
        }
        if (library.beep)
            MessageBeep(MB_OK);
        SetFocus(h);
    }
    status(std::to_wstring(plan.edits.size()) +
           tr(preview ? L" matches (preview)." : L" replacements. Ctrl+Z to undo."));
}
void statistics() {
    load();
    std::wstring s = L"KPairs " + wide(APP_VER) + L"\r\n\r\n" + tr(L"Pairs: ") +
                     std::to_wstring(library.pairs.size()) + L"\r\n" + tr(L"Pair executions: ") +
                     std::to_wstring(library.executions) + L"\r\n" + tr(L"Strings replaced: ") +
                     std::to_wstring(library.replacements) + L"\r\n";
    for (bool byReplacement : {false, true}) {
        s += L"\r\n" + tr(byReplacement ? L"Top 10 by strings replaced" : L"Top 10 by executions") + L"\r\n";
        std::vector<int> idx(library.pairs.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
            return byReplacement ? library.pairs[a].replaced > library.pairs[b].replaced
                                 : library.pairs[a].executed > library.pairs[b].executed;
        });
        int rank = 0;
        for (int i : idx) {
            auto &p = library.pairs[i];
            auto value = byReplacement ? p.replaced : p.executed;
            if (!value || rank == 10)
                break;
            s += std::to_wstring(++rank) + L". " + label(p) + L": " + std::to_wstring(value) + L"\r\n";
        }
    }
    s += L"\r\n" + tr(L"Local statistics only. No data is sent.");
    report(tr(L"Statistics"), s);
}
const std::vector<std::wstring> flagNames{L"Normal",          L"Extended",
                                          L"Regex",           L"Ignore case",
                                          L"Case-sensitive",  L"No filename filter",
                                          L"Filename filter", L"No directory filter",
                                          L"Exact directory", L"Recursive directory",
                                          L"Selection only",  L"Whole document"};
void license() {
    auto r = FindResourceW(instance, MAKEINTRESOURCEW(LICENSE_TEXT), RT_RCDATA);
    if (!r)
        throw std::runtime_error("Cannot load licence.");
    auto loadedResource = LoadResource(instance, r);
    auto p = (const char *)LockResource(loadedResource);
    report(L"GPL-3.0-or-later", wide(std::string(p, SizeofResource(instance, r))), false);
}

bool pickColor(HWND h, int &color) {
    static COLORREF custom[16]{};
    CHOOSECOLORW cc{sizeof(cc)};
    cc.hwndOwner = h;
    cc.rgbResult = color < 0 ? RGB(128, 128, 128) : color;
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc))
        return false;
    color = (int)cc.rgbResult;
    return true;
}
void pickDirectory(HWND h) {
    IFileOpenDialog *dlg = nullptr;
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (SUCCEEDED(hr)) {
        DWORD options;
        dlg->GetOptions(&options);
        dlg->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dlg->SetTitle(tr(L"Add directory").c_str());
        if (SUCCEEDED(dlg->Show(h))) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    SetDlgItemTextW(h, E_DIR, appendDirectory(field(h, E_DIR), path).c_str());
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    if (FAILED(hr))
        throw std::runtime_error("Cannot open directory picker.");
}
struct EditContext {
    Pair pair;
    bool group = false, isNew = true;
    std::vector<std::string> groupIds;
};
const std::array<int, 7> inheritIds{I_MODE, I_CASE, I_SCOPE, I_COLOR, I_MASK, I_DIR, I_RECURSIVE};
const std::array<unsigned, 7> optionBits{O_MODE, O_CASE, O_SCOPE, O_COLOR, O_MASK, O_DIR, O_RECURSIVE};
void readOptions(HWND h, EditContext &c) {
    auto &p = c.pair;
    auto own = [&](int bit) { return c.group || p.group.empty() || (p.overrides & bit); };
    if (own(O_MODE))
        p.mode = choice(h, E_MODE);
    if (own(O_SCOPE))
        p.scope = choice(h, E_SCOPE);
    if (own(O_CASE))
        p.ignoreCase = checked(h, E_CASE);
    if (own(O_MASK)) {
        p.maskEnabled = checked(h, E_MASKON);
        p.mask = bytes(field(h, E_MASK));
    }
    if (own(O_DIR)) {
        p.directoryEnabled = checked(h, E_DIRON);
        p.directory = bytes(field(h, E_DIR));
    }
    if (own(O_RECURSIVE))
        p.recursive = checked(h, E_RECURSIVE);
}
void displayOptions(HWND h, EditContext &c) {
    auto p = c.group ? c.pair : effective(c.pair, library);
    combo(h, E_MODE, {L"Normal", L"Extended", L"Regex"}, p.mode);
    combo(h, E_SCOPE, {L"Selection only", L"Whole document"}, p.scope);
    check(h, E_CASE, p.ignoreCase);
    check(h, E_MASKON, p.maskEnabled);
    check(h, E_DIRON, p.directoryEnabled);
    check(h, E_RECURSIVE, p.recursive);
    field(h, E_MASK, p.mask);
    field(h, E_DIR, p.directory);
    std::array<std::vector<int>, 7> values{{{E_MODE},
                                            {E_CASE},
                                            {E_SCOPE},
                                            {E_COLOR, E_CLEARCOLOR},
                                            {E_MASKON, E_MASK},
                                            {E_DIRON, E_DIR, E_BROWSE},
                                            {E_RECURSIVE}}};
    for (size_t i = 0; i < optionBits.size(); ++i) {
        bool inherited = !c.group && !c.pair.group.empty() && !(c.pair.overrides & optionBits[i]);
        check(h, inheritIds[i], inherited);
        EnableWindow(GetDlgItem(h, inheritIds[i]), !c.pair.group.empty());
        for (int id : values[i])
            EnableWindow(GetDlgItem(h, id), !inherited);
    }
    InvalidateRect(GetDlgItem(h, E_SWATCH), nullptr, TRUE);
}
INT_PTR CALLBACK editProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto c = (EditContext *)GetWindowLongPtrW(h, DWLP_USER);
    try {
        if (m == WM_INITDIALOG) {
            c = (EditContext *)l;
            SetWindowLongPtrW(h, DWLP_USER, l);
            translateDialog(h);
            field(h, E_NAME, c->pair.name);
            if (!c->group) {
                for (int id : {E_NAME, E_FIND, E_REPLACE, E_DESCRIPTION, E_MASK, E_DIR})
                    SendDlgItemMessageW(h, id, EM_SETLIMITTEXT, 250000, 0);
                field(h, E_FIND, c->pair.find);
                field(h, E_REPLACE, c->pair.replace);
                field(h, E_DESCRIPTION, c->pair.description);
                check(h, E_FAV, c->pair.favourite);
                c->groupIds = {""};
                std::vector<std::wstring> names{tr(L"None")};
                int selectedGroup = 0;
                for (auto &g : library.groups) {
                    c->groupIds.push_back(g.id);
                    names.push_back(wide(g.name));
                    if (g.id == c->pair.group)
                        selectedGroup = (int)names.size() - 1;
                }
                combo(h, E_GROUP, names, selectedGroup, false);
                SetDlgItemTextW(h, E_COUNT, (tr(L"Use count: ") + std::to_wstring(c->pair.executed)).c_str());
            }
            displayOptions(h, *c);
            theme(h);
            center(h);
            return TRUE;
        }
        if (m == WM_DRAWITEM && c) {
            auto d = (DRAWITEMSTRUCT *)l;
            if (d->CtlID == E_SWATCH) {
                auto p = c->group ? c->pair : effective(c->pair, library);
                HBRUSH b = CreateSolidBrush(p.color < 0 ? GetSysColor(COLOR_BTNFACE) : p.color);
                FillRect(d->hDC, &d->rcItem, b);
                DeleteObject(b);
                FrameRect(d->hDC, &d->rcItem, (HBRUSH)GetStockObject(GRAY_BRUSH));
                return TRUE;
            }
        }
        if (m == WM_COMMAND && c) {
            int id = LOWORD(w);
            if (id == IDCANCEL) {
                DestroyWindow(h);
                return TRUE;
            }
            if (id == E_GROUP && HIWORD(w) == CBN_SELCHANGE) {
                readOptions(h, *c);
                int n = choice(h, E_GROUP);
                c->pair.group = n >= 0 ? c->groupIds.at(n) : "";
                displayOptions(h, *c);
                return TRUE;
            }
            for (size_t i = 0; i < inheritIds.size(); ++i)
                if (id == inheritIds[i]) {
                    readOptions(h, *c);
                    if (checked(h, id))
                        c->pair.overrides &= ~optionBits[i];
                    else
                        c->pair.overrides |= optionBits[i];
                    displayOptions(h, *c);
                    return TRUE;
                }
            if (id == E_COLOR) {
                if (pickColor(h, c->pair.color))
                    InvalidateRect(GetDlgItem(h, E_SWATCH), nullptr, TRUE);
                return TRUE;
            }
            if (id == E_CLEARCOLOR) {
                c->pair.color = -1;
                InvalidateRect(GetDlgItem(h, E_SWATCH), nullptr, TRUE);
                return TRUE;
            }
            if (id == E_BROWSE) {
                pickDirectory(h);
                return TRUE;
            }
            if (id == IDOK) {
                readOptions(h, *c);
                c->pair.name = bytes(field(h, E_NAME));
                auto next = library;
                if (c->group) {
                    if (trim(wide(c->pair.name)).empty())
                        throw std::runtime_error("Group name must not be empty.");
                    Group g;
                    static_cast<Options &>(g) = c->pair;
                    g.id = c->pair.id;
                    g.name = c->pair.name;
                    auto it = std::find_if(next.groups.begin(), next.groups.end(),
                                           [&](auto &x) { return x.id == g.id; });
                    if (!c->isNew && it == next.groups.end())
                        throw std::runtime_error("This group was deleted.");
                    if (it == next.groups.end())
                        next.groups.push_back(g);
                    else
                        *it = g;
                } else {
                    c->pair.find = bytes(field(h, E_FIND));
                    c->pair.replace = bytes(field(h, E_REPLACE));
                    c->pair.description = bytes(field(h, E_DESCRIPTION));
                    c->pair.favourite = checked(h, E_FAV);
                    if (!c->pair.group.empty() &&
                        std::none_of(next.groups.begin(), next.groups.end(),
                                     [&](auto &g) { return g.id == c->pair.group; }))
                        throw std::runtime_error("This group was deleted.");
                    validate(effective(c->pair, next));
                    int index = pairById(c->pair.id);
                    if (!c->isNew && index < 0)
                        throw std::runtime_error("This pair was deleted.");
                    if (index < 0)
                        next.pairs.push_back(c->pair);
                    else {
                        c->pair.executed = next.pairs[index].executed;
                        c->pair.replaced = next.pairs[index].replaced;
                        next.pairs[index] = c->pair;
                    }
                }
                commit(std::move(next));
                refresh();
                DestroyWindow(h);
                return TRUE;
            }
        }
        if (m == WM_CLOSE) {
            DestroyWindow(h);
            return TRUE;
        }
        if (m == WM_NCDESTROY) {
            unregisterDialog(h);
            delete c;
            return FALSE;
        }
    } catch (const std::exception &e) {
        error(e, h);
    }
    return FALSE;
}
void editPair(int index, bool duplicate) {
    load();
    auto c = std::make_unique<EditContext>();
    if (index >= 0) {
        c->pair = library.pairs.at(index);
        c->isNew = duplicate;
        if (duplicate) {
            c->pair.id = newId();
            c->pair.created = timestamp();
            c->pair.executed = c->pair.replaced = 0;
            c->pair.favourite = false;
        }
    }
    openModeless(ID_EDIT, editProc, c.get());
    c.release();
}
void editGroup(int index) {
    load();
    auto c = std::make_unique<EditContext>();
    c->group = true;
    if (index >= 0) {
        auto &g = library.groups.at(index);
        static_cast<Options &>(c->pair) = g;
        c->pair.id = g.id;
        c->pair.name = g.name;
        c->isNew = false;
    }
    openModeless(ID_GROUPEDIT, editProc, c.get());
    c.release();
}
INT_PTR CALLBACK groupsProc(HWND h, UINT m, WPARAM w, LPARAM) {
    try {
        if (m == WM_INITDIALOG) {
            groupsWindow = h;
            translateDialog(h);
            refreshGroupList();
            theme(h);
            center(h);
            return TRUE;
        }
        if (m == WM_COMMAND) {
            int id = LOWORD(w), index = (int)SendDlgItemMessageW(h, G_LIST, LB_GETCURSEL, 0, 0);
            if (id == IDCANCEL) {
                DestroyWindow(h);
                return TRUE;
            }
            if (id == G_ADD) {
                editGroup(-1);
                return TRUE;
            }
            if (id == G_EDIT || (id == G_LIST && HIWORD(w) == LBN_DBLCLK)) {
                if (index >= 0)
                    editGroup(index);
                return TRUE;
            }
            if (id == G_DELETE && index >= 0) {
                if (MessageBoxW(h,
                                tr(L"Delete this group? Its pairs will keep their current settings.").c_str(),
                                L"KPairs", MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return TRUE;
                auto next = library;
                auto gid = next.groups.at(index).id;
                for (auto &p : next.pairs)
                    if (p.group == gid) {
                        p = effective(p, library);
                        p.group.clear();
                        p.overrides = 0;
                    }
                next.groups.erase(next.groups.begin() + index);
                commit(std::move(next));
                refresh();
                refreshGroupList();
                return TRUE;
            }
        }
        if (m == WM_CLOSE) {
            DestroyWindow(h);
            return TRUE;
        }
        if (m == WM_NCDESTROY) {
            groupsWindow = nullptr;
            unregisterDialog(h);
        }
    } catch (const std::exception &e) {
        error(e, h);
    }
    return FALSE;
}
void groups() {
    load();
    if (IsWindow(groupsWindow)) {
        ShowWindow(groupsWindow, SW_SHOW);
        SetForegroundWindow(groupsWindow);
        return;
    }
    openModeless(ID_GROUPS, groupsProc);
}
struct FlagContext {
    std::vector<Flag> flags;
    int current = 0;
};
void paintFlag(HDC dc, RECT r, const Flag &f) {
    HBRUSH brush = CreateSolidBrush(f.bg);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, f.fg);
    auto s = wide(f.text);
    DrawTextW(dc, s.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

INT_PTR CALLBACK guideProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_INITDIALOG) {
        translateDialog(h);
        for (size_t i = 0; i < flagNames.size(); ++i)
            SendDlgItemMessageW(h, G_LIST, LB_ADDSTRING, 0, (LPARAM)tr(flagNames[i]).c_str());
        theme(h);
        center(h);
        return TRUE;
    }
    if (m == WM_MEASUREITEM) {
        auto d = (MEASUREITEMSTRUCT *)l;
        d->itemHeight = MulDiv(28, GetDpiForWindow(h), 96);
        return TRUE;
    }
    if (m == WM_DRAWITEM) {
        auto d = (DRAWITEMSTRUCT *)l;
        if (d->itemID >= library.flags.size())
            return TRUE;
        bool dark = SendMessageW(npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0;
        HBRUSH b = CreateSolidBrush(dark ? RGB(40, 40, 40) : GetSysColor(COLOR_WINDOW));
        FillRect(d->hDC, &d->rcItem, b);
        DeleteObject(b);
        int scale = GetDpiForWindow(h);
        RECT badge = d->rcItem;
        badge.left += MulDiv(5, scale, 96);
        badge.right = badge.left + MulDiv(80, scale, 96);
        badge.top += 3;
        badge.bottom -= 3;
        paintFlag(d->hDC, badge, library.flags[d->itemID]);
        RECT text = d->rcItem;
        text.left = badge.right + 12;
        SetTextColor(d->hDC, dark ? RGB(225, 225, 225) : GetSysColor(COLOR_WINDOWTEXT));
        SetBkMode(d->hDC, TRANSPARENT);
        auto s = tr(flagNames[d->itemID]);
        DrawTextW(d->hDC, s.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER);
        return TRUE;
    }
    if (m == WM_CLOSE || (m == WM_COMMAND && (LOWORD(w) == IDCANCEL || LOWORD(w) == IDOK))) {
        DestroyWindow(h);
        return TRUE;
    }
    if (m == WM_NCDESTROY)
        unregisterDialog(h);
    return FALSE;
}
void guide() {
    load();
    openModeless(ID_GUIDE, guideProc);
}

INT_PTR CALLBACK flagsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto c = (FlagContext *)GetWindowLongPtrW(h, DWLP_USER);
    try {
        if (m == WM_INITDIALOG) {
            c = (FlagContext *)l;
            SetWindowLongPtrW(h, DWLP_USER, l);
            translateDialog(h);
            combo(h, F_VARIANT, flagNames);
            SendDlgItemMessageW(h, F_LABEL, EM_SETLIMITTEXT, 8, 0);
            field(h, F_LABEL, c->flags[0].text);
            theme(h);
            center(h);
            return TRUE;
        }
        if (m == WM_DRAWITEM && c) {
            auto d = (DRAWITEMSTRUCT *)l;
            if (d->CtlID == F_SAMPLE) {
                paintFlag(d->hDC, d->rcItem, c->flags[c->current]);
                return TRUE;
            }
        }
        if (m == WM_COMMAND && c) {
            int id = LOWORD(w);
            if (id == IDCANCEL) {
                DestroyWindow(h);
                return TRUE;
            }
            if (id == F_LABEL && HIWORD(w) == EN_CHANGE) {
                c->flags[c->current].text = bytes(field(h, F_LABEL));
                InvalidateRect(GetDlgItem(h, F_SAMPLE), nullptr, TRUE);
                return TRUE;
            }
            if (id == F_VARIANT && HIWORD(w) == CBN_SELCHANGE) {
                c->current = choice(h, F_VARIANT);
                field(h, F_LABEL, c->flags[c->current].text);
                InvalidateRect(GetDlgItem(h, F_SAMPLE), nullptr, TRUE);
                return TRUE;
            }
            if (id == F_BG || id == F_FG) {
                pickColor(h, id == F_BG ? c->flags[c->current].bg : c->flags[c->current].fg);
                InvalidateRect(GetDlgItem(h, F_SAMPLE), nullptr, TRUE);
                return TRUE;
            }
            if (id == F_DEFAULT) {
                c->flags = defaultFlags();
                field(h, F_LABEL, c->flags[c->current].text);
                InvalidateRect(GetDlgItem(h, F_SAMPLE), nullptr, TRUE);
                return TRUE;
            }
            if (id == IDOK) {
                auto next = library;
                next.flags = c->flags;
                commit(std::move(next));
                if(settingsWindow)InvalidateRect(settingsWindow,nullptr,TRUE);
                refresh();
                DestroyWindow(h);
                return TRUE;
            }
        }
        if (m == WM_CLOSE) {
            DestroyWindow(h);
            return TRUE;
        }
        if (m == WM_NCDESTROY) {
            unregisterDialog(h);
            delete c;
        }
    } catch (const std::exception &e) {
        error(e, h);
    }
    return FALSE;
}
void flags() {
    auto c = std::make_unique<FlagContext>();
    c->flags = library.flags;
    openModeless(ID_FLAGS, flagsProc, c.get());
    c.release();
}
void settingsEnable(HWND h) {
    bool on = checked(h, S_FLAGS);
    for (int i = 0; i < 5; ++i) {
        EnableWindow(GetDlgItem(h, S_VISIBLE + i), on);
        EnableWindow(GetDlgItem(h, S_ALWAYS + i), on && checked(h, S_VISIBLE + i));
    }
    EnableWindow(GetDlgItem(h, M_FLAGS), on);
}
void updateMenuLanguage() {
    const wchar_t *names[]{L"Show KPairs", L"Settings", L"Groups", L"Statistics", L"Flag Guide", L"About"};
    HMENU menu = (HMENU)SendMessageW(npp._nppHandle, NPPM_GETMENUHANDLE, NPPPLUGINMENU, 0);
    for (int i = 0; i < 6; ++i) {
        auto name = tr(names[i]);
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_STRING;
        item.dwTypeData = name.data();
        SetMenuItemInfoW(menu, commands[i]._cmdID, FALSE, &item);
    }
    DrawMenuBar(npp._nppHandle);
}
void panelLanguage();
INT_PTR CALLBACK settingsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    try {
        if (m == WM_INITDIALOG) {
            settingsWindow = h;
            translateDialog(h);
            check(h, S_REPEAT, library.repeatFavourites);
            check(h, S_STATS, library.collectStats);
            check(h, S_BEEP, library.beep);
            check(h, S_SHOWALL, library.showAll);
            check(h, S_FLAGS, library.showFlags);
            combo(h, S_DOUBLE, {L"None", L"Execute replace", L"Edit pair", L"Preview"}, library.doubleClick);
            combo(h, S_GROUPDISPLAY, {L"None", L"Name", L"Line"}, library.groupDisplay);
            locale.scan(moduleDir / L"locale");
            std::vector<std::wstring> names;
            int selectedLanguage = 0;
            for (size_t i = 0; i < locale.available.size(); ++i) {
                auto &code = locale.available[i];
                names.push_back(code == "auto" ? tr(L"Automatic")
                                : code == "en" ? L"English"
                                : code == "pl" ? L"Polski"
                                               : wide(code));
                if (code == library.language)
                    selectedLanguage = (int)i;
            }
            combo(h, S_LANGUAGE, names, selectedLanguage, false);
            for (int i = 0; i < 5; ++i) {
                check(h, S_VISIBLE + i, library.flagVisible[i]);
                check(h, S_ALWAYS + i, library.flagAlways[i]);
            }
            settingsEnable(h);
            SetDlgItemTextW(h, S_COUNT, (tr(L"Pairs: ") + std::to_wstring(library.pairs.size())).c_str());
            theme(h);
            center(h);
            return TRUE;
        }
        if (m == WM_DRAWITEM) {
            auto d = (DRAWITEMSTRUCT*)l;
            if(d->CtlID >= S_SAMPLE && d->CtlID < S_SAMPLE+5){
                const std::vector<std::vector<int>> variants{{0,1,2},{3,4},{5,6},{7,8,9},{10,11}};
                auto& family = variants[d->CtlID-S_SAMPLE];
                int width = (d->rcItem.right-d->rcItem.left)/(int)family.size();
                for(size_t i=0;i<family.size();++i){RECT r=d->rcItem;r.left+=(int)i*width;r.right=r.left+width-2;paintFlag(d->hDC,r,library.flags[family[i]]);}
                return TRUE;
            }
        }
        if (m == WM_COMMAND) {
            int id = LOWORD(w);
            if (id == IDCANCEL) {
                DestroyWindow(h);
                return TRUE;
            }
            if (id == S_FLAGS || (id >= S_VISIBLE && id < S_VISIBLE + 5)) {
                settingsEnable(h);
                return TRUE;
            }
            if (id >= M_IMPORT && id <= M_ABOUT) {
                action(id);
                return TRUE;
            }
            if (id == IDOK) {
                auto next = library;
                next.repeatFavourites = checked(h, S_REPEAT);
                next.collectStats = checked(h, S_STATS);
                next.beep = checked(h, S_BEEP);
                next.showAll = checked(h, S_SHOWALL);
                next.showFlags = checked(h, S_FLAGS);
                next.doubleClick = choice(h, S_DOUBLE);
                next.groupDisplay = choice(h, S_GROUPDISPLAY);
                int lang = choice(h, S_LANGUAGE);
                if (lang >= 0)
                    next.language = locale.available.at(lang);
                for (int i = 0; i < 5; ++i) {
                    next.flagVisible[i] = checked(h, S_VISIBLE + i);
                    next.flagAlways[i] = checked(h, S_ALWAYS + i);
                }
                commit(std::move(next));
                locale.load(library.language);
                updateMenuLanguage();
                panelLanguage();
                refresh();
                DestroyWindow(h);
                return TRUE;
            }
        }
        if (m == WM_CLOSE) {
            DestroyWindow(h);
            return TRUE;
        }
        if (m == WM_NCDESTROY) {
            settingsWindow = nullptr;
            unregisterDialog(h);
        }
    } catch (const std::exception &e) {
        error(e, h);
    }
    return FALSE;
}
void settings() {
    load();
    if (IsWindow(settingsWindow)) {
        ShowWindow(settingsWindow, SW_SHOW);
        SetForegroundWindow(settingsWindow);
        return;
    }
    openModeless(ID_SETTINGS, settingsProc);
}
INT_PTR CALLBACK aboutProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    try {
        if (m == WM_INITDIALOG) {
            translateDialog(h);
            SetDlgItemTextW(h, A_VERSION, (L"KPairs " + wide(APP_VER)).c_str());
            SetDlgItemTextW(h, A_PATH, configFile.c_str());
            theme(h);
            center(h);
            return TRUE;
        }
        if (m == WM_NOTIFY) {
            auto n = (NMHDR *)l;
            if (n->code == NM_CLICK || n->code == NM_RETURN) {
                if (n->idFrom == A_GITHUB)
                    ShellExecuteW(h, L"open", wide(APP_PROJECT).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (n->idFrom == A_COFFEE)
                    ShellExecuteW(h, L"open", wide(APP_SUPPORT).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
        }
        if (m == WM_COMMAND) {
            if (LOWORD(w) == M_LICENSE) {
                license();
                return TRUE;
            }
            if (LOWORD(w) == IDCANCEL || LOWORD(w) == IDOK) {
                DestroyWindow(h);
                return TRUE;
            }
        }
        if (m == WM_CLOSE) {
            DestroyWindow(h);
            return TRUE;
        }
        if (m == WM_NCDESTROY)
            unregisterDialog(h);
    } catch (const std::exception &e) {
        error(e, h);
    }
    return FALSE;
}
void about() {
    load();
    openModeless(ID_ABOUT, aboutProc);
}
std::filesystem::path chooseFile(bool save, bool simple = false) {
    wchar_t name[32768]{};
    if (save)
        wcscpy_s(name, L"KPairs.json");
    OPENFILENAMEW o{sizeof(o)};
    o.hwndOwner = npp._nppHandle;
    o.lpstrFile = name;
    o.nMaxFile = 32768;
    o.lpstrFilter = simple ? L"All files\0*.*\0\0" : L"KPairs JSON\0*.json\0All files\0*.*\0\0";
    o.lpstrDefExt = simple ? nullptr : L"json";
    o.Flags =
        OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o)))
        return {};
    return name;
}
INT_PTR CALLBACK importProc(HWND h, UINT m, WPARAM w, LPARAM) {
    if (m == WM_INITDIALOG) {
        translateDialog(h);
        combo(h, E_MODE, {L"Normal", L"Extended", L"Regex"}, 2);
        theme(h);
        center(h);
        return TRUE;
    }
    if (m == WM_COMMAND) {
        if (LOWORD(w) == IDOK) {
            EndDialog(h, choice(h, E_MODE) + 10);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) {
            EndDialog(h, 0);
            return TRUE;
        }
    }
    if (m == WM_CLOSE) {
        EndDialog(h, 0);
        return TRUE;
    }
    return FALSE;
}
void importFile(bool simple) {
    auto path = chooseFile(false, simple);
    if (path.empty())
        return;
    auto next = library;
    size_t count = 0;
    if (simple) {
        auto mode = DialogBoxParamW(instance, MAKEINTRESOURCEW(ID_IMPORT), npp._nppHandle, importProc, 0);
        if (mode < 10)
            return;
        std::ifstream f(path, std::ios::binary);
        if (!f || std::filesystem::file_size(path) > 16 * 1024 * 1024)
            throw std::runtime_error("Cannot read import file, or file is too large.");
        std::string contents((std::istreambuf_iterator<char>(f)), {});
        auto pairs = importSimple(contents, (int)mode - 10);
        count = pairs.size();
        for (auto &p : pairs)
            validate(p);
        next.pairs.insert(next.pairs.end(), pairs.begin(), pairs.end());
    } else {
        auto incoming = readFile(path);
        std::map<std::string, std::string> ids;
        for (auto &g : incoming.groups) {
            auto original = g.id;
            g.id = newId();
            ids[original] = g.id;
            next.groups.push_back(g);
        }
        for (auto &p : incoming.pairs) {
            p.id = newId();
            if (!p.group.empty())
                p.group = ids.at(p.group);
            p.executed = p.replaced = 0;
            validate(effective(p, next));
            next.pairs.push_back(p);
        }
        count = incoming.pairs.size();
    }
    commit(std::move(next));
    refresh();
    status(tr(L"Imported pairs: ") + std::to_wstring(count));
}
void action(int id) {
    load();
    int i = selected();
    switch (id) {
    case P_ADD:
        editPair(-1);
        break;
    case P_EDIT:
        if (i >= 0)
            editPair(i);
        break;
    case P_DUP:
        if (i >= 0)
            editPair(i, true);
        break;
    case P_DELETE:
        if (i >= 0 && MessageBoxW(panel, tr(L"Delete this pair?").c_str(), L"KPairs",
                                  MB_YESNO | MB_ICONQUESTION) == IDYES) {
            auto next = library;
            next.pairs.erase(next.pairs.begin() + i);
            commit(std::move(next));
            refresh();
        }
        break;
    case M_FAV:
        toggleFavourite(i);
        break;
    case P_PREVIEW:
        run(true);
        break;
    case P_RUN:
        run(false);
        break;
    case M_SETTINGS:
        settings();
        break;
    case M_GROUPS:
        groups();
        break;
    case M_STATS:
        statistics();
        break;
    case M_GUIDE:
        guide();
        break;
    case M_FLAGS:
        flags();
        break;
    case M_ABOUT:
        about();
        break;
    case M_IMPORT:
        importFile(false);
        break;
    case M_SIMPLE:
        importFile(true);
        break;
    case M_EXPORT: {
        auto p = chooseFile(true);
        if (!p.empty())
            saveFile(p, library);
        break;
    }
    }
}

void popup(bool more, POINT pt) {
    HMENU menu = CreatePopupMenu();
    auto add = [&](int id, const wchar_t *name) { AppendMenuW(menu, MF_STRING, id, tr(name).c_str()); };
    if (!more) {
        add(P_RUN, L"Replace all");
        add(P_PREVIEW, L"Preview");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(P_EDIT, L"Edit pair");
        add(P_DUP, L"Duplicate");
        int i = selected();
        add(M_FAV, i >= 0 && library.pairs[i].favourite ? L"Remove from Favourites" : L"Add to Favourites");
        add(P_DELETE, L"Delete");
    } else {
        add(M_GROUPS, L"Groups");
        add(M_SETTINGS, L"Settings");
        add(M_STATS, L"Statistics");
        add(M_GUIDE, L"Flag Guide");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(M_IMPORT, L"Import JSON");
        add(M_SIMPLE, L"Import (simple format)");
        add(M_EXPORT, L"Export JSON");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(M_ABOUT, L"About");
    }
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, panel, nullptr);
    DestroyMenu(menu);
    if (cmd)
        action(cmd);
}
int px(int n) { return MulDiv(n, panel ? GetDpiForWindow(panel) : 96, 96); }
void layout() {
    if (!panel)
        return;
    RECT r;
    GetClientRect(panel, &r);
    int margin = px(6), gap = px(4), width = std::max(1, (int)r.right - 2 * margin), y = margin;
    auto move = [&](int id, int x, int top, int w, int height) {
        MoveWindow(GetDlgItem(panel, id), x, top, std::max(1, w), std::max(1, height), TRUE);
    };
    move(P_SEARCH, margin, y, width, px(24));
    y += px(29);
    int modeWidth = std::max(1, (width - 2 * gap) / 3);
    for (int i = 0; i < 3; ++i)
        move(P_AND + i, margin + i * (modeWidth + gap), y, modeWidth, px(21));
    y += px(25);
    std::vector<std::pair<int, int>> buttons{{P_ADD, 48},  {P_EDIT, 48},     {P_DUP, 80},     {P_DELETE, 58},
                                             {P_MORE, 32}, {P_SORTNAME, 48}, {P_SORTDATE, 48}};
    int x = margin;
    for (auto [id, w] : buttons) {
        w = px(w);
        if (x > margin && x + w > margin + width) {
            x = margin;
            y += px(28);
        }
        move(id, x, y, std::min(w, width), px(24));
        x += w + gap;
    }
    y += px(30);
    int bottom = std::max(y + px(25), (int)r.bottom - px(132));
    move(P_LIST, margin, y, width, bottom - y);
    ListView_SetColumnWidth(GetDlgItem(panel, P_LIST), 0, std::max(1, width - px(20)));
    move(P_HINT, margin, bottom + px(5), width, px(18));
    move(P_SCOPE, margin, bottom + px(25), width, px(160));
    int half = (width - gap) / 2;
    move(P_PREVIEW, margin, bottom + px(56), half, px(27));
    move(P_RUN, margin + half + gap, bottom + px(56), half, px(27));
    move(P_STATUS, margin, bottom + px(88), width, px(38));
}
void panelLanguage() {
    if (!panel)
        return;
    for (auto [id, s] : std::vector<std::pair<int, const wchar_t *>>{{P_ADD, L"Add"},
                                                                     {P_EDIT, L"Edit"},
                                                                     {P_DUP, L"Duplicate"},
                                                                     {P_DELETE, L"Delete"},
                                                                     {P_PREVIEW, L"Preview"},
                                                                     {P_RUN, L"Replace all"},
                                                                     {P_HINT, L"Temporary scope"}})
        SetDlgItemTextW(panel, id, tr(s).c_str());
    SendDlgItemMessageW(panel, P_SEARCH, EM_SETCUEBANNER, TRUE,
                        (LPARAM)tr(L"Search name, description, find, replace").c_str());
    int scope = std::max(0, choice(panel, P_SCOPE));
    combo(panel, P_SCOPE, {L"Use pair settings", L"Selection only", L"Whole document"}, scope);
    check(panel, P_AND, library.searchMode == 0);
    check(panel, P_OR, library.searchMode == 1);
    check(panel, P_FULL, library.searchMode == 2);
    SetDlgItemTextW(panel, P_SORTNAME,
                    library.sortMode == 0 ? (library.sortDescending ? L"Z–A" : L"A–Z") : L"A–Z");
    auto date =
        (library.sortMode == 1 ? std::wstring(library.sortDescending ? L"↓ " : L"↑ ") : L"") + tr(L"Date");
    SetDlgItemTextW(panel, P_SORTDATE, date.c_str());
    layout();
}
int hitRow(HWND list, LPARAM l) {
    LVHITTESTINFO hit{};
    hit.pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    return ListView_HitTest(list, &hit);
}
bool isHeart(HWND list, LPARAM l, int &row) {
    row = hitRow(list, l);
    int x = GET_X_LPARAM(l);
    return row >= 0 && row < (int)rows.size() && rows[row].index >= 0 && x >= px(20) && x < px(44);
}
LRESULT CALLBACK listProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    try {
        int row = -1;
        if (m == WM_MOUSEMOVE) {
            int hover = isHeart(h, l, row) ? row : -1;
            if (hover != hoverRow) {
                hoverRow = hover;
                InvalidateRect(h, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
            TrackMouseEvent(&track);
        }
        if (m == WM_MOUSELEAVE) {
            hoverRow = -1;
            InvalidateRect(h, nullptr, FALSE);
        }
        if (m == WM_LBUTTONDOWN && isHeart(h, l, row)) {
            heartPressed = true;
            SetCapture(h);
            return 0;
        }
        if (m == WM_LBUTTONUP && heartPressed) {
            heartPressed = false;
            ReleaseCapture();
            if (isHeart(h, l, row))
                toggleFavourite(rows[row].index);
            return 0;
        }
        if (m == WM_CAPTURECHANGED)
            heartPressed = false;
        if (m == WM_LBUTTONDBLCLK && isHeart(h, l, row))
            return 0;
        if (m == WM_NCDESTROY)
            RemoveWindowSubclass(h, listProc, 1);
    } catch (const std::exception &e) {
        error(e, h);
    }
    return DefSubclassProc(h, m, w, l);
}
LRESULT drawList(NMLVCUSTOMDRAW *d) {
    if (d->nmcd.dwDrawStage == CDDS_PREPAINT)
        return CDRF_NOTIFYITEMDRAW;
    if (d->nmcd.dwDrawStage != CDDS_ITEMPREPAINT)
        return CDRF_DODEFAULT;
    auto n = d->nmcd.dwItemSpec;
    if (n >= rows.size())
        return CDRF_DODEFAULT;
    auto &row = rows[n];
    HDC dc = d->nmcd.hdc;
    RECT r = d->nmcd.rc;
    bool selectedRow =
        (ListView_GetItemState(GetDlgItem(panel, P_LIST), (int)n, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    bool dark = SendMessageW(npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0;
    COLORREF bg = selectedRow ? GetSysColor(COLOR_HIGHLIGHT)
                  : dark      ? RGB(40, 40, 40)
                              : GetSysColor(COLOR_WINDOW);
    COLORREF fg = selectedRow ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                  : dark      ? RGB(225, 225, 225)
                              : GetSysColor(COLOR_WINDOWTEXT);
    HBRUSH b = CreateSolidBrush(bg);
    FillRect(dc, &r, b);
    DeleteObject(b);
    SetTextColor(dc, fg);
    SetBkMode(dc, TRANSPARENT);
    if (row.index < 0) {
        if (row.line) {
            HPEN pen = CreatePen(PS_SOLID, 1, dark ? RGB(100, 100, 100) : RGB(170, 170, 170));
            auto old = SelectObject(dc, pen);
            MoveToEx(dc, r.left + px(6), (r.top + r.bottom) / 2, nullptr);
            LineTo(dc, r.right - px(6), (r.top + r.bottom) / 2);
            SelectObject(dc, old);
            DeleteObject(pen);
        } else {
            auto old = SelectObject(dc, boldFont);
            r.left += px(5);
            DrawTextW(dc, row.title.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(dc, old);
        }
        return CDRF_SKIPDEFAULT;
    }
    auto p = resolved(row.index);
    if (p.color >= 0) {
        RECT swatch{r.left + px(5), r.top + px(7), r.left + px(15), r.bottom - px(7)};
        b = CreateSolidBrush(p.color);
        FillRect(dc, &swatch, b);
        DeleteObject(b);
    }
    RECT heart{r.left + px(20), r.top, r.left + px(44), r.bottom};
    SetTextColor(dc, p.favourite ? RGB(180, 55, 75)
                                 : ((int)n == hoverRow ? RGB(160, 105, 110) : RGB(135, 135, 135)));
    DrawTextW(dc, L"♥", 1, &heart, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(dc, fg);
    std::vector<std::pair<int, int>> badges;
    int total = 0;
    auto v = variants(p);
    if (library.showFlags)
        for (int family = 0; family < 5; ++family)
            if (library.flagVisible[family] &&
                (library.flagAlways[family] || v[family] != defaultVariants[family])) {
                auto &f = library.flags[v[family]];
                SIZE size;
                auto s = wide(f.text);
                GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &size);
                int width = size.cx + px(8);
                badges.push_back({v[family], width});
                total += width + px(3);
            }
    RECT title = r;
    title.left += px(48);
    title.right = std::max(title.left + px(20), r.right - total - px(6));
    DrawTextW(dc, row.title.c_str(), -1, &title, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SIZE nameSize;
    GetTextExtentPoint32W(dc, row.title.c_str(), (int)row.title.size(), &nameSize);
    int x = std::min(title.left + nameSize.cx + px(6), title.right + px(3));
    for (auto [variant, width] : badges) {
        if (x + width > r.right)
            break;
        RECT tag{x, r.top + px(4), x + width, r.bottom - px(4)};
        paintFlag(dc, tag, library.flags[variant]);
        x += width + px(3);
    }
    return CDRF_SKIPDEFAULT;
}
INT_PTR CALLBACK panelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    try {
        if (m == WM_INITDIALOG) {
            panel = h;
            auto list = GetDlgItem(h, P_LIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            LVCOLUMNW col{};
            col.mask = LVCF_WIDTH | LVCF_TEXT;
            col.cx = 300;
            col.pszText = const_cast<wchar_t *>(L"Pairs");
            ListView_InsertColumn(list, 0, &col);
            LOGFONTW font{};
            GetObjectW((HFONT)SendMessageW(list, WM_GETFONT, 0, 0), sizeof(font), &font);
            font.lfWeight = FW_BOLD;
            boldFont = CreateFontIndirectW(&font);
            rowImages = ImageList_Create(1, px(26), ILC_COLOR32, 1, 1);
            ListView_SetImageList(list, rowImages, LVSIL_SMALL);
            SetWindowSubclass(list, listProc, 1, 0);
            panelLanguage();
            theme(h);
            return TRUE;
        }
        if (m == WM_SIZE) {
            layout();
            return TRUE;
        }
        if (m == WM_COMMAND) {
            int id = LOWORD(w);
            if (id == P_SEARCH && HIWORD(w) == EN_CHANGE) {
                refresh();
                return TRUE;
            }
            if (id == P_SCOPE && HIWORD(w) == CBN_SELCHANGE) {
                refresh();
                return TRUE;
            }
            if (id >= P_AND && id <= P_FULL) {
                auto next = library;
                next.searchMode = id - P_AND;
                commit(std::move(next));
                panelLanguage();
                refresh();
                return TRUE;
            }
            if (id == P_SORTNAME || id == P_SORTDATE) {
                auto next = library;
                int mode = id == P_SORTNAME ? 0 : 1;
                next.sortDescending = next.sortMode == mode ? !next.sortDescending : false;
                next.sortMode = mode;
                commit(std::move(next));
                panelLanguage();
                refresh();
                return TRUE;
            }
            if (id == P_MORE) {
                RECT r;
                GetWindowRect(GetDlgItem(h, id), &r);
                popup(true, {r.left, r.bottom});
                return TRUE;
            }
            action(id);
            return TRUE;
        }
        if (m == WM_CONTEXTMENU && (HWND)w == GetDlgItem(h, P_LIST)) {
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (pt.x == -1)
                GetCursorPos(&pt);
            else {
                POINT local = pt;
                ScreenToClient((HWND)w, &local);
                LVHITTESTINFO hit{};
                hit.pt = local;
                int row = ListView_HitTest((HWND)w, &hit);
                ListView_SetItemState((HWND)w, -1, 0, LVIS_SELECTED);
                if (row >= 0)
                    ListView_SetItemState((HWND)w, row, LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);
            }
            if (selected() >= 0)
                popup(false, pt);
            return TRUE;
        }
        if (m == WM_NOTIFY) {
            auto hdr = (NMHDR *)l;
            if (hdr->idFrom == P_LIST) {
                if (hdr->code == LVN_ITEMCHANGING) {
                    auto a = (NMLISTVIEW *)l;
                    if (a->iItem >= 0 && a->iItem < (int)rows.size() && rows[a->iItem].index < 0 &&
                        (a->uNewState & LVIS_SELECTED)) {
                        SetWindowLongPtrW(h, DWLP_MSGRESULT, TRUE);
                        return TRUE;
                    }
                }
                if (hdr->code == NM_DBLCLK) {
                    auto a = (NMITEMACTIVATE *)l;
                    if (a->iItem >= 0 && a->iItem < (int)rows.size() && rows[a->iItem].index >= 0 &&
                        !(a->ptAction.x >= px(20) && a->ptAction.x < px(44))) {
                        if (library.doubleClick == 1)
                            run(false);
                        else if (library.doubleClick == 2)
                            action(P_EDIT);
                        else if (library.doubleClick == 3)
                            run(true);
                    }
                    return TRUE;
                }
                if (hdr->code == LVN_KEYDOWN) {
                    auto k = (NMLVKEYDOWN *)l;
                    if (k->wVKey == VK_RETURN)
                        run(false);
                    else if (k->wVKey == VK_F2)
                        action(P_EDIT);
                    return TRUE;
                }
                if (hdr->code == NM_CUSTOMDRAW) {
                    SetWindowLongPtrW(h, DWLP_MSGRESULT, drawList((NMLVCUSTOMDRAW *)l));
                    return TRUE;
                }
            }
        }
        if (m == WM_CLOSE) {
            SendMessageW(npp._nppHandle, NPPM_DMMHIDE, 0, (LPARAM)h);
            return TRUE;
        }
        if (m == WM_DESTROY) {
            if (boldFont) {
                DeleteObject(boldFont);
                boldFont = nullptr;
            }
            if (rowImages) {
                ImageList_Destroy(rowImages);
                rowImages = nullptr;
            }
            panel = nullptr;
        }
    } catch (const std::exception &e) {
        error(e, h);
        status(tr(L"Operation failed; see error message."));
    }
    return FALSE;
}
void show() {
    try {
        load();
        if (!panel) {
            CreateDialogParamW(instance, MAKEINTRESOURCEW(ID_PANEL), npp._nppHandle, panelProc, 0);
            if (!panel)
                throw std::runtime_error("Cannot create library panel.");
            DockedWidgetData d{};
            d.hClient = panel;
            d.pszName = L"KPairs";
            d.dlgID = 0;
            d.uMask = DWS_DF_CONT_RIGHT;
            d.pszModuleName = L"KPairs.dll";
            SendMessageW(npp._nppHandle, NPPM_DMMREGASDCKDLG, 0, (LPARAM)&d);
            SendMessageW(npp._nppHandle, NPPM_MODELESSDIALOG, MODELESSDIALOGADD, (LPARAM)panel);
        }
        SendMessageW(npp._nppHandle, NPPM_DMMSHOW, 0, (LPARAM)panel);
        refresh();
    } catch (const std::exception &e) {
        error(e);
    }
}
void safeSettings() {
    try {
        settings();
    } catch (const std::exception &e) {
        error(e);
    }
}
void safeGroups() {
    try {
        groups();
    } catch (const std::exception &e) {
        error(e);
    }
}
void safeStatistics() {
    try {
        statistics();
    } catch (const std::exception &e) {
        error(e);
    }
}
void safeGuide() {
    try {
        guide();
    } catch (const std::exception &e) {
        error(e);
    }
}
void safeAbout() {
    try {
        about();
    } catch (const std::exception &e) {
        error(e);
    }
}
#ifdef KPAIRS_INTEGRATION_TEST
#include "host_tests.h"
#endif
} // namespace
BOOL APIENTRY DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        instance = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}
extern "C" __declspec(dllexport) void setInfo(NppData data) {
    npp = data;
    INITCOMMONCONTROLSEX c{sizeof(c), ICC_LISTVIEW_CLASSES | ICC_LINK_CLASS};
    InitCommonControlsEx(&c);
}
extern "C" __declspec(dllexport) const wchar_t *getName() { return L"KPairs"; }
extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
extern "C" __declspec(dllexport) FuncItem *getFuncsArray(int *count) {
    *count = 6;
    const wchar_t *names[]{L"Show KPairs", L"Settings", L"Groups", L"Statistics", L"Flag Guide", L"About"};
    PFUNCPLUGINCMD funcs[]{show, safeSettings, safeGroups, safeStatistics, safeGuide, safeAbout};
    for (int i = 0; i < 6; ++i) {
        wcscpy_s(commands[i]._itemName, names[i]);
        commands[i]._pFunc = funcs[i];
    }
    return commands;
}
extern "C" __declspec(dllexport) void beNotified(SCNotification *n) {
    if (!n)
        return;
#ifdef KPAIRS_INTEGRATION_TEST
    if (n->nmhdr.code == NPPN_READY) {
        integrationTest();
        return;
    }
#endif
    try {
        if (n->nmhdr.code == NPPN_READY) {
            load();
            updateMenuLanguage();
        }
        if (n->nmhdr.code == NPPN_BUFFERACTIVATED || n->nmhdr.code == NPPN_FILESAVED ||
            n->nmhdr.code == NPPN_FILERENAMED)
            refresh();
        if (n->nmhdr.code == NPPN_DARKMODECHANGED) {
            if (panel) {
                theme(panel);
                InvalidateRect(GetDlgItem(panel, P_LIST), nullptr, TRUE);
            }
            for (auto h : modeless)
                theme(h);
        }
        if (n->nmhdr.code == NPPN_SHUTDOWN) {
            shuttingDown = true;
            auto windows = modeless;
            for (auto h : windows)
                if (IsWindow(h))
                    DestroyWindow(h);
            if (panel)
                SendMessageW(npp._nppHandle, NPPM_MODELESSDIALOG, MODELESSDIALOGREMOVE, (LPARAM)panel);
        }
    } catch (const std::exception &e) {
        error(e);
    }
}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
