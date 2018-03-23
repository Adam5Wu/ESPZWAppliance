
#include "API.Config.hpp"

#include <Units.h>

#include "AppBaseUtils.hpp"

AsyncAPIConfigWebHandler::AsyncAPIConfigWebHandler(String const &path, Dir const& dir)
  : _dir(dir)
  , Path(AsyncPathURIWebHandler::normalizePath(path))
{
  _onGETPathNotFound = std::bind(&AsyncAPIConfigWebHandler::_pathNotFound, this, std::placeholders::_1);
}

void AsyncAPIConfigWebHandler::_pathNotFound(AsyncWebRequest &request) {
  request.send(404); // File not found
}

bool AsyncAPIConfigWebHandler::_canHandle(AsyncWebRequest const &request) {
  if (!(HTTP_GET & request.method())) return false;

  if (request.url().startsWith(Path)) {
    ESPWSCFG_DEBUGVV("[%s] '%s' prefix match '%s'\n",
                     request._remoteIdent.c_str(), Path.c_str(), request.url().c_str());
    String JsonExt(F(".json"));
    if (request.url().endsWith(JsonExt)) {
      ESPWSCFG_DEBUGVV("[%s] '%s' extension match '%s'\n",
                       request._remoteIdent.c_str(), Path.c_str(), JsonExt.c_str());
      return true;
    }
  }
  return false;
}

bool AsyncAPIConfigWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
  String subpath = request.url().substring(Path.length());

  if (!subpath || subpath.end()[-1] == '/' || _dir.isDir(subpath)) {
    ESPWSCFG_DEBUG("[%s] Cannot work on directory '%s'\n",
                   request._remoteIdent.c_str(),
                   subpath ? subpath.c_str() : "(config root dir)");
    request.send(406);
    return false;
  }
  return AsyncWebHandler::_checkContinue(request, continueHeader);
}

void AsyncAPIConfigWebHandler::_handleRequest(AsyncWebRequest &request) {
  String subpath = request.url().substring(Path.length());

  if (!_dir.exists(subpath)) {
    ESPWSCFG_DEBUG("[%s] Target file does not exist '%s'\n",
                   request._remoteIdent.c_str(), subpath.c_str());
    _onGETPathNotFound(request);
    return;
  }

  switch (JsonManagerResults JMRet = JsonManager(_dir, subpath, false,
  [&](JsonObject & obj, BoundedDynamicJsonBuffer & buf) {
    switch (request.queries()) {
        case 0: {
            String RespData;
            obj.prettyPrintTo(RespData);
            request.send(200, std::move(RespData), F("text/json"));
            return false;
          }
        case 1: {
            bool Updated = false;
            request.enumQueries([&](AsyncWebQuery const & Q) {
              if (!Q.value) {
                const JsonVariant &Val = obj[Q.name];
                if (Val.success()) {
                  String Reply;
                  Val.prettyPrintTo(Reply);
                  request.send(200, std::move(Reply), F("text/plain"));
                } else {
                  request.send(204);
                }
              } else {
                JsonVariant Val = buf.parse(Q.value, JSON_MAXIMUM_PARSER_NEST - 1);
                if (!Val.success()) {
                  ESPWSCFG_DEBUG("[%s] Malformed value '%s'\n",
                                 request._remoteIdent.c_str(), Q.value.c_str());
                  request.send_P(400, PSTR("Malformed value"), F("text/plain"));
                } else {
                  if (Val.is<char*>() && (Val.as<char*>() == nullptr)) {
                    obj.remove(Q.name);
                  } else {
                    obj[Q.name] = Val;
                  }
                  Updated = true;
                }
              }
              return false;
            });
            return Updated;
          }

        default: {
            request.send_P(400, PSTR("Batch operations not yet implemented"), F("text/plain"));
            return false;
          }
      }
  }, [&](File & file) {
    ESPWSCFG_DEBUG("[%s] Target file malformed or too big '%s' (%s)\n",
                   request._remoteIdent.c_str(), subpath.c_str(),
                   ToString(file.size(), SizeUnit::BYTE, true).c_str());
    request.send_P(500, PSTR("Target file malformed or too big"), F("text/plain"));
    return false;
  })) {
  case JSONMAN_OK_READONLY:
  case JSONMAN_ERR_MALSTOR:
    // Error response already sent
    break;

  case JSONMAN_OK_UPDATED:
    // Update operation successful
    request.send(204);
    break;

  case JSONMAN_WARN_NOSTOR:
  case JSONMAN_WARN_UPDATEFAIL:
    request.send_P(500, PSTR("Unable to update target file"), F("text/plain"));
    break;

  case JSONMAN_ERR_PARSER:
    request.send_P(500, PSTR("Internal json parser error"), F("text/plain"));
    break;

  default:
    ESPWSCFG_DEBUG("[%s] Unexpected json config manager result (%d)\n",
                   request._remoteIdent.c_str(), JMRet);
  }
}

