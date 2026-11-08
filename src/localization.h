// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "model.h"
#include <fstream>
#include <map>
struct Locale {
    std::map<std::string, std::wstring> messages;
    std::vector<std::string> available{"auto", "en"};
    std::filesystem::path root;
    void scan(const std::filesystem::path &path) {
        root = path;
        available = {"auto", "en"};
        std::error_code ec;
        for (auto &e : std::filesystem::directory_iterator(root, ec)) {
            auto code = bytes(e.path().filename().wstring());
            if (code != "en" && code != "auto" &&
                std::filesystem::is_regular_file(e.path() / L"LC_MESSAGES" / L"KPairs.mo", ec))
                available.push_back(code);
        }
        std::sort(available.begin() + 2, available.end());
    }
    bool read(const std::filesystem::path &path) {
        std::error_code ec;
        auto size = std::filesystem::file_size(path, ec);
        if (ec || size > 8 * 1024 * 1024)
            return false;
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        std::string data((std::istreambuf_iterator<char>(f)), {});
        if (data.size() < 28 || data.size() > 8 * 1024 * 1024)
            return false;
        auto word = [&](size_t offset, bool swap) -> uint32_t {
            if (offset + 4 > data.size())
                throw std::runtime_error("Invalid translation file.");
            uint32_t v = 0;
            for (int n = 0; n < 4; ++n)
                v |= uint32_t((unsigned char)data[offset + n]) << (8 * (swap ? 3 - n : n));
            return v;
        };
        bool swap = word(0, false) == 0xde120495;
        if (word(0, swap) != 0x950412de)
            return false;
        try {
            auto count = word(8, swap), a = word(12, swap), b = word(16, swap);
            if (count > 100000)
                return false;
            std::map<std::string, std::wstring> parsed;
            for (uint32_t i = 0; i < count; ++i) {
                auto an = word(size_t(a) + 8 * i, swap), ap = word(size_t(a) + 8 * i + 4, swap),
                     bn = word(size_t(b) + 8 * i, swap), bp = word(size_t(b) + 8 * i + 4, swap);
                if (uint64_t(ap) + an > data.size() || uint64_t(bp) + bn > data.size())
                    return false;
                if (an && bn)
                    parsed[data.substr(ap, an)] = wide(data.substr(bp, bn));
            }
            messages = std::move(parsed);
            return true;
        } catch (...) {
            return false;
        }
    }
    void load(std::string code) {
        messages.clear();
        if (code == "auto") {
            wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
            GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH);
            code = bytes(name);
            std::replace(code.begin(), code.end(), '-', '_');
        }
        if (code.empty() ||
            code.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
                code.npos)
            return;
        if (read(root / wide(code) / L"LC_MESSAGES" / L"KPairs.mo"))
            return;
        auto split = code.find_first_of("_-");
        if (split != code.npos)
            read(root / wide(code.substr(0, split)) / L"LC_MESSAGES" / L"KPairs.mo");
    }
    std::wstring tr(const std::wstring &s) const {
        auto i = messages.find(bytes(s));
        return i == messages.end() ? s : i->second;
    }
};
