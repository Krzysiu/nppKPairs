// SPDX-License-Identifier: GPL-3.0-or-later
void integrationTest() {
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    std::ofstream log(std::filesystem::path(exe).parent_path() / L"test-result.txt");
    int count = 0;
    auto test = [&](bool good, const char *name) {
        if (!good)
            throw std::runtime_error(name);
        log << "PASS " << name << "\n";
        log.flush();
        ++count;
    };
    try {
        load();
        library.language = "en";
        locale.load("en");
        auto engine = [&](std::string source, std::string find, std::string replacement, std::string expected,
                          int mode = 2, bool ic = true, intptr_t a = 0, intptr_t b = -1) {
            Pair p;
            p.find = find;
            p.replace = replacement;
            p.mode = mode;
            p.ignoreCase = ic;
            auto plan = makePlan(p, source, SC_CP_UTF8, a, b < 0 ? (intptr_t)source.size() : b);
            auto output = source;
            for (auto i = plan.edits.rbegin(); i != plan.edits.rend(); ++i)
                output.replace((size_t)i->start, (size_t)(i->end - i->start), i->after);
            test(output == expected, ("engine: " + find).c_str());
        };
        engine("Foo foo", "foo", "x", "x x");
        engine("Foo foo", "foo", "x", "Foo x", 2, false);
        engine("a12 b34", "([a-z])(\\d+)", "$2-$1", "12-a 34-b");
        engine("a  \r\nb\t\r\n", "[ \\t]+$", "", "a\r\nb\r\n");
        engine("aa aa", "aa", "X", "aa X", 2, true, 3, 5);
        engine("ab", "^|$", "_", "_ab_");
        engine("ab", "(?=.)", "_", "_a_b");
        engine("ab", ".*", "X", "X");
        engine("abc", "z", "X", "abc");
        engine("a\r\nb", "\\R", "/", "a/b");
        engine("\xC5\xBC \xC5\xBC", "\xC5\xBC", "\xC4\x85", "\xC4\x85 \xC4\x85");
        engine("ab", "(?<=a)b", "X", "aX");
        engine("a.b aXb", "a.b", "$1", "$1 aXb", 0);
        engine("a\tb", "\\t", "\\n", "a\nb", 1);
        engine("A", "\\x41", "\\u0105", "\xC4\x85", 1);
        engine("A", "A", "\\n", "\\n", 0);
        engine(std::string("a\0b", 3), "\\0", "X", "aXb", 1);
        Pair bad;
        bad.find = "[";
        bool rejected = false;
        try {
            validate(bad);
        } catch (...) {
            rejected = true;
        }
        test(rejected, "invalid regex rejection");
        show();
        test(IsWindow(panel), "docked panel");
        Group g;
        g.name = "Group";
        g.mode = 0;
        Pair p;
        p.group = g.id;
        p.find = "foo";
        p.replace = "bar";
        p.description = "gimp python gimppy plugin";
        p.name = "Test favourite";
        p.favourite = true;
        Library fixture;
        fixture.language = "en";
        fixture.groups = {g};
        fixture.pairs = {p};
        commit(fixture);
        refresh();
        auto pairRows = []() {
            return std::count_if(rows.begin(), rows.end(), [](auto &r) { return r.index >= 0; });
        };
        test(pairRows() == 1, "favourites default no duplication");
        fixture.repeatFavourites = true;
        commit(fixture);
        refresh();
        test(pairRows() == 2, "optional favourite duplication");
        SetDlgItemTextW(panel, P_SEARCH, L"imp lugin ython");
        test(pairRows() == 2, "AND substring search");
        SetDlgItemTextW(panel, P_SEARCH, L"absent");
        test(rows.size() == 1 && rows[0].index == -1 && rows[0].title == L"No results",
             "nonclickable no results");
        SetDlgItemTextW(panel, P_SEARCH, L"");
        fixture.repeatFavourites = false;
        commit(fixture);
        refresh();
        test(encode(readFile(configFile)) == encode(library), "JSON persistence");
        editPair(0);
        HWND dialog = modeless.back();
        test(field(dialog, E_FIND) == L"foo" && !IsWindowEnabled(GetDlgItem(dialog, E_MODE)),
             "modeless inherited pair editor");
        DestroyWindow(dialog);
        editGroup(0);
        dialog = modeless.back();
        test(choice(dialog, E_MODE) == 0, "group editor");
        DestroyWindow(dialog);
        settings();
        test(IsWindow(settingsWindow) && choice(settingsWindow, S_DOUBLE) == 1, "settings dialog");
        DestroyWindow(settingsWindow);
        flags();
        dialog = modeless.back();
        test(field(dialog, F_LABEL) == L"N", "flag editor");
        DestroyWindow(dialog);
        about();
        dialog = modeless.back();
        test(IsWindow(GetDlgItem(dialog, A_GITHUB)), "About project link");
        DestroyWindow(dialog);
        license();
        dialog = modeless.back();
        test(field(dialog, V_TEXT).find(L"GNU GENERAL PUBLIC LICENSE") != std::wstring::npos,
             "licence report");
        DestroyWindow(dialog);
        // Exercise the exact list mouse handler without input injection.
        int row = -1;
        for (size_t n = 0; n < rows.size(); ++n)
            if (rows[n].index == 0) {
                row = (int)n;
                break;
            }
        auto list = GetDlgItem(panel, P_LIST);
        RECT item;
        ListView_GetItemRect(list, row, &item, LVIR_BOUNDS);
        LPARAM point = MAKELPARAM(px(30), (item.top + item.bottom) / 2);
        SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(list, WM_LBUTTONUP, 0, point);
        test(!library.pairs[0].favourite && library.executions == 0, "heart toggles without execution");
        auto selectFirst = [&]() {
            for (size_t n = 0; n < rows.size(); ++n)
                if (rows[n].index == 0) {
                    ListView_SetItemState(list, (int)n, LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);
                    break;
                }
        };
        selectFirst();
        auto active = editor();
        sci(active, SCI_SETCODEPAGE, SC_CP_UTF8);
        sci(active, SCI_SETTEXT, 0, (LPARAM) "keep Foo foo keep");
        sci(active, SCI_EMPTYUNDOBUFFER);
        sci(active, SCI_SETSEL, 5, 12);
        run(true);
        dialog = modeless.back();
        test(range(active, 0, sci(active, SCI_GETLENGTH)) == "keep Foo foo keep" && library.executions == 0,
             "preview leaves document and statistics unchanged");
        DestroyWindow(dialog);
        run(false);
        test(range(active, 0, sci(active, SCI_GETLENGTH)) == "keep bar bar keep", "selection replacement");
        test(library.executions == 1 && library.replacements == 2 && library.pairs[0].executed == 1,
             "usage statistics");
        test(sci(active, SCI_GETSELECTIONSTART) == 5 && sci(active, SCI_GETSELECTIONEND) == 12,
             "selection preserved");
        sci(active, SCI_UNDO);
        test(range(active, 0, sci(active, SCI_GETLENGTH)) == "keep Foo foo keep", "one-step undo");
        sci(active, SCI_SETSEL, 0, 0);
        rejected = false;
        try {
            run(false);
        } catch (...) {
            rejected = true;
        }
        test(rejected, "empty selection refused");
        SendDlgItemMessageW(panel, P_SCOPE, CB_SETCURSEL, 2, 0);
        run(false);
        test(range(active, 0, sci(active, SCI_GETLENGTH)) == "keep bar bar keep",
             "temporary whole-document scope");
        test(library.pairs[0].scope == 0, "temporary scope not saved");
        run(false);
        test(library.executions == 3 && library.replacements == 4, "zero matches counts execution");
        statistics();
        dialog = modeless.back();
        test(field(dialog, V_TEXT).find(L"Top 10") != std::wstring::npos, "statistics report");
        DestroyWindow(dialog);
        auto next = library;
        next.collectStats = false;
        commit(next);
        run(false);
        test(library.executions == 3, "disabled statistics");
        // Measure final panel bounds at the user's narrow sidebar width.
        SetWindowPos(panel, nullptr, 0, 0, 230, 800, SWP_NOMOVE | SWP_NOZORDER);
        layout();
        RECT client, control;
        GetClientRect(panel, &client);
        GetWindowRect(GetDlgItem(panel, P_STATUS), &control);
        MapWindowPoints(nullptr, panel, (POINT *)&control, 2);
        test(control.bottom <= client.bottom && control.right <= client.right, "narrow panel status fits");
        locale.load("pl");
        panelLanguage();
        test(field(panel, P_ADD) == L"Dodaj", "Polish panel");
        locale.load("en");
        sci(active, SCI_CLEARALL);
        sci(active, SCI_SETSAVEPOINT);
        log << "ALL PASSED: " << count << " checks\n";
    } catch (const std::exception &e) {
        log << "FAILED after " << count << ": " << e.what() << "\n";
        auto active = editor();
        sci(active, SCI_CLEARALL);
        sci(active, SCI_SETSAVEPOINT);
    }
    log.flush();
    PostMessageW(npp._nppHandle, WM_CLOSE, 0, 0);
}
