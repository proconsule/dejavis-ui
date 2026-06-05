#include "milkplaylistdb.h"

bool cmilkplaylistdb::Init(const std::string& dbPath) {
    try {
        mPath = dbPath;
        mDb = std::make_unique<SQLite::Database>(mPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        mDb->exec("PRAGMA foreign_keys = ON;");
        createSchema();

        getAllPlaylist();
        getAllPresets();

        return true;
    } catch (std::exception& e) {
        DEJAVISUI_LOG_ERROR("[DB Error] Init: %s",e.what());
        return false;
    }
}

void cmilkplaylistdb::createSchema() {
    SQLite::Transaction transaction(*mDb);

    mDb->exec("CREATE TABLE IF NOT EXISTS categories ("
              "id INTEGER PRIMARY KEY AUTOINCREMENT, "
              "name TEXT NOT NULL, "
              "parent_id INTEGER DEFAULT NULL, "
              "FOREIGN KEY (parent_id) REFERENCES categories(id) ON DELETE CASCADE, "
              "UNIQUE(name, parent_id)"
              ");");

    mDb->exec("INSERT OR IGNORE INTO categories (id, name, parent_id) "
              "VALUES (1, 'Uncategorized', NULL);");

    mDb->exec("CREATE TABLE IF NOT EXISTS presets ("
              "id INTEGER PRIMARY KEY AUTOINCREMENT, "
              "name TEXT NOT NULL, "
              "content TEXT NOT NULL, "
              "category_id INTEGER DEFAULT 1, "
              "rating INTEGER DEFAULT 0, "
              "is_favorite BOOLEAN DEFAULT 0, "
              "hash TEXT UNIQUE NOT NULL, "
              "FOREIGN KEY (category_id) REFERENCES categories(id) ON DELETE SET NULL"
              ");");

    mDb->exec("CREATE TABLE IF NOT EXISTS playlists ("
              "id INTEGER PRIMARY KEY AUTOINCREMENT, "
              "name TEXT NOT NULL, "
              "description TEXT, "
              "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
              ");");

    mDb->exec("CREATE TABLE IF NOT EXISTS playlist_items ("
              "id INTEGER PRIMARY KEY AUTOINCREMENT, "
              "playlist_id INTEGER NOT NULL, "
              "preset_id INTEGER NOT NULL, "
              "position INTEGER NOT NULL, "
              "FOREIGN KEY (playlist_id) REFERENCES playlists(id) ON DELETE CASCADE, "
              "FOREIGN KEY (preset_id) REFERENCES presets(id) ON DELETE CASCADE"
              ");");

    transaction.commit();
}

void cmilkplaylistdb::addCategory(std::string _name, int _parentid) {
    if (_parentid == -1) {
        SQLite::Statement query(*mDb, "INSERT OR IGNORE INTO categories (name, parent_id) VALUES (?, NULL)");
        query.bind(1, _name.c_str());
        query.exec();

    }else {
        SQLite::Statement query(*mDb, "INSERT OR IGNORE INTO categories (name, parent_id) VALUES (?, ?)");
        query.bind(1, _name.c_str());
        query.bind(2, _parentid);
        query.exec();
    }
}

void cmilkplaylistdb::deleteCategory(int _id) {
    // Protezione: non eliminiamo la categoria di default
    if (_id <= 1) return;

    try {
        SQLite::Transaction transaction(*mDb);

        SQLite::Statement updateQuery(*mDb, "UPDATE presets SET category_id = 1 WHERE category_id = ?");
        updateQuery.bind(1, _id);
        updateQuery.exec();

        SQLite::Statement deleteQuery(*mDb, "DELETE FROM categories WHERE id = ?");
        deleteQuery.bind(1, _id);

        if (deleteQuery.exec() > 0) {
            transaction.commit();
            DEJAVISUI_LOG_INFO("Categoria %d eliminata con successo", _id);
            // Qui dovresti richiamare la funzione che invia l'albero aggiornato al frontend (msgid 4019)
        }
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore durante l'eliminazione della categoria: %s", e.what());
    }
}

void cmilkplaylistdb::movePresetToCategory(int _presetid, int _categoryid) {
    try {

        SQLite::Statement query(*mDb, "UPDATE presets SET category_id = ? WHERE id = ?");


        query.bind(1, _categoryid);
        query.bind(2, _presetid);

        // Esecuzione della query
        int rowsAffected = query.exec();

        if (rowsAffected > 0) {
            DEJAVISUI_LOG_INFO("Preset %d spostato con successo nella categoria %d", _presetid, _categoryid);

            // IMPORTANTE: Dopo lo spostamento, rinfresca i dati per i client collegati
            getAllPresets();
        } else {
            DEJAVISUI_LOG_WARN("Nessun preset trovato con ID %d", _presetid);
        }
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore movePresetToCategory: %s", e.what());
    }
}

bool cmilkplaylistdb::addLocalPreset(const std::string &_path) {
    namespace fs = std::filesystem;

    try {
        std::ifstream file(_path, std::ios::in | std::ios::binary);
        if (!file.is_open()) return false;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        std::string contentHash = drogon::utils::getMd5(content);

        std::string fileNameOnly = fs::path(_path).stem().string();

        SQLite::Statement query(*mDb, "INSERT OR IGNORE INTO presets (name, content, hash) VALUES (?, ?, ?)");
        query.bind(1, fileNameOnly);
        query.bind(2, content);
        query.bind(3, contentHash);

        int rowsAffected = query.exec();

        if (rowsAffected == 0) {
            DEJAVISUI_LOG_DEBUG("Preset saltato (hash duplicato): %s", fileNameOnly.c_str());
        } else {
            DEJAVISUI_LOG_INFO("Preset aggiunto con successo: %s", fileNameOnly.c_str());
        }

        return true;

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore nel DB durante l'aggiunta di %s: %s", _path.c_str(), e.what());
        return false;
    }
}

#include <drogon/utils/Utilities.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include <string>

bool cmilkplaylistdb::addPresetFromData(const std::string& _name, std::string& _data) {
    try {

        std::string decodedContent = drogon::utils::base64Decode(_data);

        if (decodedContent.empty() && !_data.empty()) {
            DEJAVISUI_LOG_ERROR("Errore: Decodifica Base64 fallita per il preset %s", _name.c_str());
            return false;
        }


        std::string contentHash = drogon::utils::getMd5(decodedContent);


        SQLite::Statement query(*mDb, "INSERT OR IGNORE INTO presets (name, content, hash) VALUES (?, ?, ?)");

        query.bind(1, _name);          // Il nome che passi via rete
        query.bind(2, decodedContent); // Il codice .milk pulito
        query.bind(3, contentHash);    // L'identificativo univoco

        int rowsAffected = query.exec();

        if (rowsAffected == 0) {
            DEJAVISUI_LOG_DEBUG("Preset ricevuto via rete già esistente (hash match): %s", _name.c_str());
        } else {
            DEJAVISUI_LOG_INFO("Nuovo preset aggiunto via rete: %s", _name.c_str());
        }
        getAllPresets(-1);
        return true;

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore DB durante addPresetFromData (%s): %s", _name.c_str(), e.what());
        return false;
    }

}

bool cmilkplaylistdb::addPresetToPlaylist(int _presetid, int _playlistid) {
    try {
        std::string posQuery = "SELECT IFNULL(MAX(position), 0) + 1 FROM playlist_items WHERE playlist_id = ?";
        SQLite::Statement selQuery(*mDb, posQuery);
        selQuery.bind(1, _playlistid);

        int nextPosition = 1;
        if (selQuery.executeStep()) {
            nextPosition = selQuery.getColumn(0).getInt();
        }

        SQLite::Statement checkPlaylist(*mDb, "SELECT COUNT(*) FROM playlists WHERE id = ?");
        checkPlaylist.bind(1, (int)_playlistid);
        checkPlaylist.executeStep();
        int playlistExists = checkPlaylist.getColumn(0).getInt();

        SQLite::Statement checkPreset(*mDb, "SELECT COUNT(*) FROM presets WHERE id = ?");
        checkPreset.bind(1, (int)_presetid);
        checkPreset.executeStep();
        int presetExists = checkPreset.getColumn(0).getInt();

        if (playlistExists == 0) {
            DEJAVISUI_LOG_ERROR("ERRORE: La playlist con ID %u NON ESISTE nel database!", _playlistid);
        }
        if (presetExists == 0) {
            DEJAVISUI_LOG_ERROR("ERRORE: Il preset con ID %u NON ESISTE nel database!", _presetid);
        }

        if (playlistExists > 0 && presetExists > 0) {
            DEJAVISUI_LOG_INFO("Entrambi gli ID sono validi. Il problema potrebbe essere un vincolo UNIQUE o un trigger.");
        }



        DEJAVISUI_LOG_DEBUG("NEXT %d",nextPosition);
        SQLite::Statement insQuery(*mDb, "INSERT INTO playlist_items (playlist_id, preset_id,\"position\") VALUES (?, ?, ?)");
        insQuery.bind(1, _playlistid);
        insQuery.bind(2, _presetid);
        insQuery.bind(3, nextPosition);
        DEJAVISUI_LOG_DEBUG("AAAAAAAAAAA: %d %d %d",_playlistid,_presetid,nextPosition);
        int rows = insQuery.exec();

        if (rows > 0) {
            DEJAVISUI_LOG_INFO("Preset %u aggiunto alla playlist %u in posizione %d", _presetid, _playlistid, nextPosition);
            return true;
        }

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore durante addPresetToPlaylist: %s", e.what());
    }

    return false;
}

bool cmilkplaylistdb::removePresetFromPlaylist(uint32_t _presetid, uint32_t _playlistid) {
    try {
        SQLite::Transaction transaction(*mDb);

        SQLite::Statement delQuery(*mDb,
            "DELETE FROM playlist_items WHERE playlist_id = ? AND preset_id = ?");
        delQuery.bind(1, (int)_playlistid);
        delQuery.bind(2, (int)_presetid);

        int rowsDeleted = delQuery.exec();

        if (rowsDeleted > 0) {
            SQLite::Statement updateQuery(*mDb,
                "UPDATE playlist_items "
                "SET position = (SELECT COUNT(*) FROM playlist_items p2 "
                "                WHERE p2.playlist_id = playlist_items.playlist_id "
                "                AND p2.position <= playlist_items.position) "
                "WHERE playlist_id = ?");
            updateQuery.bind(1, (int)_playlistid);
            updateQuery.exec();

            transaction.commit();

            DEJAVISUI_LOG_INFO("Preset %u rimosso dalla playlist %u. Righe rimosse: %d",
                               _presetid, _playlistid, rowsDeleted);

            getAllPresets(_playlistid);

            return true;
        }

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore durante removePresetFromPlaylist: %s", e.what());
    }

    return false;
}

int cmilkplaylistdb::getRandom() {
    try {
        SQLite::Statement query(*mDb, "SELECT id FROM presets ORDER BY RANDOM() LIMIT 1");

        if (query.executeStep()) {
            return query.getColumn(0).getInt();
        } else {
            DEJAVISUI_LOG_WARN("getRandom: no preset inside db");
        }
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore getRandom: %s", e.what());
    }

    return -1;
}

int cmilkplaylistdb::nextId(int _currentid) {
    try {

        if (_currentid == -1) {
            SQLite::Statement first(*mDb, "SELECT id FROM presets ORDER BY id ASC LIMIT 1");
            if (first.executeStep()) {
                return first.getColumn(0).getInt();
            }
            DEJAVISUI_LOG_WARN("nextId: nessun preset presente nel database");
            return -1;
        }

        SQLite::Statement query(*mDb,
            "SELECT id FROM presets WHERE id > ? ORDER BY id ASC LIMIT 1");
        query.bind(1, _currentid);

        if (query.executeStep()) {
            return query.getColumn(0).getInt();
        }

        SQLite::Statement first(*mDb, "SELECT id FROM presets ORDER BY id ASC LIMIT 1");
        if (first.executeStep()) {
            return first.getColumn(0).getInt();
        }

        DEJAVISUI_LOG_WARN("nextId: nessun preset presente nel database");

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore durante nextId (ID: %d): %s", _currentid, e.what());
    }

    return -1;
}

int cmilkplaylistdb::prevId(int _currentid) {
    try {
        if (_currentid == -1) {
            SQLite::Statement first(*mDb, "SELECT id FROM presets ORDER BY id ASC LIMIT 1");
            if (first.executeStep()) {
                return first.getColumn(0).getInt();
            }
            DEJAVISUI_LOG_WARN("prevId: nessun preset presente nel database");
            return -1;
        }

        SQLite::Statement query(*mDb,
            "SELECT id FROM presets WHERE id < ? ORDER BY id DESC LIMIT 1");
        query.bind(1, _currentid);

        if (query.executeStep()) {
            return query.getColumn(0).getInt();
        }

        SQLite::Statement last(*mDb, "SELECT id FROM presets ORDER BY id DESC LIMIT 1");
        if (last.executeStep()) {
            return last.getColumn(0).getInt();
        }

        DEJAVISUI_LOG_WARN("prevId: nessun preset presente nel database");

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore durante prevId (ID: %d): %s", _currentid, e.what());
    }

    return -1;
}

std::string cmilkplaylistdb::getPresetContent(uint32_t _presetid) {
    try {

        SQLite::Statement query(*mDb, "SELECT content FROM presets WHERE id = ?");
        query.bind(1, (int)_presetid);

        if (query.executeStep()) {
            // Restituiamo il contenuto come stringa
            return query.getColumn(0).getText();
        } else {
            DEJAVISUI_LOG_WARN("Preset ID %u non trovato nel database", _presetid);
        }
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore durante getPresetContent (ID: %u): %s", _presetid, e.what());
    }

    return "";
}

std::string cmilkplaylistdb::getPresetOrigName(uint32_t _presetid) {
    try {

        SQLite::Statement query(*mDb, "SELECT name FROM presets WHERE id = ?");
        query.bind(1, (int)_presetid);

        if (query.executeStep()) {
            // Restituiamo il contenuto come stringa
            return query.getColumn(0).getText();
        } else {
            DEJAVISUI_LOG_WARN("Preset ID %u non trovato nel database", _presetid);
        }
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore durante getPresetContent (ID: %u): %s", _presetid, e.what());
    }

    return "";
}

MilkPreset cmilkplaylistdb::getPreset(uint32_t _presetid) {
    try {

        SQLite::Statement query(*mDb, "SELECT * FROM presets WHERE id = ?");
        query.bind(1, (int)_presetid);

        if (query.executeStep()) {
            return MilkPreset(query);
        } else {
            DEJAVISUI_LOG_WARN("getPreset: ID %u non trovato.", _presetid);
        }
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore durante getPreset (ID: %u): %s", _presetid, e.what());
    }

    return MilkPreset();
}

bool cmilkplaylistdb::removePreset(uint32_t _presetid) {
    try {
        SQLite::Statement query(*mDb, "DELETE FROM presets WHERE id = ?");
        query.bind(1, (int)_presetid);

        int rowsDeleted = query.exec();

        if (rowsDeleted > 0) {
            DEJAVISUI_LOG_INFO("Preset ID %u eliminato definitivamente dal database.", _presetid);
            return true;
        } else {
            DEJAVISUI_LOG_WARN("Tentativo di eliminare Preset ID %u fallito: ID non trovato.", _presetid);
        }

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore critico durante removePreset (ID: %u): %s", _presetid, e.what());
    }

    return false;
}
void cmilkplaylistdb::getAllPresets(int _playlistid) {

    _tmp_preset_list.clear();

    if (_playlistid != -1) {
        try {
            currplaylist = _playlistid;
            SQLite::Statement query(*mDb,
            "SELECT p.id, p.name, p.category_id, p.rating, p.is_favorite, p.hash "
            "FROM presets p "
            "INNER JOIN playlist_items pi ON p.id = pi.preset_id "
            "WHERE pi.playlist_id = ? "
            "ORDER BY p.name ASC");

            query.bind(1, _playlistid);

            while (query.executeStep()) {
                MilkPresetMinimal item;
                item.id          = query.getColumn(0).getInt();
                item.name        = query.getColumn(1).getText();
                item.category    = query.getColumn(2).getInt();
                item.rating      = query.getColumn(3).getInt();
                item.is_favorite = (query.getColumn(4).getInt() != 0);
                item.hash        = query.getColumn(5).getText();

                _tmp_preset_list.push_back(std::move(item));
            }

            DEJAVISUI_LOG_DEBUG("Recuperati %zu preset completi dal database", _tmp_preset_list.size());

        } catch (const std::exception& e) {
            DEJAVISUI_LOG_ERROR("Errore in getAllPresets (nuova struct): %s", e.what());
        }
    }else {
        try {
            currplaylist = _playlistid;

            SQLite::Statement query(*mDb,
                "SELECT id, name, category_id, rating, is_favorite, hash "
                "FROM presets ORDER BY name ASC");

            while (query.executeStep()) {
                MilkPresetMinimal item;

                item.id          = query.getColumn(0).getInt();
                item.name        = query.getColumn(1).getText();
                item.category    = query.getColumn(2).getInt();
                item.rating      = query.getColumn(3).getInt();

                item.is_favorite = (query.getColumn(4).getInt() != 0);
                item.hash        = query.getColumn(5).getText();

                _tmp_preset_list.push_back(std::move(item));
            }

            DEJAVISUI_LOG_DEBUG("Recuperati %zu preset completi dal database", _tmp_preset_list.size());

        } catch (const std::exception& e) {
            DEJAVISUI_LOG_ERROR("Errore in getAllPresets (nuova struct): %s", e.what());
        }
    }

}
Json::Value cmilkplaylistdb::getAllPresets_Json(int _playlistid) {
    if (_playlistid != currplaylist)getAllPresets(_playlistid);
    Json::Value list(Json::arrayValue);
    for (const auto& _preset : _tmp_preset_list) {
        Json::Value item;
        item["id"] = _preset.id;
        item["name"] = _preset.name;
        item["hash"] = _preset.hash;
        item["category"] = _preset.category;
        item["is_favorite"] = _preset.is_favorite;
        item["rating"] = _preset.rating;
        list.append(item);
    }
    return list;
}

void cmilkplaylistdb::setPresetStar(uint32_t _presetid, uint32_t stars) {
    try {
        uint32_t safeStars = (stars > 5) ? 5 : stars;

        SQLite::Statement query(*mDb, "UPDATE presets SET rating = ? WHERE id = ?");

        query.bind(1, (int)safeStars);
        query.bind(2, (int)_presetid);

        int rowsAffected = query.exec();

        if (rowsAffected > 0) {
            DEJAVISUI_LOG_INFO("Rating aggiornato: Preset %u ora ha %u stelle", _presetid, safeStars);
        } else {
            DEJAVISUI_LOG_WARN("setPresetStar: Nessun preset trovato con ID %u", _presetid);
        }

        getAllPresets(currplaylist);

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore durante l'aggiornamento del rating (ID %u): %s", _presetid, e.what());
    }
}

void cmilkplaylistdb::setPresetFavorite(uint32_t _presetid, uint32_t favorite) {
    try {

        int favoriteValue = (favorite > 0) ? 1 : 0;

        SQLite::Statement query(*mDb, "UPDATE presets SET is_favorite = ? WHERE id = ?");

        query.bind(1, favoriteValue);
        query.bind(2, (int)_presetid);

        int rowsAffected = query.exec();

        if (rowsAffected > 0) {
            DEJAVISUI_LOG_DEBUG("Stato preferito aggiornato: Preset %u -> %s",
                               _presetid, favoriteValue ? "FAVORITE" : "NORMAL");
            getAllPresets(currplaylist);
        } else {
            DEJAVISUI_LOG_WARN("setPresetFavorite: ID %u non trovato", _presetid);
        }


    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore in setPresetFavorite (ID %u): %s", _presetid, e.what());
    }
}

void cmilkplaylistdb::getAllPlaylist() {

    _tmp_plalists.clear();

    try {
        SQLite::Statement query(*mDb,
            "SELECT p.id, p.name, COUNT(pi.preset_id) as cnt "
            "FROM playlists p "
            "LEFT JOIN playlist_items pi ON p.id = pi.playlist_id "
            "GROUP BY p.id "
            "ORDER BY p.name ASC");

        while (query.executeStep()) {
            MilkPlaylistMinimal item;
            item.id = (uint32_t)query.getColumn(0).getInt();
            item.name = query.getColumn(1).getText();
            item.items_count = query.getColumn(2).getInt();

            _tmp_plalists.push_back(std::move(item));
        }

        DEJAVISUI_LOG_DEBUG("Caricate %zu playlist dal database", _tmp_plalists.size());

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore in getAllPlaylist: %s", e.what());
    }
}

Json::Value cmilkplaylistdb::getAllPlaylist_Json() {
    //std::vector<MilkPlaylistMinimal> testlist = getAllPlaylist();
    Json::Value list(Json::arrayValue);
    for (const auto& _playlist : _tmp_plalists) {
        Json::Value item;
        item["id"] = _playlist.id;
        item["name"] = _playlist.name;
        item["count"] = _playlist.items_count;
        list.append(item);
    }
    return list;
}

Json::Value cmilkplaylistdb::getCategoriesTreeJson() {

    std::map<int, Json::Value> nodes;
    std::vector<int> rootIds;

    try {

        SQLite::Statement query(*mDb, "SELECT id, name, parent_id FROM categories ORDER BY id ASC");

        while (query.executeStep()) {
            int id = query.getColumn(0).getInt();
            std::string name = query.getColumn(1).getText();

            Json::Value node;
            node["id"] = id;
            node["name"] = name;
            node["children"] = Json::arrayValue; // Inizializza l'array per i figli

            // Gestione parent_id (null o intero)
            if (query.isColumnNull(2)) {
                node["parent_id"] = Json::nullValue;
                rootIds.push_back(id); // Segnamo questo ID come radice
            } else {
                node["parent_id"] = query.getColumn(2).getInt();
            }

            nodes[id] = node;
        }

        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
            int currentId = it->first;
            if (!nodes[currentId]["parent_id"].isNull()) {
                int pId = nodes[currentId]["parent_id"].asInt();

                // Se il padre esiste nella nostra mappa, appendiamo il nodo corrente ai suoi figli
                if (nodes.find(pId) != nodes.end()) {
                    nodes[pId]["children"].append(nodes[currentId]);
                }
            }
        }

        Json::Value result(Json::arrayValue);
        for (int rId : rootIds) {
            result.append(nodes[rId]);
        }

        return result;

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore albero categorie: %s", e.what());
        return Json::arrayValue;
    }
}

void cmilkplaylistdb::createPlaylist(const std::string& name) {
    try {
        SQLite::Statement query(*mDb, "INSERT INTO playlists (name) VALUES (?)");
        query.bind(1, name);

        if (query.exec() > 0) {
            uint32_t newId = (uint32_t)mDb->getLastInsertRowid();
            DEJAVISUI_LOG_INFO("Creata nuova playlist: %s (ID: %u)", name.c_str(), newId);
            getAllPlaylist();
        }
    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore creazione playlist: %s", e.what());
    }
}

void cmilkplaylistdb::deletePlaylist(uint32_t playlistid) {
    try {
        SQLite::Transaction transaction(*mDb);

        SQLite::Statement queryItems(*mDb, "DELETE FROM playlist_items WHERE playlist_id = ?");
        queryItems.bind(1, (int)playlistid);
        queryItems.exec();
        // 2. Eliminiamo la playlist stessa
        SQLite::Statement queryPlaylist(*mDb, "DELETE FROM playlists WHERE id = ?");
        queryPlaylist.bind(1, (int)playlistid);

        if (queryPlaylist.exec() > 0) {

            transaction.commit();
            DEJAVISUI_LOG_INFO("Playlist %u eliminata con successo (e relativi collegamenti)", playlistid);
            getAllPlaylist();
        } else {
            DEJAVISUI_LOG_WARN("deletePlaylist: Nessuna playlist trovata con ID %u", playlistid);

        }

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore durante l'eliminazione della playlist %u: %s", playlistid, e.what());

    }
}
