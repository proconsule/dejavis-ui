#ifndef DEJAVIS_UI_USERCONFIG_H
#define DEJAVIS_UI_USERCONFIG_H

#include <string>
#include <json/json.h>

class cuserconfig {
public:
    void createConfig(std::string _filename);
    bool loadConfig(std::string _filename);
    bool saveConfig();
    Json::Value getConfig() {
        return user_config;
    }
private:
    Json::Value user_config;
    std::string filename;

};


#endif //DEJAVIS_UI_USERCONFIG_H