void ensureScratch() {
    if (!scratch) {
        scratch = (HWND)SendMessageW(npp._nppHandle, NPPM_CREATESCINTILLAHANDLE, 0, (LPARAM)npp._nppHandle);
        if (!scratch)
            throw std::runtime_error("Cannot create a preview editor.");
        ShowWindow(scratch, SW_HIDE);
        sci(scratch, SCI_SETUNDOCOLLECTION, 0);
    }
}
std::string range(HWND h, intptr_t a, intptr_t b) {
    std::string s((size_t)(b - a) + 1, 0);
    Sci_TextRangeFull r{{a, b}, s.data()};
    sci(h, SCI_GETTEXTRANGEFULL, 0, (LPARAM)&r);
    s.resize((size_t)(b - a));
    return s;
}
void putScratch(const std::string &s, int cp) {
    ensureScratch();
    sci(scratch, SCI_SETCODEPAGE, cp);
    sci(scratch, SCI_CLEARALL);
    sci(scratch, SCI_ADDTEXT, s.size(), (LPARAM)s.data());
    sci(scratch, SCI_SETSTATUS, 0);
}
struct Edit {
    intptr_t start, end;
    std::string before, after;
    intptr_t line;
};
struct Plan {
    std::vector<Edit> edits;
    std::string original;
    int cp;
    intptr_t a, b;
};
Plan makePlan(const Pair &p, const std::string &original, int cp, intptr_t a, intptr_t b) {
    if (original.size() > 32 * 1024 * 1024)
        throw std::runtime_error("This version supports documents up to 32 MiB.");
    putScratch(original, cp);
    UINT conversion = cp ? cp : GetACP();
    std::string find = bytes(p.mode == 1 ? extended(wide(p.find)) : wide(p.find), conversion),
                replacement = bytes(p.mode == 1 ? extended(wide(p.replace)) : wide(p.replace), conversion);
    if (find.empty())
        throw std::runtime_error("Find must not be empty.");
    sci(scratch, SCI_SETSEARCHFLAGS, (p.mode == 2 ? regexFlags : 0) | (p.ignoreCase ? 0 : SCFIND_MATCHCASE));
    Plan plan{{}, original, cp, a, b};
    intptr_t pos = a, end = b, delta = 0;
    auto started = GetTickCount64();
    while (pos <= end) {
        sci(scratch, SCI_SETTARGETRANGE, pos, end);
        auto found = sci(scratch, SCI_SEARCHINTARGET, find.size(), (LPARAM)find.data());
        if (found < -1)
            throw std::runtime_error("Invalid regular expression.");
        if (sci(scratch, SCI_GETSTATUS) > 0)
            throw std::runtime_error("Regex engine reported an error.");
        if (found == -1)
            break;
        auto stop = sci(scratch, SCI_GETTARGETEND);
        if (stop > end)
            break;
        Edit edit{found - delta, stop - delta, range(scratch, found, stop), {}, 0};
        edit.line = sci(scratch, SCI_LINEFROMPOSITION, found) + 1;
        bool last = (stop == end);
        auto len = sci(scratch, p.mode == 2 ? SCI_REPLACETARGETRE : SCI_REPLACETARGET, replacement.size(),
                       (LPARAM)replacement.data());
        if (len < 0 || sci(scratch, SCI_GETSTATUS) > 0)
            throw std::runtime_error("Invalid replacement expression.");
        edit.after = range(scratch, found, found + len);
        plan.edits.push_back(std::move(edit));
        auto change = len - (stop - found);
        delta += change;
        end += change;
        pos = found + len;
        if (plan.edits.size() > 100000 || sci(scratch, SCI_GETLENGTH) > 64 * 1024 * 1024 ||
            GetTickCount64() - started > 10000)
            throw std::runtime_error("Operation is too large. Narrow the selection or simplify the regex. No "
                                     "changes were applied.");
        if (last)
            break;
    }
    return plan;
}
void validate(const Pair &p) {
    json j = p;
    Pair valid = j.get<Pair>();
    makePlan(valid, "", SC_CP_UTF8, 0, 0);
}
void applyPlan(HWND h, const Plan &p) {
    if (sci(h, SCI_GETREADONLY))
        throw std::runtime_error("The document is read-only.");
    if (range(h, 0, sci(h, SCI_GETLENGTH)) != p.original)
        throw std::runtime_error("The document changed. Run the operation again.");
    if (p.edits.empty())
        return;
    auto anchor = sci(h, SCI_GETANCHOR), caret = sci(h, SCI_GETCURRENTPOS);
    intptr_t delta = 0;
    for (auto &e : p.edits)
        delta += (intptr_t)e.after.size() - (e.end - e.start);
    sci(h, SCI_BEGINUNDOACTION);
    for (auto i = p.edits.rbegin(); i != p.edits.rend(); ++i) {
        sci(h, SCI_SETTARGETRANGE, i->start, i->end);
        sci(h, SCI_REPLACETARGET, i->after.size(), (LPARAM)i->after.data());
    }
    sci(h, SCI_ENDUNDOACTION);
    sci(h, SCI_SETSEL, anchor <= caret ? p.a : p.b + delta, anchor <= caret ? p.b + delta : p.a);
}
void saveFile(const std::filesystem::path &path, const Library &l) {
    auto contents = encode(l).dump(2);
    if (contents.size() + 1 > 16 * 1024 * 1024)
        throw std::runtime_error("Library file exceeds 16 MiB.");
    auto tmp = path;
    tmp += L".tmp";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f)
        throw std::runtime_error("Cannot open library file for writing.");
    f << contents << '\n';
    f.flush();
    if (!f)
        throw std::runtime_error("Cannot write library file.");
    f.close();
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace library file. Check directory permissions.");
}
Library readFile(const std::filesystem::path &path) {
    if (std::filesystem::file_size(path) > 16 * 1024 * 1024)
        throw std::runtime_error("Library file exceeds 16 MiB.");
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot read library file.");
    json j;
    f >> j;
    return decode(j);
}
