#ifndef __APPBASEUTILS_H__
#define __APPBASEUTILS_H__

#include <functional>

#include <FS.h>
#include <WString.h>

#include <ArduinoJson.h>
#include <Misc.h>
#include <BoundedAllocator.h>

#ifndef ESPAPP_DEBUG_LEVEL
#define ESPAPP_DEBUG_LEVEL ESPZW_DEBUG_LEVEL
#endif

#ifndef ESPAPP_LOG
#define ESPAPP_LOG(...) ESPZW_LOG(__VA_ARGS__)
#endif

#if ESPAPP_DEBUG_LEVEL < 1
#define ESPAPP_DEBUGDO(...)
#define ESPAPP_DEBUG(...)
#else
#define ESPAPP_DEBUGDO(...) __VA_ARGS__
#define ESPAPP_DEBUG(...) ESPAPP_LOG(__VA_ARGS__)
#endif

#if ESPAPP_DEBUG_LEVEL < 2
#define ESPAPP_DEBUGVDO(...)
#define ESPAPP_DEBUGV(...)
#else
#define ESPAPP_DEBUGVDO(...) __VA_ARGS__
#define ESPAPP_DEBUGV(...) ESPAPP_LOG(__VA_ARGS__)
#endif

#if ESPAPP_DEBUG_LEVEL < 3
#define ESPAPP_DEBUGVVDO(...)
#define ESPAPP_DEBUGVV(...)
#else
#define ESPAPP_DEBUGVVDO(...) __VA_ARGS__
#define ESPAPP_DEBUGVV(...) ESPAPP_LOG(__VA_ARGS__)
#endif

#define JSON_MAXIMUM_PARSER_BUFFER 2048
#define JSON_MAXIMUM_PARSER_NEST   2

typedef Internals::DynamicJsonBufferBase<BoundedOneshotAllocator>
	BoundedDynamicJsonBuffer;

typedef std::function<bool(JsonObject &obj, BoundedDynamicJsonBuffer &buf)>
	JsonObjectCallback;
typedef std::function<bool(fs::File &file)>
	JsonFileCallback;

typedef enum {
  JSONMAN_OK_READONLY,
  JSONMAN_OK_UPDATED,
  JSONMAN_WARN,
  JSONMAN_WARN_NOSTOR,
  JSONMAN_WARN_UPDATEFAIL,
  JSONMAN_ERR,
  JSONMAN_ERR_MALSTOR,
  JSONMAN_ERR_PARSER,
} JsonManagerResults;

JsonManagerResults JsonManager(fs::Dir &dir, String const &name, bool create_new,
                               JsonObjectCallback const &obj_cb,
                               JsonFileCallback const &malstor_cb = JsonFileCallback(),
                               uint8_t nest_limit = JSON_MAXIMUM_PARSER_NEST,
                               size_t buf_limit = JSON_MAXIMUM_PARSER_BUFFER);

#endif //__APPBASEUTILS_H__
