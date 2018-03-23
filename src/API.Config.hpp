#ifndef __API_CONFIG_H__
#define __API_CONFIG_H__

#include <FS.h>

#include <Misc.h>
#include <ESPAsyncWebServer.h>

#include "AppBaseUtils.hpp"

#ifndef ESPWSCFG_DEBUG_LEVEL
#define ESPWSCFG_DEBUG_LEVEL ESPAPP_DEBUG_LEVEL
#endif

#ifndef ESPWSCFG_LOG
#define ESPWSCFG_LOG(...) ESPZW_LOG(__VA_ARGS__)
#endif

#if ESPWSCFG_DEBUG_LEVEL < 1
#define ESPWSCFG_DEBUGDO(...)
#define ESPWSCFG_DEBUG(...)
#else
#define ESPWSCFG_DEBUGDO(...) __VA_ARGS__
#define ESPWSCFG_DEBUG(...) ESPWSCFG_LOG(__VA_ARGS__)
#endif

#if ESPWSCFG_DEBUG_LEVEL < 2
#define ESPWSCFG_DEBUGVDO(...)
#define ESPWSCFG_DEBUGV(...)
#else
#define ESPWSCFG_DEBUGVDO(...) __VA_ARGS__
#define ESPWSCFG_DEBUGV(...) ESPWSCFG_LOG(__VA_ARGS__)
#endif

#if ESPWSCFG_DEBUG_LEVEL < 3
#define ESPWSCFG_DEBUGVVDO(...)
#define ESPWSCFG_DEBUGVV(...)
#else
#define ESPWSCFG_DEBUGVVDO(...) __VA_ARGS__
#define ESPWSCFG_DEBUGVV(...) ESPWSCFG_LOG(__VA_ARGS__)
#endif

class AsyncAPIConfigWebHandler: public AsyncWebHandler {
  protected:
    Dir _dir;

    void _pathNotFound(AsyncWebRequest &request);

  public:
    String const Path;
    ArRequestHandlerFunction _onGETPathNotFound;

    AsyncAPIConfigWebHandler(String const &path, Dir const& dir);

    virtual bool _canHandle(AsyncWebRequest const &request) override;
    virtual bool _checkContinue(AsyncWebRequest &request, bool continueHeader) override;

    virtual void _handleRequest(AsyncWebRequest &request) override;

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
                                   size_t offset, void *buf, size_t size) override {
      // Do not expect request upload
      return false;
    }
#endif

#endif
};

#endif //__API_CONFIG_H__
