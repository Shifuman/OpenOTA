# Provenance du code

OpenOTA est une implementation independante. Elle n'est pas un fork, ni un
derive, ni une reecriture d'ElegantOTA (edition libre ou Pro), ni d'aucune autre
bibliotheque OTA.

Ce qui a servi de reference :
- la liste publique des fonctionnalites annoncees sur le README d'ElegantOTA,
  c'est-a-dire un cahier des charges fonctionnel ;
- la documentation publique de l'API Arduino `Update` / `Updater` et de
  `esp_ota_ops` (Espressif) ;
- les en-tetes publics de WebServer, ESP8266WebServer et ESPAsyncWebServer.

Aucun code source d'ElegantOTA, libre ou sous licence commerciale, n'a ete lu,
copie, transpose ou masque pour produire ce depot. Les noms de symboles, la
structure interne, le protocole HTTP, l'interface web et son style sont propres
a ce projet.

Les fonctionnalites d'un logiciel ne sont pas protegeables ; seule leur
expression l'est. Reimplementer un jeu de fonctionnalites a partir de zero est
licite. Recopier ou obscurcir du code sous licence ne l'est pas, et n'est pas ce
qui a ete fait ici.

Si tu vises un usage commercial : ce depot est sous MIT et ne t'y oblige a rien,
mais acheter ElegantOTA Pro reste la facon de soutenir le travail amont si tu
utilises leur produit par ailleurs.
