#ifndef DEJAVIS_APP_MILKPLAYLISTDB_H
#define DEJAVIS_APP_MILKPLAYLISTDB_H


#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <SQLiteCpp/SQLiteCpp.h>
#include <json/json.h>
#include <drogon/utils/Utilities.h>
#include "../logger.h"


struct MilkPresetMinimal {
    int id;
    std::string name;
    int category;
    int rating;
    bool is_favorite;
    std::string hash;

};

struct MilkPlaylistMinimal {
    uint32_t id;
    std::string name;
    int items_count;
};

struct MilkPreset {
    int id;
    std::string name;
    std::string content;
    int category;
    int rating;
    bool is_favorite;
    std::string hash;

    MilkPreset() : id(0), rating(0), is_favorite(false) {}

    explicit MilkPreset(SQLite::Statement& query) {
        id          = query.getColumn(0).getInt();
        name        = query.getColumn(1).getText();
        content     = query.getColumn(2).getText();
        category    = query.getColumn(3).getInt();
        rating      = query.getColumn(4).getInt();
        is_favorite = (query.getColumn(5).getInt() != 0);
        hash        = query.getColumn(6).getText();
    }
};

struct preset_category_struct {
    int id;
    std::string name;
    int parentId;
    std::vector<std::shared_ptr<preset_category_struct>> children;
};


class cmilkplaylistdb {
    public:
        cmilkplaylistdb() = default;
        ~cmilkplaylistdb() = default;

        bool Init(const std::string& dbPath);

        bool addLocalPreset(const std::string& _path);
        bool addPresetFromData(const std::string& _name, std::string& _data);

        bool addPresetToPlaylist(int _presetid, int _playlistid);
        bool removePresetFromPlaylist(uint32_t _presetid, uint32_t _playlistid);

        void createPlaylist(const std::string& name);
        void deletePlaylist(uint32_t playlistid);

        void addCategory(std::string _name,int _parentid);
        void deleteCategory(int _id);
        void movePresetToCategory(int _presetid, int _categoryid);

        std::string getPresetContent(uint32_t _presetid);
        std::string getPresetOrigName(uint32_t _presetid);
        MilkPreset getPreset(uint32_t _presetid);
        void getAllPresets(int _playlistid = -1);
        Json::Value getPlaylistPresets_Json(uint32_t _playlist);
        Json::Value getAllPresets_Json(int _presetid = -1);

        Json::Value getCategoriesTreeJson();

        void getAllPlaylist();
        Json::Value getAllPlaylist_Json();

        void setPresetStar(uint32_t _presetid,uint32_t start);
        void setPresetFavorite(uint32_t _presetid,uint32_t favorite);

        bool removePreset(uint32_t _presetid);

    private:
        void createSchema();
        std::unique_ptr<SQLite::Database> mDb;
        std::string mPath;


        int currplaylist = -1;
        std::vector<MilkPlaylistMinimal> _tmp_plalists;
        std::vector<MilkPresetMinimal> _tmp_preset_list;

};


#endif //DEJAVIS_APP_MILKPLAYLISTDB_H