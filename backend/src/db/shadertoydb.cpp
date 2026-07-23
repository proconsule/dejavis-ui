#include "shadertoydb.h"

bool cshadertoydb::Init(const std::string& dbPath) {
    try {
        mPath = dbPath;
        mDb = std::make_unique<SQLite::Database>(mPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        mDb->exec("PRAGMA foreign_keys = ON;");
        createSchema();

        getAllPresets();

        return true;
    } catch (std::exception& e) {
        DEJAVISUI_LOG_ERROR("[DB Error] Init: %s",e.what());
        return false;
    }
}

void cshadertoydb::createSchema() {
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

    transaction.commit();
}

void cshadertoydb::addCategory(std::string _name, int _parentid) {
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

void cshadertoydb::deleteCategory(int _id) {
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

void cshadertoydb::movePresetToCategory(int _presetid, int _categoryid) {
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

bool cshadertoydb::addPresetFromData(const std::string& _name, std::string& _data) {
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
        getAllPresets();
        return true;

    } catch (const std::exception& e) {
        DEJAVISUI_LOG_ERROR("Errore DB durante addPresetFromData (%s): %s", _name.c_str(), e.what());
        return false;
    }

}

void cshadertoydb::getAllPresets() {

    _tmp_preset_list.clear();


    try {

        SQLite::Statement query(*mDb,
            "SELECT id, name, category_id, rating, is_favorite, hash "
            "FROM presets ORDER BY name ASC");

        while (query.executeStep()) {
            ShaderToyPresetMinimal item;

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



Json::Value cshadertoydb::getCategoriesTreeJson() {

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