#ifndef __API_OTA_H__
#define __API_OTA_H__

#include <Updater.h>

#include <Misc.h>
#include <ESPAsyncWebServer.h>

#include "AppBaseUtils.hpp"

#ifndef ESPWSOTA_DEBUG_LEVEL
#define ESPWSOTA_DEBUG_LEVEL ESPAPP_DEBUG_LEVEL
#endif

#ifndef ESPWSOTA_LOG
#define ESPWSOTA_LOG(...) ESPZW_LOG(__VA_ARGS__)
#endif

#if ESPWSOTA_DEBUG_LEVEL < 1
#define ESPWSOTA_DEBUGDO(...)
#define ESPWSOTA_DEBUG(...)
#else
#define ESPWSOTA_DEBUGDO(...) __VA_ARGS__
#define ESPWSOTA_DEBUG(...) ESPWSOTA_LOG(__VA_ARGS__)
#endif

#if ESPWSOTA_DEBUG_LEVEL < 2
#define ESPWSOTA_DEBUGVDO(...)
#define ESPWSOTA_DEBUGV(...)
#else
#define ESPWSOTA_DEBUGVDO(...) __VA_ARGS__
#define ESPWSOTA_DEBUGV(...) ESPWSOTA_LOG(__VA_ARGS__)
#endif

#if ESPWSOTA_DEBUG_LEVEL < 3
#define ESPWSOTA_DEBUGVVDO(...)
#define ESPWSOTA_DEBUGVV(...)
#else
#define ESPWSOTA_DEBUGVVDO(...) __VA_ARGS__
#define ESPWSOTA_DEBUGVV(...) ESPWSOTA_LOG(__VA_ARGS__)
#endif

class AsyncAPIOTAWebHandler: public AsyncWebHandler {
  protected:
    String _updateFile;
    size_t _updateProg;
    AsyncWebRequest *_updateReq;

  public:
    String const Path;
    String const UIUrl;
    AsyncAPIOTAWebHandler(String const &path, String const &uiurl);

    virtual bool _canHandle(AsyncWebRequest const &request) override;
    virtual bool _checkContinue(AsyncWebRequest &request, bool continueHeader) override;

    virtual void _handleRequest(AsyncWebRequest &request) override;
    virtual void _terminateRequest(AsyncWebRequest &request) override;

#ifdef HANDLE_REQUEST_CONTENT

    virtual bool _handleBody(AsyncWebRequest &request,
                             size_t offset, void *buf, size_t size) override {
      // Do not expect request body
      return false;
    }

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
    virtual bool _handleParamData(AsyncWebRequest &request, String const& name,
                                  size_t offset, void *buf, size_t size) override {
      // Do not expect request param
      return false;
    }
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
    virtual bool _handleUploadData(AsyncWebRequest &request, String const& name,
                                   String const& filename, String const& contentType,
                                   size_t offset, void *buf, size_t size) override;
#endif

#endif
};

#endif //__API_CONFIG_H__
