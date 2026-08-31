// OpenOTA — mise a jour OTA par navigateur pour ESP32 / ESP8266.
//
// Implementation independante, ecrite de zero. Aucun code tiers repris.
// Licence MIT (voir LICENSE).
//
//   Mode synchrone (defaut) :   WebServer / ESP8266WebServer
//   Mode asynchrone         :   -D OPENOTA_ASYNC=1  + ESPAsyncWebServer
//
#pragma once

#include <Arduino.h>
#include <functional>

#ifndef OPENOTA_ASYNC
#define OPENOTA_ASYNC 0
#endif

#ifndef OPENOTA_DEBUG
#define OPENOTA_DEBUG 0
#endif

// ---------------------------------------------------------------------------
// Detection de plateforme
// ---------------------------------------------------------------------------
#if defined(ESP32)
  #include <WiFi.h>
  #include <Update.h>
  #include <esp_partition.h>
  #include <esp_ota_ops.h>
  #include <esp_system.h>
  #if __has_include(<esp_chip_info.h>)
    #include <esp_chip_info.h>   // IDF >= 5
  #endif
  #if OPENOTA_ASYNC
    #include <ESPAsyncWebServer.h>
    typedef AsyncWebServer OpenOTAServer;
  #else
    #include <WebServer.h>
    typedef WebServer OpenOTAServer;
  #endif
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <Updater.h>
  #include <flash_hal.h>   // _FS_start / _FS_end
  #include <coredecls.h>   // close_all_fs()
  #if OPENOTA_ASYNC
    #include <ESPAsyncWebServer.h>
    typedef AsyncWebServer OpenOTAServer;
  #else
    #include <ESP8266WebServer.h>
    typedef ESP8266WebServer OpenOTAServer;
  #endif
#else
  #error "OpenOTA prend en charge ESP32 et ESP8266."
#endif

#if OPENOTA_DEBUG
  #define OPENOTA_LOG(fmt, ...) Serial.printf("[OpenOTA] " fmt "\n", ##__VA_ARGS__)
#else
  #define OPENOTA_LOG(fmt, ...) do {} while (0)
#endif

// Cible de l'ecriture.
enum OpenOTATarget : uint8_t {
  OPENOTA_FIRMWARE = 0,
  OPENOTA_FILESYSTEM = 1,
};

class OpenOTAClass {
 public:
  typedef std::function<void(OpenOTATarget target)> StartHandler;
  typedef std::function<void(size_t written, size_t total)> ProgressHandler;
  typedef std::function<void(bool success, const String& error)> EndHandler;
  // Retourner false refuse la mise a jour (garde applicative : batterie faible,
  // moteur en marche, version incompatible, etc.).
  typedef std::function<bool(OpenOTATarget target, size_t size)> GuardHandler;

  OpenOTAClass();

  // --- cycle de vie ------------------------------------------------------
  // basePath : prefixe des routes, "/update" par defaut.
  void begin(OpenOTAServer* server, const char* basePath = "/update");
  // A appeler dans loop() : gere le redemarrage differe.
  void loop();

  // --- securite ----------------------------------------------------------
  void setAuth(const char* username, const char* password);
  void clearAuth();
  // Coupe completement l'endpoint sans redemarrer (retourne 403).
  void setEnabled(bool enabled) { _enabled = enabled; }
  bool isEnabled() const { return _enabled; }
  // Autorise/interdit chaque cible independamment.
  void setFirmwareEnabled(bool e) { _fwEnabled = e; }
  void setFilesystemEnabled(bool e) { _fsEnabled = e; }
  // Refuse toute image dont le MD5 n'est pas fourni par le client.
  void setRequireChecksum(bool r) { _requireMd5 = r; }

  // --- comportement ------------------------------------------------------
  void setAutoReboot(bool e) { _autoReboot = e; }
  void setRebootDelay(uint32_t ms) { _rebootDelay = ms; }

  // --- marque blanche ----------------------------------------------------
  void setProductName(const char* name) { _name = name; }
  void setFirmwareVersion(const char* v) { _version = v; }
  void setAccentColor(const char* cssColor) { _accent = cssColor; }
  // SVG en ligne ("<svg ...>") ou data-URI ("data:image/png;base64,...").
  void setLogo(const char* svgOrDataUri) { _logo = svgOrDataUri; }
  void setFooterHtml(const char* html) { _footer = html; }
  // Par defaut : MAC de l'eFuse. A surcharger pour un numero de serie maison.
  void setHardwareId(const char* id) { _hwid = id; }
  String hardwareId() const;

  // --- callbacks ---------------------------------------------------------
  void onStart(StartHandler cb) { _onStart = cb; }
  void onProgress(ProgressHandler cb) { _onProgress = cb; }
  void onEnd(EndHandler cb) { _onEnd = cb; }
  void onGuard(GuardHandler cb) { _guard = cb; }

  // --- etat --------------------------------------------------------------
  bool isUpdating() const { return _active; }
  size_t bytesWritten() const { return _written; }
  size_t bytesTotal() const { return _total; }

#if defined(ESP32)
  // --- rollback (ESP32 uniquement) ---------------------------------------
  // A appeler quand la nouvelle image a prouve qu'elle fonctionne. Sans cela,
  // et si CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE est actif, le bootloader
  // revient a la partition precedente au prochain reset.
  bool markValid();
  // Repasse immediatement sur la partition precedente et redemarre.
  bool rollback();
  String runningPartition() const;
#endif

 private:
  // --- routage -----------------------------------------------------------
  void mountRoutes();
  String buildInfoJson() const;

  // --- moteur d'ecriture, commun aux deux modes de serveur ---------------
  bool writeBegin(OpenOTATarget target, size_t size, const String& md5, String& err);
  bool writeChunk(uint8_t* data, size_t len, String& err);
  bool writeFinish(String& err);
  void writeAbort();
  size_t targetCapacity(OpenOTATarget target) const;
  static String updaterError();

  OpenOTAServer* _server = nullptr;
  String _base = "/update";

  String _user, _pass;
  bool _authEnabled = false;
  bool _enabled = true;
  bool _fwEnabled = true;
  bool _fsEnabled = true;
  bool _requireMd5 = false;

  bool _autoReboot = true;
  uint32_t _rebootDelay = 800;
  uint32_t _rebootAt = 0;
  bool _rebootPending = false;

  String _name = "Mise a jour";
  String _version;
  String _accent;
  String _logo;
  String _footer;
  String _hwid;

  StartHandler _onStart;
  ProgressHandler _onProgress;
  EndHandler _onEnd;
  GuardHandler _guard;

  // Etat de la session d'ecriture en cours.
  volatile bool _active = false;
  bool _failed = false;
  String _error;
  size_t _written = 0;
  size_t _total = 0;
  OpenOTATarget _target = OPENOTA_FIRMWARE;
  uint32_t _lastProgressMs = 0;
};

extern OpenOTAClass OpenOTA;
