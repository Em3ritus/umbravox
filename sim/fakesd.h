// An in-memory FAT-like card behind sdcmd::Io, shared by sim/test_sdcmd.cpp
// and sim/sdcmd_console.cpp (the stdin/stdout console the designer's Web
// Serial client is tested against), so both drive the very same fake.
#pragma once
#include "../device/sdcmd.h"
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>

// --- a fake card ----------------------------------------------------------
// FAT compares names case-insensitively, so every key is lower-cased.
inline bool eq(const char* a, const char* b) { return a && b && !strcmp(a, b); }
inline bool has(const char* a, const char* b) { return a && b && strstr(a, b) != nullptr; }

inline std::string key(const char* p) {
    std::string s(p ? p : "");
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}
inline std::string parentOf(const std::string& k) {
    const size_t i = k.rfind('/');
    return i == 0 ? std::string("/") : k.substr(0, i);
}

struct FakeSd : sdcmd::Io {
    std::map<std::string, std::vector<uint8_t>> files;
    std::set<std::string> dirs{"/"};
    bool card = true;
    int  failWriteAfter = -1;     // appends allowed before every append fails
    bool failRename = false, failMkdir = false, failOpen = false, failClose = false;
    std::set<std::string> failRenameOf;   // renames FROM these paths fail
    size_t capacity = 1u << 20;   // bytes on the card (a full card fails short)
    std::string tmpKey;
    bool tmpOpenNow = false;
    int  appends = 0;
    std::vector<std::string> notes;

    size_t used() const { size_t u = 0; for (auto& f : files) u += f.second.size(); return u; }

    bool present() override { return card; }
    bool exists(const char* p) override { const std::string k = key(p); return card && (files.count(k) || dirs.count(k)); }
    bool isDir(const char* p) override { return card && dirs.count(key(p)); }
    bool mkdir(const char* p) override {
        const std::string k = key(p);
        if (!card || failMkdir || files.count(k) || dirs.count(k) || !dirs.count(parentOf(k))) return false;
        dirs.insert(k);
        return true;
    }
    bool remove(const char* p) override {
        const std::string k = key(p);
        if (!card || !files.count(k) || (tmpOpenNow && k == tmpKey)) return false;
        files.erase(k);
        return true;
    }
    bool rmdir(const char* p) override {
        const std::string k = key(p);
        if (!card || !dirs.count(k) || k == "/") return false;
        for (auto& f : files) if (parentOf(f.first) == k) return false;
        for (auto& d : dirs)  if (d != k && parentOf(d) == k) return false;
        dirs.erase(k);
        return true;
    }
    bool rename(const char* from, const char* to) override {
        const std::string a = key(from), b = key(to);
        if (!card || failRename || failRenameOf.count(a) || !files.count(a) || files.count(b) || dirs.count(b)) return false;
        files[b] = files[a];
        files.erase(a);
        return true;
    }
    bool tmpOpen(const char* p) override {
        const std::string k = key(p);
        if (!card || failOpen || tmpOpenNow || dirs.count(k) || !dirs.count(parentOf(k))) return false;
        files[k].clear();
        tmpKey = k; tmpOpenNow = true;
        return true;
    }
    bool tmpAppend(const uint8_t* d, size_t n) override {
        if (!card || !tmpOpenNow) return false;
        if (failWriteAfter >= 0 && appends >= failWriteAfter) return false;
        if (used() + n > capacity) {               // a full card: a short write
            const size_t room = capacity - used();
            files[tmpKey].insert(files[tmpKey].end(), d, d + room);
            return false;
        }
        appends++;
        files[tmpKey].insert(files[tmpKey].end(), d, d + n);
        return true;
    }
    long tmpRead(uint32_t off, uint8_t* buf, size_t n) override {
        if (!card || !tmpOpenNow) return -1;
        const auto& f = files[tmpKey];
        if (off > f.size()) return -1;
        const size_t k = f.size() - off < n ? f.size() - off : n;
        memcpy(buf, f.data() + off, k);
        return (long)k;
    }
    bool tmpClose() override {
        if (!tmpOpenNow) return false;
        tmpOpenNow = false;
        return card && !failClose;
    }
    bool crcOf(const char* p, uint32_t& size, uint32_t& crc) override {
        const std::string k = key(p);
        if (!card || !files.count(k)) return false;
        const auto& f = files[k];
        size = (uint32_t)f.size();
        crc = sdcmd::crc32(0, f.data(), f.size());
        return true;
    }
    void note(bool warn, const char* msg) override {
        notes.push_back(std::string(warn ? "WARN " : "INFO ") + msg);
    }

    bool has(const char* p) const { return files.count(key(p)) != 0; }
    std::vector<uint8_t> get(const char* p) const { auto it = files.find(key(p)); return it == files.end() ? std::vector<uint8_t>() : it->second; }
    void put(const char* p, const std::vector<uint8_t>& d) { files[key(p)] = d; }
};

