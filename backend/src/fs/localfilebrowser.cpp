#include "localfilebrowser.h"
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>

#include "backend/src/logger.h"

namespace fs = std::filesystem;

static fs::path string_to_u8path(const std::string& s) {
    return fs::path(reinterpret_cast<const char8_t*>(s.data()), 
                   reinterpret_cast<const char8_t*>(s.data() + s.size()));
}

static std::string to_utf8_std(const fs::path& p) {
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

CLocalFileBrowser::CLocalFileBrowser() {
    allowedExtensions = { ".mp3", ".wav", ".flac", ".ogg", ".m4a", ".aac" };
}

void CLocalFileBrowser::clearExtensions() {
    allowedExtensions.clear();
}

void CLocalFileBrowser::addExtension(const std::string& ext) {
    allowedExtensions.insert(ext);
}

void CLocalFileBrowser::setRootPath(const std::string& newPath) {
    std::lock_guard<std::mutex> lock(pathMutex);
    rootPath = fs::absolute(string_to_u8path(newPath)).make_preferred();
    _dirlist = getAllDirectories(rootPath);
    DEJAVISUI_LOG_DEBUG("Root : %s",to_utf8_std(rootPath).c_str());
}

std::string CLocalFileBrowser::getRootPath() const {
    std::lock_guard<std::mutex> lock(pathMutex);
    return to_utf8_std(rootPath);
}

bool CLocalFileBrowser::isSupported(const fs::path& p) const {
    if (!p.has_extension()) return false;
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return allowedExtensions.contains(ext);
}

Json::Value CLocalFileBrowser::browse(const std::string& relativeRequest) {
    Json::Value rootJson;
    Json::Value entries(Json::arrayValue);

    fs::path currentRoot;
    {
        std::lock_guard<std::mutex> lock(pathMutex);
        currentRoot = rootPath;
    }

    try {

        std::string relReq = relativeRequest;
        relReq.erase(0, relReq.find_first_not_of("\\/"));
        if (relReq.find_last_not_of("\\/") != std::string::npos) {
            relReq.erase(relReq.find_last_not_of("\\/") + 1);
        }

        fs::path targetPath = currentRoot;
        if (!relReq.empty()) {
            targetPath /= string_to_u8path(relReq);
        }
        targetPath = targetPath.make_preferred();

        std::error_code ec;
        if (!fs::exists(targetPath, ec)) {
            rootJson["status"] = "error";
            rootJson["message"] = "Path inesistente: " + to_utf8_std(targetPath);
            return rootJson;
        }

        struct Entry {
            std::string name;
            bool isDir;
            uintmax_t size;
        };
        std::vector<Entry> tempEntries;

        for (const auto& entry : fs::directory_iterator(targetPath, ec)) {
            if (ec) break;
            bool isDir = entry.is_directory();
            if (isDir || isSupported(entry.path())) {
                tempEntries.push_back({
                    to_utf8_std(entry.path().filename()),
                    isDir,
                    isDir ? 0 : entry.file_size(ec)
                });
            }
        }

        std::sort(tempEntries.begin(), tempEntries.end(), [](const Entry& a, const Entry& b) {
            if (a.isDir != b.isDir) return a.isDir;
            std::string aL = a.name; std::string bL = b.name;
            std::transform(aL.begin(), aL.end(), aL.begin(), ::tolower);
            std::transform(bL.begin(), bL.end(), bL.begin(), ::tolower);
            return aL < bL;
        });

        for (const auto& e : tempEntries) {
            Json::Value item;
            item["name"] = e.name;
            item["type"] = e.isDir ? "directory" : "file";
            item["path"] = relReq.empty() ? e.name : relReq + "/" + e.name;
            if (!e.isDir) item["size"] = formatSize(e.size);
            entries.append(item);
        }

        rootJson["entries"] = entries;

        rootJson["status"] = "success";
        rootJson["entries"] = entries;
		rootJson["absPath"] = to_utf8_std(currentRoot);
        rootJson["relReq"] = relativeRequest;


    } catch (const std::exception& e) {
        rootJson["status"] = "error";
        rootJson["message"] = e.what();
    }

    return rootJson;
}

std::string CLocalFileBrowser::formatSize(uintmax_t size) const {
    double s = static_cast<double>(size);
    const char* units[] = {"B", "KB", "MB", "GB"};
    int i = 0;
    while (s >= 1024 && i < 3) { s /= 1024; i++; }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f %s", s, units[i]);
    return std::string(buf);
}

Json::Value CLocalFileBrowser::getAllDirectories(const fs::path& rootPath) {
    Json::Value dirList(Json::arrayValue);
    std::error_code ec;

    Json::Value rootEntry;
    rootEntry["name"] = "Root";
    rootEntry["isDirectory"] = true;
    rootEntry["path"] = "/";
    dirList.append(rootEntry);

    for (const auto& entry : fs::recursive_directory_iterator(rootPath, ec)) {
        if (ec) continue;

        if (entry.is_directory()) {
            Json::Value item;

            std::string relPath = "/" + fs::relative(entry.path(), rootPath).generic_string();

            item["name"] = entry.path().filename().string();
            item["isDirectory"] = true;
            item["path"] = relPath;
            item["updatedAt"] = "";

            dirList.append(item);
        }
    }
    return dirList;
}
