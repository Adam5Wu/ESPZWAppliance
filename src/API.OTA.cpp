
#include "API.OTA.hpp"

#include <Arduino.h>
#include <Units.h>

#include "AppBaseUtils.hpp"

AsyncAPIOTAWebHandler::AsyncAPIOTAWebHandler(String const &path, String const &uiurl)
  : Path(path), UIUrl(uiurl)
{
  // Do Nothing
}

bool AsyncAPIOTAWebHandler::_canHandle(AsyncWebRequest const &request) {
  if (!((HTTP_HEAD | HTTP_POST) & request.method())) return false;

  if (request.url() == Path) {
    ESPWSOTA_DEBUGVV("[%s] '%s' prefix match '%s'\n",
                     request._remoteIdent.c_str(), Path.c_str(), request.url().c_str());
    return true;
  }
  return false;
}

bool AsyncAPIOTAWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
  if (Update.isRunning()) {
    request.send_P(409, PSTR("Update already in progress"), F("text/plain"));
    return false;
  }
  if (request.method() == HTTP_HEAD) {
    request.send(204);
    return false;
  }

  // Check necessary query param
  auto hashParam = request.getQuery(F("md5"));
  if (!hashParam) {
    request.send_P(400, PSTR("Missing hash parameter"), F("text/plain"));
    return false;
  }
  ESPWSOTA_DEBUGV("[%s] Update package MD5 = '%s'\n",
                 request._remoteIdent.c_str(), hashParam->value.c_str());
  auto lenParam = request.getQuery(F("length"));
  if (!lenParam) {
    request.send_P(400, PSTR("Missing length parameter"), F("text/plain"));
    return false;
  }
  size_t uplen;
  if (!lenParam->value.toUInt(uplen)) {
    request.send_P(400, PSTR("Malformed length parameter"), F("text/plain"));
    return false;
  }
  ESPWSOTA_DEBUGV("[%s] Update package size = %d\n",
                 request._remoteIdent.c_str(), uplen);

  _updateProg = 0;
  _updateReq = &request;
  Update.setMD5(hashParam->value.begin());
  Update.begin(uplen);
  if (Update.hasError()) {
    PrintString UpdateErr;
    Update.printError(UpdateErr);
    UpdateErr.trim();
    ESPWSOTA_DEBUG("[%s] Unable to start update - %s\n",
                   request._remoteIdent.c_str(), UpdateErr.c_str());
    request.send(500, std::move(UpdateErr), F("text/plain"));
    return false;
  }
  return AsyncWebHandler::_checkContinue(request, continueHeader);
}

void AsyncAPIOTAWebHandler::_terminateRequest(AsyncWebRequest &request) {
  if (_updateReq == &request) {
    if (Update.isRunning()) {
      ESPWSOTA_DEBUG("[%s] WARNING: Aborting unsuccessful update...\n",
                     request._remoteIdent.c_str());
      Update.end();
    }
    _updateFile.clear(true);
    _updateReq = nullptr;
  }
}

bool AsyncAPIOTAWebHandler::_handleUploadData(AsyncWebRequest &request, String const& name,
    String const& filename, String const& contentType,
    size_t offset, void *buf, size_t size) {
  if (_updateReq != &request) {
    // Should not reach, but just in case
    ESPWSOTA_DEBUGV("[%s] WARNING: Ignoring conflicting upload data of %d @ %d\n",
                   request._remoteIdent.c_str(), size, offset);
    return true;
  }

  if (offset == 0) {
    // Start of file
    if (_updateFile) {
      ESPWSOTA_DEBUG("[%s] WARNING: Extra upload file '%s'...\n",
                     request._remoteIdent.c_str(), filename.c_str());
    } else {
      ESPWSOTA_DEBUG("[%s] Start receiving upload file '%s'...\n",
                     request._remoteIdent.c_str(), filename.c_str());
      _updateFile = filename;
	}
  }
  if (_updateFile != filename) {
    ESPWSOTA_DEBUGV("[%s] WARNING: Ignoring extra upload data of %d @ %d\n",
                    request._remoteIdent.c_str(), size, offset);
    return true;
  }
  ESPWSOTA_DEBUGV("[%s] Received upload data of %d @ %d\n",
                 request._remoteIdent.c_str(), size, offset);
  if (offset != _updateProg) {
    ESPWSOTA_DEBUG("[%s] WARNING: Mis-aligned upload payload (expect %d, off by %d)\n",
                   request._remoteIdent.c_str(), _updateProg, offset - _updateProg);
    return true;
  }
  // If we have encountered error, skip to the end so we can reply
  if (Update.hasError()) {
    ESPWSOTA_DEBUG("[%s] WARNING: Update process error, ignoring subsequent data...\n",
                   request._remoteIdent.c_str());
    return true;
  }
  Update.runAsync(true);
  size_t bufofs = 0;
  while (bufofs < size) {
    size_t written = Update.write(((uint8_t *)buf) + bufofs, size - bufofs);
    if (written) {
      _updateProg += written;
      bufofs += written;
    } else break;
  }
  if (Update.hasError()) {
    PrintString UpdateErr;
    Update.printError(UpdateErr);
    UpdateErr.trim();
    ESPWSOTA_DEBUG("[%s] WARNING: Update write failed - %s\n",
                   request._remoteIdent.c_str(), UpdateErr.c_str());
  }
  return true;
}

void AsyncAPIOTAWebHandler::_handleRequest(AsyncWebRequest &request) {
  if (!_updateProg) {
    request.send_P(400, PSTR("Ineffective request, nothing uploaded"), F("text/plain"));
    return;
  }
  if (!Update.isFinished()) {
    PrintString ErrMsg(F("Update incomplete, short of "));
    ErrMsg.concat(ToString(Update.remaining(), SizeUnit::BYTE, true));
    ESPWSOTA_DEBUG("[%s] %s\n", request._remoteIdent.c_str(), ErrMsg.c_str());
    request.send(400, std::move(ErrMsg), F("text/plain"));
    return;
  }
  ESPWSOTA_DEBUGV("[%s] Update completed, finalizing...\n",
                  request._remoteIdent.c_str());
  if (Update.end()) {
    ESPWSOTA_DEBUG("[%s] Successfully applied update '%s'!\n",
                   request._remoteIdent.c_str(), _updateFile.c_str());
    request.send(204);
    return;
  }
  {
    PrintString UpdateErr(F("Update failed - "));
    Update.printError(UpdateErr);
    UpdateErr.trim();
    ESPWSOTA_DEBUG("[%s] %s\n", request._remoteIdent.c_str(), UpdateErr.c_str());
    request.send(500, std::move(UpdateErr), F("text/plain"));
  }
}

