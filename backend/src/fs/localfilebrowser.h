#ifndef LOCALFILEBROWSER_H
#define LOCALFILEBROWSER_H

#include <string>
#include <vector>
#include <set>
#include <mutex>
#include <filesystem>
#include <json/json.h>

namespace fs = std::filesystem;

class CLocalFileBrowser {
public:
    CLocalFileBrowser();

    void setRootPath(const std::string& newPath);
    std::string getRootPath() const;
    
    Json::Value browse(const std::string& relativeRequest);

    Json::Value browsecurrent();

    void clearExtensions();
    void addExtension(const std::string& ext);
    Json::Value getAllDirectories(const fs::path& rootPath);

private:
    fs::path rootPath;
    std::set<std::string> allowedExtensions;
    mutable std::mutex pathMutex;

    Json::Value _dirlist;

    bool isSupported(const fs::path& p) const;
    std::string formatSize(uintmax_t size) const;

    std::string lastrelPath = "";

};

#endif