// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <set>
#include "json.hpp"
using json = nlohmann::json;
inline std::wstring wide(const std::string &s, UINT cp = CP_UTF8) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, s.data(), (int)s.size(),
                                nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid text encoding.");
    std::wstring r(n, 0);
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), r.data(), n);
    return r;
}
inline std::string bytes(const std::wstring &s, UINT cp = CP_UTF8) {
    if (s.empty())
        return {};
    BOOL used = FALSE;
    BOOL *p = (cp == CP_UTF8 ? nullptr : &used);
    DWORD flags = cp == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    int n = WideCharToMultiByte(cp, flags, s.data(), (int)s.size(), nullptr, 0, nullptr, p);
    if (!n || used)
        throw std::runtime_error(
            "Text cannot be represented in this document's encoding. Convert the document to UTF-8.");
    std::string r(n, 0);
    WideCharToMultiByte(cp, flags, s.data(), (int)s.size(), r.data(), n, nullptr, p);
    return r;
}
inline std::string newId() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id)))
        throw std::runtime_error("Cannot create an identifier.");
    wchar_t s[40]{};
    StringFromGUID2(id, s, 40);
    return bytes(s);
}
inline uint64_t timestamp() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    return (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
inline std::wstring lower(std::wstring s) {
    if (!s.empty())
        CharLowerBuffW(s.data(), (DWORD)s.size());
    return s;
}
inline std::wstring trim(std::wstring s) {
    auto a = s.find_first_not_of(L" \t\r\n");
    return a == s.npos ? L"" : s.substr(a, s.find_last_not_of(L" \t\r\n") - a + 1);
}
inline std::vector<std::wstring> split(const std::wstring &s) {
    std::vector<std::wstring> r;
    size_t a = 0, b;
    do {
        b = s.find(L';', a);
        auto t = trim(s.substr(a, b == s.npos ? b : b - a));
        if (!t.empty())
            r.push_back(t);
        a = b + 1;
    } while (b != s.npos);
    return r;
}
inline bool wildcard(std::wstring p, std::wstring s) {
    p = lower(p);
    s = lower(s);
    size_t i = 0, j = 0, star = p.npos, mark = 0;
    while (j < s.size()) {
        if (i < p.size() && (p[i] == L'?' || p[i] == s[j])) {
            ++i;
            ++j;
        } else if (i < p.size() && p[i] == L'*') {
            star = i++;
            mark = j;
        } else if (star != p.npos) {
            i = star + 1;
            j = ++mark;
        } else
            return false;
    }
    while (i < p.size() && p[i] == L'*')
        ++i;
    return i == p.size();
}
inline std::wstring normpath(std::wstring s) {
    std::replace(s.begin(), s.end(), L'/', L'\\');
    s = std::filesystem::path(s).lexically_normal().wstring();
    while (s.size() > 1 && s.back() == L'\\' && !(s.size() == 3 && s[1] == L':'))
        s.pop_back();
    return lower(s);
}
inline bool absolutePath(const std::wstring &s) {
    return !s.empty() && (s[0] == L'/' || s[0] == L'\\' || (s.size() > 2 && s[1] == L':'));
}
enum OptionBit {
    O_MODE = 1,
    O_CASE = 2,
    O_SCOPE = 4,
    O_COLOR = 8,
    O_MASK = 16,
    O_DIR = 32,
    O_RECURSIVE = 64
};
struct Options {
    int mode = 2, scope = 0, color = -1;
    bool ignoreCase = true, maskEnabled = false, directoryEnabled = false, recursive = true;
    std::string mask, directory;
};
inline void to_json(json &j, const Options &p) {
    j = {{"mode", p.mode},
         {"scope", p.scope},
         {"color", p.color},
         {"ignoreCase", p.ignoreCase},
         {"maskEnabled", p.maskEnabled},
         {"directoryEnabled", p.directoryEnabled},
         {"recursive", p.recursive},
         {"mask", p.mask},
         {"directory", p.directory}};
}
inline void from_json(const json &j, Options &p) {
    p.mode = j.value("mode", 2);
    p.scope = j.value("scope", 0);
    p.color = j.value("color", -1);
    p.ignoreCase = j.value("ignoreCase", true);
    p.maskEnabled = j.value("maskEnabled", false);
    p.directoryEnabled = j.value("directoryEnabled", false);
    p.recursive = j.value("recursive", true);
    p.mask = j.value("mask", std::string{});
    p.directory = j.value("directory", std::string{});
    if (p.mode < 0 || p.mode > 2 || p.scope < 0 || p.scope > 1 || p.color < -1 || p.color > 0xFFFFFF)
        throw std::runtime_error("Invalid settings.");
}
struct Pair : Options {
    std::string id = newId(), name, find, replace, description, group;
    unsigned overrides = 0;
    bool favourite = false;
    uint64_t created = timestamp(), executed = 0, replaced = 0;
};
inline void to_json(json &j, const Pair &p) {
    to_json(j, static_cast<const Options &>(p));
    j.update({{"id", p.id},
              {"name", p.name},
              {"find", p.find},
              {"replace", p.replace},
              {"description", p.description},
              {"group", p.group},
              {"overrides", p.overrides},
              {"favourite", p.favourite},
              {"created", p.created},
              {"executed", p.executed},
              {"replaced", p.replaced}});
}
inline void from_json(const json &j, Pair &p) {
    from_json(j, static_cast<Options &>(p));
    p.id = j.value("id", newId());
    p.name = j.value("name", std::string{});
    p.find = j.at("find").get<std::string>();
    p.replace = j.value("replace", std::string{});
    p.description = j.value("description", std::string{});
    p.group = j.value("group", std::string{});
    p.overrides = j.value("overrides", 0u);
    p.favourite = j.value("favourite", false);
    p.created = j.value("created", timestamp());
    p.executed = j.value("executed", uint64_t(0));
    p.replaced = j.value("replaced", uint64_t(0));
    for (auto *s : {&p.id, &p.name, &p.find, &p.replace, &p.description, &p.group, &p.mask, &p.directory}) {
        if (s->size() > 1000000 || s->find('\0') != s->npos)
            throw std::runtime_error("Invalid text field.");
        wide(*s);
    }
    if (p.find.empty())
        throw std::runtime_error("Find must not be empty.");
}
struct Group : Options {
    std::string id = newId(), name;
};
inline void to_json(json &j, const Group &g) {
    to_json(j, static_cast<const Options &>(g));
    j["id"] = g.id;
    j["name"] = g.name;
}
inline void from_json(const json &j, Group &g) {
    from_json(j, static_cast<Options &>(g));
    g.id = j.at("id").get<std::string>();
    g.name = j.at("name").get<std::string>();
    if (g.id.empty() || trim(wide(g.name)).empty())
        throw std::runtime_error("Group name must not be empty.");
}
struct Flag {
    std::string text;
    int bg = 0, fg = RGB(20, 20, 20);
};
inline void to_json(json &j, const Flag &f) { j = {{"text", f.text}, {"bg", f.bg}, {"fg", f.fg}}; }
inline void from_json(const json &j, Flag &f) {
    f.text = j.at("text").get<std::string>();
    f.bg = j.at("bg");
    f.fg = j.at("fg");
    if (wide(f.text).size() > 8 || f.bg < 0 || f.bg > 0xFFFFFF || f.fg < 0 || f.fg > 0xFFFFFF)
        throw std::runtime_error("Invalid flag appearance.");
}
inline std::vector<Flag> defaultFlags() {
    return {{"N", RGB(217, 108, 165)},  {"E", RGB(165, 132, 214)},  {"R", RGB(96, 155, 222)},
            {"CI", RGB(168, 156, 130)}, {"CS", RGB(217, 164, 65)},  {"F-", RGB(142, 170, 160)},
            {"F", RGB(89, 174, 134)},   {"D-", RGB(140, 168, 168)}, {"D", RGB(67, 173, 174)},
            {"D+", RGB(216, 184, 77)},  {"S", RGB(168, 186, 199)},  {"W", RGB(85, 169, 224)}};
}
struct Library {
    std::vector<Pair> pairs;
    std::vector<Group> groups;
    bool repeatFavourites = false, collectStats = true, beep = false, showAll = false, showFlags = true,
         sortDescending = false;
    int doubleClick = 1, groupDisplay = 1, searchMode = 0, sortMode = 0;
    std::string language = "auto";
    uint64_t executions = 0, replacements = 0;
    std::array<bool, 5> flagVisible{true, true, true, true, true},
        flagAlways{false, false, false, false, false};
    std::vector<Flag> flags = defaultFlags();
};
inline json encode(const Library &l) {
    return {{"schemaVersion", 2},
            {"pairs", l.pairs},
            {"groups", l.groups},
            {"repeatFavourites", l.repeatFavourites},
            {"collectStats", l.collectStats},
            {"beep", l.beep},
            {"showAll", l.showAll},
            {"showFlags", l.showFlags},
            {"doubleClick", l.doubleClick},
            {"groupDisplay", l.groupDisplay},
            {"searchMode", l.searchMode},
            {"sortMode", l.sortMode},
            {"sortDescending", l.sortDescending},
            {"language", l.language},
            {"executions", l.executions},
            {"replacements", l.replacements},
            {"flagVisible", l.flagVisible},
            {"flagAlways", l.flagAlways},
            {"flags", l.flags}};
}
inline Library decode(const json &j) {
    int version = j.at("schemaVersion");
    if (version != 1 && version != 2)
        throw std::runtime_error("Unsupported library version.");
    Library l;
    l.pairs = j.at("pairs").get<std::vector<Pair>>();
    if (version == 2) {
        l.groups = j.value("groups", std::vector<Group>{});
        l.repeatFavourites = j.value("repeatFavourites", false);
        l.collectStats = j.value("collectStats", true);
        l.beep = j.value("beep", false);
        l.showAll = j.value("showAll", false);
        l.showFlags = j.value("showFlags", true);
        l.doubleClick = j.value("doubleClick", 1);
        l.groupDisplay = j.value("groupDisplay", 1);
        l.searchMode = j.value("searchMode", 0);
        l.sortMode = j.value("sortMode", 0);
        l.sortDescending = j.value("sortDescending", false);
        l.language = j.value("language", std::string("auto"));
        l.executions = j.value("executions", uint64_t(0));
        l.replacements = j.value("replacements", uint64_t(0));
        l.flagVisible = j.value("flagVisible", l.flagVisible);
        l.flagAlways = j.value("flagAlways", l.flagAlways);
        l.flags = j.value("flags", defaultFlags());
    }
    if (l.pairs.size() > 10000 || l.groups.size() > 1000 || l.flags.size() != 12 || l.doubleClick < 0 ||
        l.doubleClick > 3 || l.groupDisplay < 0 || l.groupDisplay > 2 || l.searchMode < 0 ||
        l.searchMode > 2 || l.sortMode < 0 || l.sortMode > 1)
        throw std::runtime_error("Invalid library settings.");
    std::set<std::string> ids;
    for (auto &g : l.groups)
        if (!ids.insert(g.id).second)
            throw std::runtime_error("Duplicate group identifier.");
    ids.clear();
    for (auto &p : l.pairs) {
        if (p.id.empty() || !ids.insert(p.id).second)
            throw std::runtime_error("Duplicate pair identifier.");
        if (!p.group.empty() &&
            std::none_of(l.groups.begin(), l.groups.end(), [&](auto &g) { return g.id == p.group; }))
            throw std::runtime_error("Unknown group.");
    }
    return l;
}
inline Pair effective(const Pair &p, const Library &l) {
    Pair r = p;
    auto g = std::find_if(l.groups.begin(), l.groups.end(), [&](auto &x) { return x.id == p.group; });
    if (g == l.groups.end())
        return r;
    if (!(p.overrides & O_MODE))
        r.mode = g->mode;
    if (!(p.overrides & O_CASE))
        r.ignoreCase = g->ignoreCase;
    if (!(p.overrides & O_SCOPE))
        r.scope = g->scope;
    if (!(p.overrides & O_COLOR))
        r.color = g->color;
    if (!(p.overrides & O_MASK)) {
        r.mask = g->mask;
        r.maskEnabled = g->maskEnabled;
    }
    if (!(p.overrides & O_DIR)) {
        r.directory = g->directory;
        r.directoryEnabled = g->directoryEnabled;
    }
    if (!(p.overrides & O_RECURSIVE))
        r.recursive = g->recursive;
    return r;
}
inline std::wstring label(const Pair &p) {
    return wide(p.name.empty() ? p.find + " => " + p.replace : p.name);
}
inline bool matchesQuery(const Pair &p, const std::wstring &query, int mode) {
    if (query.empty())
        return true;
    std::vector<std::wstring> fields;
    for (auto *s : {&p.name, &p.description, &p.find, &p.replace})
        fields.push_back(lower(wide(*s)));
    auto has = [&](const std::wstring &q) {
        return std::any_of(fields.begin(), fields.end(), [&](auto &s) { return s.find(q) != s.npos; });
    };
    auto q = lower(query);
    if (mode == 2)
        return has(q);
    std::wistringstream input(q);
    std::wstring term;
    bool any = false;
    while (input >> term) {
        any = true;
        bool match = has(term);
        if (mode == 0 && !match)
            return false;
        if (mode == 1 && match)
            return true;
    }
    return mode == 0 || !any;
}
inline bool visible(const Pair &p, const std::wstring &path, const std::wstring &query = L"", int mode = 0,
                    bool showAll = false) {
    if (!matchesQuery(p, query, mode))
        return false;
    if (showAll)
        return true;
    if ((p.maskEnabled || p.directoryEnabled) && !absolutePath(path))
        return false;
    auto pos = path.find_last_of(L"/\\");
    auto file = pos == path.npos ? path : path.substr(pos + 1);
    if (p.maskEnabled) {
        bool ok = false;
        for (auto &m : split(wide(p.mask)))
            if (wildcard(m, file))
                ok = true;
        if (!ok)
            return false;
    }
    if (p.directoryEnabled) {
        bool ok = false;
        auto parent = normpath(
            pos == path.npos ? L""
                             : path.substr(0, (pos == 0 || (pos == 2 && path[1] == L':')) ? pos + 1 : pos));
        for (auto &d : split(wide(p.directory))) {
            auto root = normpath(d);
            if (root.empty())
                continue;
            auto prefix = root + (root.back() == L'\\' ? L"" : L"\\");
            if (parent == root || (p.recursive && parent.compare(0, prefix.size(), prefix) == 0))
                ok = true;
        }
        if (!ok)
            return false;
    }
    return true;
}
inline std::wstring appendDirectory(const std::wstring &original, const std::wstring &dir) {
    for (auto &d : split(original))
        if (normpath(d) == normpath(dir))
            return original;
    if (trim(original).empty())
        return dir;
    return original + (original.back() == L';' ? L"" : L";") + dir;
}
inline std::vector<Pair> importSimple(std::string text, int mode) {
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        text.erase(0, 3);
    wide(text);
    std::istringstream input(text);
    std::string line;
    size_t number = 0;
    std::vector<Pair> out;
    while (std::getline(input, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        std::string a, b;
        bool second = false;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '\\' && i + 1 < line.size() && (line[i + 1] == '>' || line[i + 1] == '\\')) {
                c = line[++i];
            } else if (c == '>' && !second) {
                second = true;
                continue;
            }
            (second ? b : a) += c;
        }
        if (!second || a.empty())
            throw std::runtime_error("Invalid simple import line: " + std::to_string(number));
        Pair p;
        p.find = a;
        p.replace = b;
        p.mode = mode;
        out.push_back(p);
        if (out.size() > 10000)
            throw std::runtime_error("Too many pairs.");
    }
    return out;
}
inline std::wstring extended(const std::wstring &s) {
    std::wstring r;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != L'\\' || i + 1 == s.size()) {
            r += s[i];
            continue;
        }
        wchar_t c = s[i + 1];
        if (c == L'n') {
            r += L'\n';
            ++i;
        } else if (c == L'r') {
            r += L'\r';
            ++i;
        } else if (c == L't') {
            r += L'\t';
            ++i;
        } else if (c == L'0') {
            r += L'\0';
            ++i;
        } else if (c == L'\\') {
            r += L'\\';
            ++i;
        } else {
            int base = 0, count = 0;
            if (c == L'x') {
                base = 16;
                count = 2;
            }
            if (c == L'u') {
                base = 16;
                count = 4;
            }
            if (c == L'd') {
                base = 10;
                count = 3;
            }
            if (c == L'o') {
                base = 8;
                count = 3;
            }
            if (c == L'b') {
                base = 2;
                count = 8;
            }
            bool ok = base && i + 1 + count < s.size();
            int value = 0;
            for (int n = 0; ok && n < count; ++n) {
                wchar_t digit = s[i + 2 + n];
                int v = digit >= L'0' && digit <= L'9'   ? digit - L'0'
                        : digit >= L'a' && digit <= L'f' ? digit - L'a' + 10
                        : digit >= L'A' && digit <= L'F' ? digit - L'A' + 10
                                                         : -1;
                if (v < 0 || v >= base)
                    ok = false;
                else
                    value = value * base + v;
            }
            if (ok) {
                r += (wchar_t)value;
                i += count + 1;
            } else
                r += s[i];
        }
    }
    return r;
}
