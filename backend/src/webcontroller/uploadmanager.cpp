#include "uploadmanager.h"

#include "websocket.h"

void UploadManager::upload(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&callback)
{

    MultiPartParser fileUpload;
    if (fileUpload.parse(req) != 0 || fileUpload.getFiles().empty())
    {
        DEJAVISUI_LOG_DEBUG("ERRORE");
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("Nessun file ricevuto o formato non valido");
        callback(resp);
        return;
    }

    auto &file = fileUpload.getFiles()[0];
    const unsigned char* rawData = reinterpret_cast<const unsigned char*>(file.fileContent().data());
    int dataSize = static_cast<int>(file.fileLength());

    DEJAVISUI_LOG_DEBUG("Inizio caricamento texture su GPU...");


    CWebSocket::Renderer->m_pendingImageLoad.data_size = dataSize;
    CWebSocket::Renderer->m_pendingImageLoad.data = new unsigned char[dataSize];
    std::memcpy(CWebSocket::Renderer->m_pendingImageLoad.data, rawData, dataSize);
    CWebSocket::Renderer->m_pendingImageLoad.shouldLoad = true;

    auto resp = HttpResponse::newHttpResponse();
    resp->setBody("Immagine salvata correttamente: " + file.getFileName());
    callback(resp);
}
