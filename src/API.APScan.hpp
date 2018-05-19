#ifndef __API_APSCAN_H__
#define __API_APSCAN_H__

#include <Misc.h>
#include <ESPAsyncWebServer.h>

#ifndef ESPWSAPSCAN_DEBUG_LEVEL
#define ESPWSAPSCAN_DEBUG_LEVEL ESPAPP_DEBUG_LEVEL
#endif

#ifndef ESPWSAPSCAN_LOG
#define ESPWSAPSCAN_LOG(...) ESPZW_LOG(__VA_ARGS__)
#endif

#if ESPWSAPSCAN_DEBUG_LEVEL < 1
#define ESPWSAPSCAN_DEBUGDO(...)
#define ESPWSAPSCAN_DEBUG(...)
#else
#define ESPWSAPSCAN_DEBUGDO(...) __VA_ARGS__
#define ESPWSAPSCAN_DEBUG(...) ESPWSAPSCAN_LOG(__VA_ARGS__)
#endif

#if ESPWSAPSCAN_DEBUG_LEVEL < 2
#define ESPWSAPSCAN_DEBUGVDO(...)
#define ESPWSAPSCAN_DEBUGV(...)
#else
#define ESPWSAPSCAN_DEBUGVDO(...) __VA_ARGS__
#define ESPWSAPSCAN_DEBUGV(...) ESPWSAPSCAN_LOG(__VA_ARGS__)
#endif

#if ESPWSAPSCAN_DEBUG_LEVEL < 3
#define ESPWSAPSCAN_DEBUGVVDO(...)
#define ESPWSAPSCAN_DEBUGVV(...)
#else
#define ESPWSAPSCAN_DEBUGVVDO(...) __VA_ARGS__
#define ESPWSAPSCAN_DEBUGVV(...) ESPWSAPSCAN_LOG(__VA_ARGS__)
#endif

class AsyncAPIAPScanWebHandler: public AsyncWebHandler {
  protected:
    bool ForceScan;

  public:
    String const Path;

    AsyncAPIAPScanWebHandler(String const &path) : ForceScan(false), Path(path) {}

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

#endif //__API_APSCAN_H__
