// SPDX-License-Identifier: GPL-3.0-or-later
#include "model.h"
#include "localization.h"
#include <iostream>
int main() {
    int count = 0;
    auto check = [&](bool v, const char *s) {
        if (!v)
            throw std::runtime_error(s);
        ++count;
    };
    auto rejects = [&](auto f, const char *s) {
        bool failed = false;
        try {
            f();
        } catch (...) {
            failed = true;
        }
        check(failed, s);
    };
    try {
        Pair p;
        p.find = "A";
        p.replace = "B";
        p.description = "gimp python gimppy plugin";
        std::vector<std::tuple<std::wstring, int, bool>> queries{{L"gimp plugin", 0, true},
                                                                 {L"gimp plugin python", 0, true},
                                                                 {L"imp lugin ython", 0, true},
                                                                 {L"gimp3 plugin python", 0, false},
                                                                 {L"gimp plugin python", 1, true},
                                                                 {L"gimp3 plugin python", 1, true},
                                                                 {L"gimp plugin python", 2, false},
                                                                 {L"gimppy plugin", 2, true},
                                                                 {L"gimppy plugin new", 2, false},
                                                                 {L"PYTHON GIMP", 0, true},
                                                                 {L"foo", 1, false}};
        for (auto &[q, m, want] : queries)
            check(matchesQuery(p, q, m) == want, "search modes");
        p.name = "another";
        check(matchesQuery(p, L"another python", 0), "search across fields");
        Library lib;
        Group g;
        g.name = "PyGimp";
        g.mode = 0;
        g.scope = 1;
        g.ignoreCase = false;
        g.color = 123;
        g.maskEnabled = true;
        g.mask = "*.py";
        g.directoryEnabled = true;
        g.directory = "C:\\scripts\\gimp";
        g.recursive = true;
        lib.groups.push_back(g);
        p.group = g.id;
        auto e = effective(p, lib);
        check(e.mode == 0 && e.scope == 1 && !e.ignoreCase && e.color == 123 && e.maskEnabled &&
                  e.directoryEnabled,
              "inherit all options");
        check(visible(e, L"C:\\scripts\\gimp\\nested\\f.py"), "recursive mask");
        check(!visible(e, L"C:\\scripts\\gimp2\\f.py"), "directory boundary");
        check(!visible(e, L"C:\\scripts\\gimp\\f.php"), "filename mask");
        check(!visible(e, L"new 1"), "unsaved filtered document");
        check(visible(e, L"new 1", L"", 0, true), "show all");
        p.overrides = O_MODE | O_CASE | O_SCOPE | O_COLOR | O_MASK | O_DIR | O_RECURSIVE;
        p.mode = 1;
        p.scope = 0;
        p.color = -1;
        p.ignoreCase = true;
        p.recursive = false;
        e = effective(p, lib);
        check(e.mode == 1 && e.scope == 0 && e.color == -1 && e.ignoreCase && !e.maskEnabled &&
                  !e.directoryEnabled && !e.recursive,
              "override all options");
        p.overrides = 0;
        lib.groups[0].mode = 1;
        check(effective(p, lib).mode == 1, "group edits inherited");
        p = effective(p, lib);
        p.group.clear();
        check(effective(p, lib).mode == 1, "detached materialized settings");
        p.directoryEnabled = true;
        p.directory = "C:\\";
        p.maskEnabled = false;
        p.recursive = false;
        check(visible(p, L"C:\\file.txt"), "root directory");
        p.directory = "/home/krzysiu";
        p.recursive = true;
        check(visible(p, L"/home/krzysiu/sub/file.py"), "POSIX directory");
        check(!visible(p, L"/home/krzysiu2/file.py"), "POSIX boundary");
        p.directory = "C:\\missing";
        lib.pairs = {p};
        check(decode(encode(lib)).pairs[0].directory == p.directory, "nonexistent paths accepted");
        check(appendDirectory(L"C:\\Folder", L"c:/folder/") == L"C:\\Folder", "directory duplicates");
        check(appendDirectory(L"C:\\one;", L"C:\\two") == L"C:\\one;C:\\two", "append semicolon");
        check(wildcard(L"*.PHP", L"test.php"), "case-insensitive masks");
        check(wildcard(L"f?le.*", L"file.txt"), "wildcard question");
        check(!wildcard(L"*.py", L"a.php"), "wildcard negative");
        auto imported = importSimple("A>Foo\nB>Bar\nC>Foo\\>Bar\n space > value \n\\d+>X\nA>\n", 0);
        check(imported.size() == 6, "simple count");
        check(imported[2].replace == "Foo>Bar", "escaped delimiter");
        check(imported[3].find == " space " && imported[3].replace == " value ", "preserve spaces");
        check(imported[4].find == "\\d+", "regex escape preserved");
        check(imported[5].replace.empty(), "simple deletion");
        check(imported[0].mode == 0, "import mode");
        check(importSimple("A\\>B>C", 2)[0].find == "A>B", "escaped find separator");
        check(importSimple("A>B>C", 1)[0].replace == "B>C", "first separator");
        check(importSimple("A>\\\\", 2)[0].replace == "\\", "literal backslash");
        rejects([&] { importSimple("A>B\nmissing", 2); }, "invalid import");
        rejects([&] { importSimple(">B", 2); }, "empty find");
        check(extended(L"\\t\\r\\n") == L"\t\r\n", "basic extended");
        check(extended(L"\\x41\\u0042\\d067\\o104\\b01000101") == L"ABCDE", "numeric extended");
        check(extended(L"\\d+\\xZQ") == L"\\d+\\xZQ", "unknown extended preserved");
        check(extended(L"\\0").size() == 1 && extended(L"\\0")[0] == 0, "nul extended");
        check(extended(L"\\\\") == L"\\", "backslash extended");
        check(encode(decode(encode(lib))) == encode(lib), "persistence round trip");
        auto j = encode(lib);
        j["schemaVersion"] = 99;
        rejects([&] { decode(j); }, "future schema");
        j = encode(lib);
        j["pairs"].push_back(j["pairs"][0]);
        rejects([&] { decode(j); }, "duplicate pair id");
        j = encode(lib);
        j["pairs"][0]["group"] = "missing";
        rejects([&] { decode(j); }, "missing group");
        j = encode(lib);
        j["flags"][0]["text"] = "toolongtext";
        rejects([&] { decode(j); }, "flag length");
        j = encode(lib);
        j["schemaVersion"] = 1;
        check(decode(j).pairs.size() == 1, "legacy version");
        check(defaultFlags().size() == 12, "flag variants");
        Locale locale;
        locale.scan(std::filesystem::path(__FILE__).parent_path().parent_path() / L"locale");
        locale.load("pl_PL");
        check(locale.tr(L"Save") == L"Zapisz", "language fallback");
        locale.load("missing");
        check(locale.tr(L"Save") == L"Save", "English fallback");
        std::cout << count << " model assertions passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAILED after " << count << ": " << e.what() << '\n';
        return 1;
    }
}
