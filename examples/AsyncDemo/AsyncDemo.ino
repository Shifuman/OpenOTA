// OpenOTA — exemple mode asynchrone (ESPAsyncWebServer).
//
// platformio.ini :
//   lib_deps =
//     ESP32Async/AsyncTCP
//     ESP32Async/ESPAsyncWebServer
//   build_flags = -D OPENOTA_ASYNC=1

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <OpenOTA.h>

const char* SSID = "...";
const char* PASS = "...";

AsyncWebServer server(80);

// Entree "machine en fonctionnement" — adapte a ton cablage.
constexpr uint8_t PIN_MOTEUR_ACTIF = 34;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_MOTEUR_ACTIF, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print('.'); }
  Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "text/plain", "En ligne. OTA sur /update");
  });

  // --- Marque blanche -----------------------------------------------------
  OpenOTA.setProductName("Banc moteur — noeud A");
  OpenOTA.setFirmwareVersion("2.3.1");
  OpenOTA.setAccentColor("#4ea3f0");
  OpenOTA.setLogo("<svg viewBox='0 0 24 24' fill='none' stroke='#4ea3f0' "
                  "stroke-width='2'><circle cx='12' cy='12' r='9'/>"
                  "<path d='M12 7v10M7 12h10'/></svg>");
  OpenOTA.setFooterHtml("Atelier &middot; usage interne");
  // OpenOTA.setHardwareId("BM-A-0042");   // numero de serie maison

  // --- Securite -----------------------------------------------------------
  OpenOTA.setAuth("admin", "changeme");
  OpenOTA.setRequireChecksum(true);       // refuse toute image sans MD5
  OpenOTA.setFilesystemEnabled(true);

  // --- Garde applicative --------------------------------------------------
  OpenOTA.onGuard([](OpenOTATarget t, size_t size) -> bool {
    if (digitalRead(PIN_MOTEUR_ACTIF)) {
      Serial.println("OTA refusee : moteur en marche");
      return false;
    }
    return true;
  });

  OpenOTA.onStart([](OpenOTATarget t) {
    Serial.printf("OTA demarree (%s)\n", t == OPENOTA_FIRMWARE ? "fw" : "fs");
  });
  OpenOTA.onProgress([](size_t done, size_t total) {
    Serial.printf("  %u / %u\n", (unsigned)done, (unsigned)total);
  });
  OpenOTA.onEnd([](bool ok, const String& err) {
    Serial.println(ok ? "OTA OK" : ("OTA KO: " + err).c_str());
  });

  OpenOTA.begin(&server);          // -> http://<ip>/update
  server.begin();
}

void loop() {
  OpenOTA.loop();
}
