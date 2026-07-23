#ifndef DEJAVIS_UI_SHADERTOYDB_H
#define DEJAVIS_UI_SHADERTOYDB_H

#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <SQLiteCpp/SQLiteCpp.h>
#include <json/json.h>
#include <drogon/utils/Utilities.h>
#include "../logger.h"


struct ShaderToyPresetMinimal {
    int id;
    std::string name;
    int category;
    int rating;
    bool is_favorite;
    std::string hash;

};

struct ShaderToyPreset {
    int id;
    std::string name;
    std::string content;
    int category;
    int rating;
    bool is_favorite;
    std::string hash;

    ShaderToyPreset() : id(0), rating(0), is_favorite(false) {}

    explicit ShaderToyPreset(SQLite::Statement& query) {
        id          = query.getColumn(0).getInt();
        name        = query.getColumn(1).getText();
        content     = query.getColumn(2).getText();
        category    = query.getColumn(3).getInt();
        rating      = query.getColumn(4).getInt();
        is_favorite = (query.getColumn(5).getInt() != 0);
        hash        = query.getColumn(6).getText();
    }
};




class cshadertoydb {
    public:

    struct preset_category_struct {
        int id;
        std::string name;
        int parentId;
        std::vector<std::shared_ptr<preset_category_struct>> children;
    };

        cshadertoydb() = default;
        ~cshadertoydb() = default;

        bool Init(const std::string& dbPath);

        bool addPresetFromData(const std::string& _name, std::string& _data);

        int getRandom();

        int nextId(int _currentid);
        int prevId(int _currentid);


        void addCategory(std::string _name,int _parentid);
        void deleteCategory(int _id);
        void movePresetToCategory(int _presetid, int _categoryid);


        std::string getPresetContent(uint32_t _presetid);
        std::string getPresetOrigName(uint32_t _presetid);
        ShaderToyPreset getPreset(uint32_t _presetid);
        void getAllPresets();
        Json::Value getAllPresets_Json();

        Json::Value getCategoriesTreeJson();

        void setPresetStar(uint32_t _presetid,uint32_t start);
        void setPresetFavorite(uint32_t _presetid,uint32_t favorite);

        bool removePreset(uint32_t _presetid);

    private:
        void createSchema();
        std::unique_ptr<SQLite::Database> mDb;
        std::string mPath;


        int currplaylist = -1;
        std::vector<ShaderToyPresetMinimal> _tmp_preset_list;

};

#endif //DEJAVIS_UI_SHADERTOYDB_H