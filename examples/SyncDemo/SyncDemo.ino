// OpenOTA — exemple mode synchrone (WebServer, aucune dependance externe).

#include <WiFi.h>
#include <WebServer.h>
#include <OpenOTA.h>

const char* SSID = "...";
const char* PASS = "...";

WebServer server(80);

// Remplace par tes propres controles : peripheriques presents, capteurs qui
// repondent, liaison montante etablie. Renvoyer true a l'aveugle vide le
// rollback de son interet.
bool autotestsOk() {
  return WiFi.status() == WL_CONNECTED;
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(300); }
  Serial.println(WiFi.localIP());

  server.on("/", []() { server.send(200, "text/plain", "OTA sur /update"); });

  OpenOTA.setProductName("Prototype");
  OpenOTA.setFirmwareVersion("0.9.0");
  OpenOTA.begin(&server);

  server.begin();

  // Rollback ESP32 : valider l'image seulement une fois les auto-tests passes.
  // Sans cet appel, et avec CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, le
  // bootloader repasse sur l'image precedente au prochain reset.
  if (autotestsOk()) OpenOTA.markValid();
}

void loop() {
  server.handleClient();
  OpenOTA.loop();
}
