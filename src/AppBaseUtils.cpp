
#include "AppBaseUtils.hpp"

#include <Units.h>

JsonManagerResults JsonManager(fs::Dir &dir, String const &name,
                               bool create_new_if_dne,
                               JsonObjectCallback const &obj_cb,
                               JsonFileCallback const &malstor_cb,
                               uint8_t nest_limit, size_t buf_limit) {
  JsonManagerResults Ret = JSONMAN_ERR_MALSTOR;
  fs::File JsonFile = dir.openFile(name, "r+");
  BoundedOneshotAllocator BoundedAllocator(buf_limit);
  String UpdateData;
  if (JsonFile && JsonFile.size()) {
    while (true) {
      {
        BoundedDynamicJsonBuffer jsonBuffer(BoundedAllocator, buf_limit-BoundedDynamicJsonBuffer::EmptyBlockSize);
        JsonObject& JsonObj = jsonBuffer.parseObject(JsonFile, nest_limit);
        if (JsonObj.success()) {
          if (obj_cb(JsonObj, jsonBuffer)) {
            JsonObj.prettyPrintTo(UpdateData);
            Ret = JSONMAN_OK_UPDATED;
          } else {
            Ret = JSONMAN_OK_READONLY;
          }
          break;
        }
      }

      ESPAPP_DEBUG("WARNING: error loading json file '%s'\n", name.c_str());
      if (malstor_cb && malstor_cb(JsonFile)) continue;
      break;
    }
  } else {
    if (!JsonFile) {
      ESPAPP_DEBUGV("WARNING: Unable to open json file '%s'\n", name.c_str());
      if (create_new_if_dne) {
        JsonFile = dir.openFile(name, "w");
      }
    } else {
      ESPAPP_DEBUGV("WARNING: Empty json file '%s'\n", name.c_str());
    }

    if (JsonFile) {
      Ret = JSONMAN_OK_UPDATED;
    } else {
      ESPAPP_DEBUG("WARNING: Unable to create new json file '%s'\n", name.c_str());
      Ret = JSONMAN_WARN_NOSTOR;
    }

    {
      BoundedDynamicJsonBuffer jsonBuffer(BoundedAllocator, buf_limit-BoundedDynamicJsonBuffer::EmptyBlockSize);
      JsonObject& JsonObj = jsonBuffer.createObject();
      if (JsonObj.success()) {
        if (!obj_cb(JsonObj, jsonBuffer))
          Ret = JSONMAN_OK_READONLY;
        if (Ret == JSONMAN_OK_UPDATED)
          JsonObj.prettyPrintTo(UpdateData);
      } else {
        Ret = JSONMAN_ERR_PARSER;
      }
    }
  }
  if (Ret == JSONMAN_OK_UPDATED) {
    if (!JsonFile.truncate(0)) {
      ESPAPP_DEBUG("WARNING: unable to truncate json file '%s'\n", name.c_str());
      Ret = JSONMAN_WARN_UPDATEFAIL;
    } else {
      size_t bufofs = 0;
      while (UpdateData.length() > bufofs) {
        size_t outlen = JsonFile.write(((uint8_t*)UpdateData.begin()) + bufofs, UpdateData.length() - bufofs);
        if (outlen < 0) {
          ESPAPP_DEBUG("WARNING: failed to write json file '%s'\n", name.c_str());
          Ret = JSONMAN_WARN_UPDATEFAIL;
          break;
        }
        bufofs += outlen;
      }
      ESPAPP_DEBUGV("Json data written to '%s' (%s)\n", name.c_str(),
                    ToString(bufofs, SizeUnit::BYTE, true).c_str());
    }
  }
  return Ret;
}
