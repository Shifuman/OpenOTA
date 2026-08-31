# OpenOTA

Mise à jour OTA par navigateur pour ESP32 / ESP8266. Implémentation
indépendante, écrite de zéro, licence MIT.

L'interface s'ouvre sur l'identité de la cible — identifiant matériel, version
en cours, occupation des partitions — avant de proposer quoi que ce soit à
écrire. C'est la question qu'on se pose réellement devant un flasheur : *est-ce
que je suis en train d'écraser le bon appareil ?*

## Fonctionnalités

| | |
|---|---|
| Zone glisser-déposer | fichier déposé n'importe où sur la carte, validation `.bin` et taille vs partition avant envoi |
| Bascule firmware / système de fichiers | segmenté, chaque cible activable/désactivable côté firmware |
| Identité matérielle + version | MAC eFuse ou numéro de série maison, version applicative, puce, révision, cœurs, fréquence, flash, occupation appli, taille FS, RAM libre, SDK, date de compilation |
| Marque blanche | nom, logo (SVG en ligne ou data-URI), couleur d'accent, pied de page — tout injecté à l'exécution, la page reste statique et gzippée |
| Vérification d'intégrité | MD5 calculé dans le navigateur, passé à `Update.setMD5()`, comparé par le SDK à la fin de l'écriture |
| Progression | débit lissé et temps restant, carte de secteurs qui se remplit |
| Authentification | HTTP Basic sur toutes les routes, y compris le flux d'upload |
| Rollback ESP32 | `markValid()` / `rollback()` sur `esp_ota_ops` |
| Garde applicative | `onGuard()` refuse l'écriture selon l'état de la machine |
| Reprise après reboot | la page attend le retour de l'appareil et se recharge seule |

Page complète : **6,3 ko** gzippés en flash. Aucune dépendance externe en mode
synchrone.

## Installation

PlatformIO, `platformio.ini` :

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
    https://github.com/<toi>/OpenOTA.git
```

Mode asynchrone (recommandé si tu utilises déjà ESPAsyncWebServer) :

```ini
lib_deps =
    ESP32Async/AsyncTCP
    ESP32Async/ESPAsyncWebServer
    https://github.com/<toi>/OpenOTA.git
build_flags = -D OPENOTA_ASYNC=1
```

`OPENOTA_ASYNC` doit être un `build_flags` global, pas un `#define` dans le
`.ino` : l'en-tête est compilé séparément de ton sketch.

## Usage minimal

```cpp
#include <WebServer.h>
#include <OpenOTA.h>

WebServer server(80);

void setup() {
  // ... WiFi ...
  OpenOTA.begin(&server);   // -> http://<ip>/update
  server.begin();
}

void loop() {
  server.handleClient();
  OpenOTA.loop();           // gère le redémarrage différé
}
```

## API

```cpp
void begin(OpenOTAServer* server, const char* basePath = "/update");
void loop();

// Sécurité
void setAuth(const char* user, const char* pass);
void clearAuth();
void setEnabled(bool);                 // coupe l'endpoint sans reflasher
void setFirmwareEnabled(bool);
void setFilesystemEnabled(bool);
void setRequireChecksum(bool);         // refuse toute image sans MD5

// Comportement
void setAutoReboot(bool);              // défaut: true
void setRebootDelay(uint32_t ms);      // défaut: 800

// Marque blanche
void setProductName(const char*);
void setFirmwareVersion(const char*);
void setAccentColor(const char*);      // "#4ea3f0"
void setLogo(const char*);             // "<svg …>" ou "data:image/png;base64,…"
void setFooterHtml(const char*);
void setHardwareId(const char*);       // défaut: MAC eFuse
String hardwareId() const;

// Callbacks
void onStart(std::function<void(OpenOTATarget)>);
void onProgress(std::function<void(size_t done, size_t total)>);
void onEnd(std::function<void(bool ok, const String& err)>);
void onGuard(std::function<bool(OpenOTATarget, size_t)>);   // false = refus

// État
bool isUpdating() const;
size_t bytesWritten() const;
size_t bytesTotal() const;

// ESP32 uniquement
bool markValid();
bool rollback();
String runningPartition() const;
```

## Routes

| Méthode | Route | Rôle |
|---|---|---|
| GET | `/update` | page (HTML gzippé, PROGMEM) |
| GET | `/update/info` | JSON : identité, partitions, marque blanche |
| POST | `/update/upload?mode=fw\|fs&size=N&md5=…` | image, `multipart/form-data` |

Le préfixe est configurable via `begin(&server, "/maintenance")`.

## Rollback ESP32

Sans filet, une image qui boucle au boot te force à ressortir le câble. Avec
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` dans le sdkconfig et
`Update.setRollbackEnabled(true)`, la nouvelle image démarre en état *pending
verify* : si elle n'appelle pas `OpenOTA.markValid()` avant le prochain reset,
le bootloader repasse sur la partition précédente.

```cpp
void setup() {
  // ...
  if (autotestsOk() && wifiConnected()) OpenOTA.markValid();
}
```

## Modifier l'interface

`web/index.html` est la source. Après édition :

```bash
python3 tools/build_web.py     # régénère src/OpenOTAPage.h
```

Pour un simple changement de couleur ou de logo, pas besoin de toucher au HTML :
`setAccentColor()` / `setLogo()` suffisent.

## Notes de portage

- **ESP8266** : le chemin est écrit mais moins éprouvé que l'ESP32. Le calcul de
  la partition FS repose sur les symboles linker `_FS_start` / `_FS_end`, donc
  la taille FS suit ce que déclare le `board_build.filesystem` de PlatformIO.
- **ESPAsyncWebServer** : le fork ESP32Async est celui visé. Les versions
  récentes marquent `beginResponse_P()` déprécié au profit d'une surcharge
  `beginResponse()` PROGMEM — si tu vois un warning, c'est la seule ligne à
  changer dans `mountRoutes()`.
- **RP2040 / Pico W** : non couvert. La structure est prête (`targetCapacity()`
  et les deux `#include` d'`Updater` sont les seuls points à traiter).

## Licence

MIT. Le code ne dérive d'aucune base existante — voir la note ci-dessous.
