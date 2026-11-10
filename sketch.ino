
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

// ---------- PINES ----------
#define DHT_PIN 27
#define DHT_TYPE DHT22       // Wokwi: DHT22 / Real: DHT11
#define RELAIS_PIN 26

// ---------- CONFIGURACION ----------
const float SEUIL_HUMIDITE = 69.0;

// Cambiar si el modulo rele es activo a LOW
const int RELAIS_ON = HIGH;
const int RELAIS_OFF = LOW;

DHT dht(DHT_PIN, DHT_TYPE);
WebServer server(80);

float temperature = NAN;
float humidite = NAN;

unsigned long derniereLecture = 0;
const unsigned long INTERVALLE_LECTURE = 3000;

// Inicio en modo manual
bool modeAutomatique = false;
bool relaisActif = false;

// ---------- PAGINA WEB ----------
const char pageHTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>SEAL - Informations du sac</title>

  <style>
    body {
      font-family: Arial, sans-serif;
      background: #eef2f7;
      text-align: center;
      padding: 25px 12px;
      color: #243247;
    }

    main {
      max-width: 440px;
      margin: auto;
      background: white;
      border-radius: 16px;
      padding: 24px;
      box-shadow: 0 6px 24px #00000010;
    }

    .valeur {
      font-size: 32px;
      color: #1766b5;
      font-weight: bold;
    }

    button {
      padding: 12px 16px;
      margin: 5px;
      border: 2px solid #1766b5;
      border-radius: 8px;
      cursor: pointer;
      background: white;
      color: #1766b5;
      font-size: 15px;
    }

    button.selectionne {
      background: #1766b5;
      color: white;
    }

    #etat, #regle {
      color: #666;
      font-size: 14px;
    }

    #instruction {
      line-height: 1.5;
    }

    hr {
      border: 0;
      border-top: 1px solid #eee;
      margin: 24px 0;
    }
  </style>
</head>

<body>
  <main>
    <h1>Les informations du sac</h1>

    <p>Température</p>
    <div class="valeur">
      <span id="temperature">--</span> °C
    </div>

    <p>Humidité</p>
    <div class="valeur">
      <span id="humidite">--</span> %
    </div>

    <hr>

    <h2>Ventilation</h2>

    <p>Mode : <strong id="mode">--</strong></p>

    <button id="modeManuel"
            onclick="changerMode('manuel')">
      Mode manuel
    </button>

    <button id="modeAuto"
            onclick="changerMode('auto')">
      Mode automatique
    </button>

    <p>Relais : <strong id="relais">--</strong></p>

    <p id="instruction"></p>
    <p id="regle"></p>
    <p id="etat" role="status">Connexion...</p>
  </main>

  <script>
    let commandeEnCours = false;

    function afficher(donnees) {

      document.getElementById("temperature").textContent =
        donnees.temperature === null
          ? "--"
          : donnees.temperature.toFixed(1);

      document.getElementById("humidite").textContent =
        donnees.humidite === null
          ? "--"
          : donnees.humidite.toFixed(1);

      const automatique = donnees.mode === "auto";

      document.getElementById("mode").textContent =
        automatique ? "Automatique" : "Manuel";

      document.getElementById("relais").textContent =
        donnees.relais ? "ACTIVÉ" : "DÉSACTIVÉ";

      document.getElementById("modeManuel").classList.toggle(
        "selectionne", !automatique
      );

      document.getElementById("modeAuto").classList.toggle(
        "selectionne", automatique
      );

      document.getElementById("instruction").textContent =
        automatique
          ? "Le relais est commandé selon l’humidité."
          : "Le relais est activé. Utilisez votre interrupteur physique pour commander le ventilateur.";

      document.getElementById("regle").textContent =
        "Auto : relais activé si humidité > " +
        donnees.seuilHumidite + " %.";

      const capteurValide = donnees.humidite !== null;

      document.getElementById("etat").textContent =
        capteurValide
          ? "Dernière mise à jour : " +
            new Date().toLocaleTimeString()
          : "Capteur indisponible." +
            (automatique ? " Relais désactivé." : "");
    }

    async function lireEtat() {
      const reponse = await fetch("/api/mesures", {
        cache: "no-store"
      });

      if (!reponse.ok) {
        throw new Error("Lecture impossible");
      }

      afficher(await reponse.json());
    }

    async function changerMode(mode) {

      if (commandeEnCours) return;

      commandeEnCours = true;

      document.getElementById("modeManuel").disabled = true;
      document.getElementById("modeAuto").disabled = true;

      try {
        const reponse = await fetch("/api/mode", {
          method: "POST",
          headers: {
            "Content-Type": "application/x-www-form-urlencoded"
          },
          body: new URLSearchParams({ mode })
        });

        if (!reponse.ok) {
          throw new Error(await reponse.text());
        }

        await lireEtat();

      } catch (erreur) {
        document.getElementById("etat").textContent =
          "Erreur : " + erreur.message;
      } finally {

        commandeEnCours = false;

        document.getElementById("modeManuel").disabled = false;
        document.getElementById("modeAuto").disabled = false;
      }
    }

    async function actualiser() {
      try {
        if (!commandeEnCours) {
          await lireEtat();
        }
      } catch (erreur) {
        document.getElementById("etat").textContent =
          "Connexion à l’ESP32 impossible.";
      } finally {
        setTimeout(actualiser, 3000);
      }
    }

    actualiser();
  </script>
</body>
</html>
)HTML";

// ---------- CONTROLE DU RELAIS ----------
void commanderRelais(bool activer) {

  relaisActif = activer;

  digitalWrite(
    RELAIS_PIN,
    activer ? RELAIS_ON : RELAIS_OFF
  );
}

// ---------- LOGIQUE AUTOMATIQUE ----------
void appliquerCommande() {

  // Mode manuel : relais active
  if (!modeAutomatique) {
    commanderRelais(true);
    return;
  }

  // Capteur indisponible
  if (isnan(humidite)) {
    commanderRelais(false);
    return;
  }

  // Activation uniquement si humidite > 69 %
  bool conditionsRespectees =
    humidite > SEUIL_HUMIDITE;

  commanderRelais(conditionsRespectees);
}

// ---------- LECTURE DU CAPTEUR ----------
void lireCapteur() {

  humidite = dht.readHumidity();
  temperature = dht.readTemperature();

  appliquerCommande();

  if (isnan(humidite)) {

    Serial.printf(
      "Erreur DHT | Mode : %s | Relais : %s\n",
      modeAutomatique ? "AUTO" : "MANUEL",
      relaisActif ? "ACTIF" : "INACTIF"
    );

    return;
  }

  Serial.printf(
    "Temperature : %.1f C | Humidite : %.1f %% | Mode : %s | Relais : %s\n",
    temperature,
    humidite,
    modeAutomatique ? "AUTO" : "MANUEL",
    relaisActif ? "ACTIF" : "INACTIF"
  );
}

// ---------- API MESURES ----------
void envoyerMesures() {

  String json;
  json.reserve(240);

  json = "{\"temperature\":";
  json += isnan(temperature)
    ? String("null")
    : String(temperature, 1);

  json += ",\"humidite\":";
  json += isnan(humidite)
    ? String("null")
    : String(humidite, 1);

  json += ",\"mode\":\"";
  json += modeAutomatique ? "auto" : "manuel";
  json += "\"";

  json += ",\"relais\":";
  json += relaisActif ? "true" : "false";

  json += ",\"seuilHumidite\":";
  json += String(SEUIL_HUMIDITE, 1);

  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

// ---------- SETUP ----------
void setup() {

  Serial.begin(115200);

  // Initialiser le relais
  digitalWrite(RELAIS_PIN, RELAIS_OFF);
  pinMode(RELAIS_PIN, OUTPUT);

  // Mode manuel au demarrage
  appliquerCommande();

  // Initialiser DHT22
  dht.begin();

  // WiFi Wokwi
  WiFi.mode(WIFI_STA);
  WiFi.begin("Wokwi-GUEST", "", 6);

  Serial.print("Connexion WiFi");

  unsigned long debutConnexion = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - debutConnexion < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connecte");
    Serial.print("IP : ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("Connexion WiFi impossible");
  }

  // Page web
  server.on("/", HTTP_GET, []() {
    server.send_P(
      200,
      "text/html; charset=utf-8",
      pageHTML
    );
  });

  // API mesures
  server.on("/api/mesures", HTTP_GET, envoyerMesures);

  // API changement de mode
  server.on("/api/mode", HTTP_POST, []() {

    String mode = server.arg("mode");

    if (mode != "auto" && mode != "manuel") {
      server.send(400, "text/plain", "Mode invalide");
      return;
    }

    modeAutomatique = (mode == "auto");

    appliquerCommande();

    Serial.print("Nouveau mode : ");
    Serial.println(modeAutomatique ? "AUTO" : "MANUEL");

    server.send(200, "text/plain", "OK");
  });

  server.begin();

  Serial.println("Serveur web demarre");
}

// ---------- LOOP ----------
void loop() {

  server.handleClient();

  if (millis() - derniereLecture >= INTERVALLE_LECTURE) {

    derniereLecture = millis();

    lireCapteur();
  }
}
