#ifndef DEJAVIS_UI_UPLOADMANAGER_H
#define DEJAVIS_UI_UPLOADMANAGER_H

#include <drogon/HttpController.h>

#include "backend/src/logger.h"

using namespace drogon;

class UploadManager : public HttpController<UploadManager>
{
public:

    METHOD_LIST_BEGIN
        ADD_METHOD_TO(UploadManager::upload, "/upload", drogon::Post, drogon::Options);
    METHOD_LIST_END

    void upload(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback);

};



#endif //DEJAVIS_UI_UPLOADMANAGER_H