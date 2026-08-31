// OpenOTA — verrouiller la cible d'une mise a jour.
//
// Trois couches independantes, de la plus universelle a la plus specifique.

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <OpenOTA.h>

WebServer server(80);
Preferences prefs;

// Numero de serie de l'unite, colle sur le boitier.
// Grave en NVS a la production, ou derive de la MAC.
String serialNumber() {
  uint64_t mac = ESP.getEfuseMac();
  char b[16];
  snprintf(b, sizeof(b), "BM-%04X", (uint16_t)(mac >> 32));
  return String(b);
}

void setup() {
  Serial.begin(115200);
  prefs.begin("ota", false);   // drapeau ota_armed, persistant en NVS

  WiFi.mode(WIFI_STA);
  WiFi.begin("ssid", "pass");
  while (WiFi.status() != WL_CONNECTED) delay(300);

  String sn = serialNumber();

  // --- Couche 1 : adressage ------------------------------------------------
  // Un nom mDNS par unite. On tape banc-bm-0a3f.local, pas une IP DHCP qui
  // aura change depuis la derniere fois.
  String host = "banc-" + sn;
  host.toLowerCase();
  MDNS.begin(host.c_str());
  MDNS.addService("http", "tcp", 80);

  OpenOTA.setHardwareId(sn.c_str());       // affiche en tete de page
  OpenOTA.setProductName("Banc moteur " + String(sn));

  // Repere visuel : une couleur par unite. Le cerveau la voit avant de lire
  // l'identifiant, ce qui n'est pas le cas de six caracteres hexa.
  const char* teintes[] = {"#f0a828", "#4ea3f0", "#4fb477", "#c77dd6"};
  OpenOTA.setAccentColor(teintes[(uint8_t)ESP.getEfuseMac() & 3]);

  // --- Couche 2 : compatibilite du binaire ---------------------------------
  // Modele de puce : actif par defaut, rien a faire. Bloque un binaire S3
  // depose sur un ESP32 simple.
  OpenOTA.setChipCheckEnabled(true);

  // Variante : n'active cette ligne qu'apres avoir verifie la trace ci-dessous.
  // OpenOTA.setExpectedVariant("banc-moteur");

  Serial.printf("variante en cours : \"%s\"  version \"%s\"\n",
                OpenOTA.runningVariant().c_str(),
                OpenOTA.runningVersion().c_str());

  // --- Couche 3 : ciblage d'une unite precise ------------------------------
  // Utile pour un deploiement progressif : une seule carte du parc accepte la
  // preversion. Pilote par un drapeau en NVS, pas par le binaire.
  OpenOTA.onGuard([sn](OpenOTATarget t, size_t size) -> bool {
    if (!prefs.getBool("ota_armed", false)) {
      Serial.printf("[%s] OTA non armee\n", sn.c_str());
      return false;
    }
    return true;
  });

  OpenOTA.setAuth("admin", "changeme");
  OpenOTA.setRequireChecksum(true);
  OpenOTA.begin(&server);
  server.begin();
}

void loop() {
  server.handleClient();
  OpenOTA.loop();
}
