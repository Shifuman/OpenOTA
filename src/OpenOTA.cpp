#include "OpenOTA.h"
#include "OpenOTAPage.h"
#include <StreamString.h>

OpenOTAClass OpenOTA;

OpenOTAClass::OpenOTAClass() {}

// ---------------------------------------------------------------------------
// Identite materielle
// ---------------------------------------------------------------------------
String OpenOTAClass::hardwareId() const {
  if (_hwid.length()) return _hwid;
  char buf[18];
#if defined(ESP32)
  uint64_t mac = ESP.getEfuseMac();
  // getEfuseMac() renvoie la MAC en ordre inverse : on la remet a l'endroit.
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           (uint8_t)(mac >> 0), (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
           (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
#else
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
  return String(buf);
}

void OpenOTAClass::setAuth(const char* username, const char* password) {
  _user = username;
  _pass = password;
  _authEnabled = (_user.length() > 0 || _pass.length() > 0);
}

void OpenOTAClass::clearAuth() {
  _user = "";
  _pass = "";
  _authEnabled = false;
}

// ---------------------------------------------------------------------------
// Capacite de la partition cible
// ---------------------------------------------------------------------------
size_t OpenOTAClass::targetCapacity(OpenOTATarget target) const {
#if defined(ESP32)
  if (target == OPENOTA_FIRMWARE) {
    const esp_partition_t* p = esp_ota_get_next_update_partition(NULL);
    return p ? p->size : 0;
  }
  const esp_partition_t* p = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
  if (!p) {
    p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                 ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
  }
  return p ? p->size : 0;
#else
  if (target == OPENOTA_FIRMWARE) {
    return (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
  }
  return (size_t)&_FS_end - (size_t)&_FS_start;
#endif
}

String OpenOTAClass::updaterError() {
  StreamString s;
  Update.printError(s);
  s.trim();
  return String(s);
}

// ---------------------------------------------------------------------------
// Moteur d'ecriture
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Inspection de l'en-tete de l'image
//
// Disposition d'une image applicative Espressif :
//   0x000  esp_image_header_t      32 o   magie 0xE9, chip_id sur 2 o a 0x00C
//   0x020  esp_app_desc_t         256 o   magie 0xABCD5432
//            +0x00 magic  +0x10 version[32]  +0x30 project_name[32]
// Soit 288 octets a bufferiser avant de pouvoir decider.
// ---------------------------------------------------------------------------
#define OPENOTA_DESC_OFF     32
#define OPENOTA_DESC_MAGIC   0xABCD5432UL
#define OPENOTA_VERSION_OFF  (OPENOTA_DESC_OFF + 0x10)
#define OPENOTA_PROJECT_OFF  (OPENOTA_DESC_OFF + 0x30)

static String fixedStr(const uint8_t* p, size_t max) {
  char buf[33];
  size_t n = max < 32 ? max : 32;
  memcpy(buf, p, n);
  buf[n] = 0;
  return String(buf);
}

bool OpenOTAClass::inspectHeader(String& err) {
  // Une image de systeme de fichiers n'a pas d'en-tete applicatif.
  if (_target != OPENOTA_FIRMWARE) return true;

  if (_hdr[0] != 0xE9) {
    err = "Ce fichier n'est pas une image Espressif (magie 0x"
          + String(_hdr[0]) + " au lieu de 0xE9). Fichier .bin errone ?";
    return false;
  }

#if defined(ESP32)
  // --- modele de puce -----------------------------------------------------
  // Compare a l'en-tete de la partition qui tourne : pas de table de
  // correspondance a maintenir, la reference se decrit elle-meme.
  if (_checkChip) {
    const esp_partition_t* run = esp_ota_get_running_partition();
    uint8_t mine[16];
    if (run && esp_partition_read(run, 0, mine, sizeof(mine)) == ESP_OK) {
      uint16_t idIn = 0, idMe = 0;
      memcpy(&idIn, _hdr + 12, 2);
      memcpy(&idMe, mine + 12, 2);
      if (idIn != idMe) {
        err = "Image compilee pour une autre puce (chip_id 0x" + String(idIn)
              + ", cette carte est 0x" + String(idMe) + ").";
        return false;
      }
    }
  }

  // --- descripteur applicatif --------------------------------------------
  uint32_t magic = 0;
  memcpy(&magic, _hdr + OPENOTA_DESC_OFF, 4);
  if (magic == OPENOTA_DESC_MAGIC) {
    _inVersion = fixedStr(_hdr + OPENOTA_VERSION_OFF, 32);
    _inVariant = fixedStr(_hdr + OPENOTA_PROJECT_OFF, 32);
    OPENOTA_LOG("image entrante: %s %s", _inVariant.c_str(), _inVersion.c_str());

    if (_checkVariant) {
      String want = _variant.length() ? _variant : runningVariant();
      if (want.length() && _inVariant != want) {
        err = "Image marquee \"" + _inVariant + "\" alors que cette carte "
              "attend \"" + want + "\".";
        return false;
      }
    }
  } else if (_checkVariant) {
    err = "Descripteur applicatif absent : variante non verifiable.";
    return false;
  }
#endif
  return true;
}

String OpenOTAClass::runningVariant() const {
#if defined(ESP32)
  const esp_app_desc_t* d = esp_ota_get_app_description();
  return d ? String(d->project_name) : String();
#else
  return String();
#endif
}

String OpenOTAClass::runningVersion() const {
#if defined(ESP32)
  const esp_app_desc_t* d = esp_ota_get_app_description();
  return d ? String(d->version) : String();
#else
  return String();
#endif
}

bool OpenOTAClass::writeBegin(OpenOTATarget target, size_t size,
                              const String& md5, String& err) {
  if (!_enabled) { err = "Les mises a jour sont desactivees."; return false; }
  if (target == OPENOTA_FIRMWARE && !_fwEnabled) {
    err = "Mise a jour du firmware desactivee."; return false;
  }
  if (target == OPENOTA_FILESYSTEM && !_fsEnabled) {
    err = "Mise a jour du systeme de fichiers desactivee."; return false;
  }
  if (_active) { err = "Une ecriture est deja en cours."; return false; }
  if (_requireMd5 && md5.length() != 32) {
    err = "Empreinte MD5 requise mais absente."; return false;
  }

  size_t cap = targetCapacity(target);
  if (cap == 0) { err = "Partition cible introuvable."; return false; }
  if (size > 0 && size > cap) {
    err = "Image de " + String(size) + " o pour une partition de " +
          String(cap) + " o.";
    return false;
  }
  if (_guard && !_guard(target, size)) {
    err = "Mise a jour refusee par l'application.";
    return false;
  }

#if defined(ESP32)
  int cmd = (target == OPENOTA_FILESYSTEM) ? U_SPIFFS : U_FLASH;
  size_t reserve = (target == OPENOTA_FIRMWARE && size == 0)
                       ? UPDATE_SIZE_UNKNOWN : (size ? size : cap);
#else
  int cmd = (target == OPENOTA_FILESYSTEM) ? U_FS : U_FLASH;
  size_t reserve = size ? size : cap;
  if (target == OPENOTA_FILESYSTEM) close_all_fs();
  Update.runAsync(true);
#endif

  if (!Update.begin(reserve, cmd)) {
    err = "Initialisation impossible : " + updaterError();
    return false;
  }

  if (md5.length() == 32) {
    String lower = md5;
    lower.toLowerCase();
    if (!Update.setMD5(lower.c_str())) {
      Update.abort();
      err = "Empreinte MD5 invalide.";
      return false;
    }
  }

  _active = true;
  _failed = false;
  _error = "";
  _written = 0;
  _total = size;
  _target = target;
  _lastProgressMs = 0;
  _hdrLen = 0;
  _hdrDone = false;
  _inVariant = "";
  _inVersion = "";
  OPENOTA_LOG("debut cible=%d taille=%u", (int)target, (unsigned)size);
  if (_onStart) _onStart(target);
  return true;
}

bool OpenOTAClass::writeChunk(uint8_t* data, size_t len, String& err) {
  if (!_active || _failed) return false;

  // Accumule les 288 premiers octets, puis decide avant la premiere ecriture.
  if (!_hdrDone) {
    size_t room = HDR_LEN - _hdrLen;
    size_t take = (len < room) ? len : room;
    memcpy(_hdr + _hdrLen, data, take);
    _hdrLen += take;
    if (_hdrLen >= HDR_LEN) {
      _hdrDone = true;
      String herr;
      if (!inspectHeader(herr)) {
        err = herr;
        _failed = true;
        _error = herr;
        Update.abort();
        _active = false;
        OPENOTA_LOG("image refusee: %s", herr.c_str());
        if (_onEnd) _onEnd(false, herr);
        return false;
      }
    }
  }

  if (Update.write(data, len) != len) {
    err = "Ecriture flash interrompue : " + updaterError();
    _failed = true;
    _error = err;
    Update.abort();
    _active = false;
    return false;
  }
  _written += len;

  // Le callback est limite a ~10 Hz : appele a chaque paquet TCP il sature
  // le port serie et fait decrocher le transfert.
  if (_onProgress) {
    uint32_t now = millis();
    if (now - _lastProgressMs >= 100 || _written == _total) {
      _lastProgressMs = now;
      _onProgress(_written, _total);
    }
  }
  return true;
}

bool OpenOTAClass::writeFinish(String& err) {
  if (!_active) {
    err = _failed ? _error : "Aucune ecriture en cours.";
    return false;
  }
  _active = false;

  if (!Update.end(true)) {
    err = "Finalisation impossible : " + updaterError();
    _failed = true;
    _error = err;
    OPENOTA_LOG("echec: %s", err.c_str());
    if (_onEnd) _onEnd(false, err);
    return false;
  }

  OPENOTA_LOG("succes, %u octets ecrits", (unsigned)_written);
  if (_onEnd) _onEnd(true, String());

  if (_autoReboot) {
    _rebootPending = true;
    _rebootAt = millis() + _rebootDelay;
  }
  return true;
}

void OpenOTAClass::writeAbort() {
  if (!_active) return;
  Update.abort();
  _active = false;
  _failed = true;
  _error = "Transfert interrompu.";
  OPENOTA_LOG("abandon");
  if (_onEnd) _onEnd(false, _error);
}

// ---------------------------------------------------------------------------
// JSON d'information (alimente l'UI et la marque blanche)
// ---------------------------------------------------------------------------
static void jsonEscape(String& out, const String& in) {
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) { char b[7]; snprintf(b, 7, "\\u%04x", c); out += b; }
        else out += c;
    }
  }
}

static void kvStr(String& j, const char* k, const String& v, bool& first) {
  if (!v.length()) return;
  if (!first) j += ',';
  first = false;
  j += '"'; j += k; j += "\":\"";
  jsonEscape(j, v);
  j += '"';
}

static void kvNum(String& j, const char* k, uint64_t v, bool& first) {
  if (!first) j += ',';
  first = false;
  j += '"'; j += k; j += "\":"; j += String((unsigned long)v);
}

static void kvBool(String& j, const char* k, bool v, bool& first) {
  if (!first) j += ',';
  first = false;
  j += '"'; j += k; j += "\":"; j += (v ? "true" : "false");
}

String OpenOTAClass::buildInfoJson() const {
  String j;
  j.reserve(640);
  j = "{";
  bool first = true;

  kvStr(j, "name", _name, first);
  kvStr(j, "version", _version, first);
  kvStr(j, "hwid", hardwareId(), first);
  kvStr(j, "accent", _accent, first);
  kvStr(j, "logo", _logo, first);
  kvStr(j, "footer", _footer, first);
  kvStr(j, "built", String(__DATE__ " " __TIME__), first);

#if defined(ESP32)
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  kvStr(j, "chip", String(ESP.getChipModel()), first);
  kvNum(j, "rev", ci.revision, first);
  kvNum(j, "cores", ci.cores, first);
  kvStr(j, "sdk", String(ESP.getSdkVersion()), first);
  kvNum(j, "appUsed", ESP.getSketchSize(), first);
#else
  kvStr(j, "chip", String("ESP8266"), first);
  kvNum(j, "rev", 0, first);
  kvNum(j, "cores", 1, first);
  kvStr(j, "sdk", String(ESP.getSdkVersion()), first);
  kvNum(j, "appUsed", ESP.getSketchSize(), first);
#endif
  kvNum(j, "mhz", ESP.getCpuFreqMHz(), first);
  kvNum(j, "flash", ESP.getFlashChipSize(), first);
  kvNum(j, "heap", ESP.getFreeHeap(), first);
  kvNum(j, "appMax", targetCapacity(OPENOTA_FIRMWARE), first);
  kvNum(j, "fsSize", _fsEnabled ? targetCapacity(OPENOTA_FILESYSTEM) : 0, first);

  kvBool(j, "fwEnabled", _enabled && _fwEnabled, first);
  kvBool(j, "fsEnabled", _enabled && _fsEnabled && targetCapacity(OPENOTA_FILESYSTEM) > 0, first);
  kvBool(j, "autoReboot", _autoReboot, first);
  kvBool(j, "requireMd5", _requireMd5, first);

  j += "}";
  return j;
}

// ---------------------------------------------------------------------------
// Routes — mode ASYNCHRONE
// ---------------------------------------------------------------------------
#if OPENOTA_ASYNC

void OpenOTAClass::mountRoutes() {
  String pageUrl = _base;
  String infoUrl = _base + "/info";
  String upUrl = _base + "/upload";

  // Page.
  _server->on(pageUrl.c_str(), HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (_authEnabled && !req->authenticate(_user.c_str(), _pass.c_str()))
      return req->requestAuthentication();
    AsyncWebServerResponse* res = req->beginResponse_P(
        200, "text/html; charset=utf-8", OPENOTA_PAGE, OPENOTA_PAGE_LEN);
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  // Redirection "/update/" -> "/update" pour que BASE cote JS reste stable.
  _server->on((pageUrl + "/").c_str(), HTTP_GET,
              [pageUrl](AsyncWebServerRequest* req) { req->redirect(pageUrl); });

  // Info + marque blanche.
  _server->on(infoUrl.c_str(), HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (_authEnabled && !req->authenticate(_user.c_str(), _pass.c_str()))
      return req->requestAuthentication();
    AsyncWebServerResponse* res =
        req->beginResponse(200, "application/json", buildInfoJson());
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  // Reception de l'image.
  _server->on(
      upUrl.c_str(), HTTP_POST,
      // --- fin de requete : on repond ---
      [this](AsyncWebServerRequest* req) {
        if (_authEnabled && !req->authenticate(_user.c_str(), _pass.c_str()))
          return req->requestAuthentication();
        String err;
        bool ok = writeFinish(err);
        AsyncWebServerResponse* res = req->beginResponse(
            ok ? 200 : 400, "text/plain; charset=utf-8", ok ? "OK" : err);
        res->addHeader("Connection", "close");
        req->send(res);
      },
      // --- flux du corps multipart ---
      [this](AsyncWebServerRequest* req, const String& filename, size_t index,
             uint8_t* data, size_t len, bool final) {
        if (index == 0) {
          _failed = false;
          _error = "";
          if (_authEnabled && !req->authenticate(_user.c_str(), _pass.c_str())) {
            _failed = true;
            _error = "Authentification requise.";
            return;
          }
          OpenOTATarget target = OPENOTA_FIRMWARE;
          if (req->hasParam("mode") && req->getParam("mode")->value() == "fs")
            target = OPENOTA_FILESYSTEM;
          size_t size = req->hasParam("size")
                            ? (size_t)req->getParam("size")->value().toInt() : 0;
          String md5 = req->hasParam("md5") ? req->getParam("md5")->value() : String();

          String err;
          if (!writeBegin(target, size, md5, err)) {
            _failed = true;
            _error = err;
            return;
          }
        }
        if (_failed || !_active) return;

        String err;
        if (!writeChunk(data, len, err)) return;
        (void)final;  // writeFinish() est declenche par le handler de requete.
      });
}

#else
// ---------------------------------------------------------------------------
// Routes — mode SYNCHRONE
// ---------------------------------------------------------------------------

void OpenOTAClass::mountRoutes() {
  String pageUrl = _base;
  String infoUrl = _base + "/info";
  String upUrl = _base + "/upload";

  _server->on(pageUrl.c_str(), HTTP_GET, [this]() {
    if (_authEnabled && !_server->authenticate(_user.c_str(), _pass.c_str()))
      return _server->requestAuthentication();
    _server->sendHeader("Content-Encoding", "gzip");
    _server->sendHeader("Cache-Control", "no-store");
    _server->send_P(200, "text/html; charset=utf-8",
                    (const char*)OPENOTA_PAGE, OPENOTA_PAGE_LEN);
  });

  _server->on((pageUrl + "/").c_str(), HTTP_GET, [this, pageUrl]() {
    _server->sendHeader("Location", pageUrl);
    _server->send(302, "text/plain", "");
  });

  _server->on(infoUrl.c_str(), HTTP_GET, [this]() {
    if (_authEnabled && !_server->authenticate(_user.c_str(), _pass.c_str()))
      return _server->requestAuthentication();
    _server->sendHeader("Cache-Control", "no-store");
    _server->send(200, "application/json", buildInfoJson());
  });

  _server->on(
      upUrl.c_str(), HTTP_POST,
      // --- reponse, apres que tout le corps a ete consomme ---
      [this]() {
        String err;
        bool ok = !_failed && writeFinish(err);
        if (_failed && !err.length()) err = _error;
        _server->sendHeader("Connection", "close");
        _server->send(ok ? 200 : 400, "text/plain; charset=utf-8",
                      ok ? "OK" : err);
      },
      // --- flux d'upload ---
      [this]() {
        HTTPUpload& up = _server->upload();

        if (up.status == UPLOAD_FILE_START) {
          _failed = false;
          _error = "";
          if (_authEnabled &&
              !_server->authenticate(_user.c_str(), _pass.c_str())) {
            _failed = true;
            _error = "Authentification requise.";
            return;
          }
          OpenOTATarget target =
              (_server->arg("mode") == "fs") ? OPENOTA_FILESYSTEM : OPENOTA_FIRMWARE;
          size_t size = (size_t)_server->arg("size").toInt();
          String md5 = _server->arg("md5");

          String err;
          if (!writeBegin(target, size, md5, err)) {
            _failed = true;
            _error = err;
          }
          return;
        }

        if (_failed) return;

        if (up.status == UPLOAD_FILE_WRITE) {
          String err;
          writeChunk(up.buf, up.currentSize, err);
          if (err.length()) _error = err;
        } else if (up.status == UPLOAD_FILE_ABORTED) {
          writeAbort();
        }
        // UPLOAD_FILE_END : writeFinish() est fait dans le handler de reponse.
      });
}

#endif  // OPENOTA_ASYNC

// ---------------------------------------------------------------------------
// API publique
// ---------------------------------------------------------------------------
void OpenOTAClass::begin(OpenOTAServer* server, const char* basePath) {
  _server = server;
  _base = basePath;
  if (!_base.startsWith("/")) _base = "/" + _base;
  while (_base.length() > 1 && _base.endsWith("/"))
    _base.remove(_base.length() - 1);
  mountRoutes();
  OPENOTA_LOG("monte sur %s (async=%d)", _base.c_str(), OPENOTA_ASYNC);
}

void OpenOTAClass::loop() {
  if (_rebootPending && (int32_t)(millis() - _rebootAt) >= 0) {
    _rebootPending = false;
    OPENOTA_LOG("redemarrage");
    ESP.restart();
  }
}

// ---------------------------------------------------------------------------
// Rollback (ESP32)
// ---------------------------------------------------------------------------
#if defined(ESP32)

bool OpenOTAClass::markValid() {
  return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}

bool OpenOTAClass::rollback() {
  // Ne rend pas la main si elle reussit : la puce redemarre.
  return esp_ota_mark_app_invalid_rollback_and_reboot() == ESP_OK;
}

String OpenOTAClass::runningPartition() const {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p ? String(p->label) : String("?");
}

#endif
