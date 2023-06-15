#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <DHT.h>
#include <WiFi.h>
#include <vector>

#define DHTPIN 23 
#define DHTTYPE DHT11 

const int GREEN_LED_PIN = 26;
const int RED_LED_PIN = 19;
const int LIGHT_SENSOR_PIN = 33;
const int LIGHT_THRESHOLD = 0;
const int BLUE_LED_PIN = 22; 

const char* mqtt_server = "test.mosquitto.org";
const char* ssid = "Livebox-6DA0";
const char* password = "NalaaErosScarSanaaEmmaAdem0323";
#define TOPIC_PISCINE "uca/iot/piscine"

WiFiClient espClient;
PubSubClient client(espClient);

DHT dht(DHTPIN, DHTTYPE); 

const float latitude = 43.599998;
const float longitude = 7;

float haversine_distance(float lat1, float lon1, float lat2, float lon2) {
  float lat1_rad = lat1 * M_PI / 180.0;
  float lon1_rad = lon1 * M_PI / 180.0;
  float lat2_rad = lat2 * M_PI / 180.0;
  float lon2_rad = lon2 * M_PI / 180.0;

  float delta_lat = lat2_rad - lat1_rad;
  float delta_lon = lon2_rad - lon1_rad;

  float a = sin(delta_lat / 2) * sin(delta_lat / 2) +
            cos(lat1_rad) * cos(lat2_rad) *
            sin(delta_lon / 2) * sin(delta_lon / 2);
  float c = 2 * atan2(sqrt(a), sqrt(1 - a));
  float distance = 6371.0 * c; 

  return distance;
}

// Structure to store cached pool information
struct CachedPool {
  float lat;
  float lon;
};

std::vector<CachedPool> poolCache;

void initializeCache() {
  // Add pool coordinates to the cache
  CachedPool pool1;
  pool1.lat = 43.599998;
  pool1.lon = 7;
  poolCache.push_back(pool1);

  Serial.println("Cache initialized");
}

bool isInCache(float lat, float lon) {
  for (const auto& pool : poolCache) {
    float distance = haversine_distance(lat, lon, pool.lat, pool.lon);
    if (distance < 25.0) {
      return true;
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  while (!Serial);
  // Initialize LED pins as output
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT); 

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  Serial.println("Connected to WiFi");

  dht.begin(); //température get

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback); // Définir la fonction de rappel pour la réception des messages MQTT

  reconnect();

  initializeCache();
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("esp32")) {
      Serial.println("Connected to MQTT broker");
      client.subscribe(TOPIC_PISCINE);
    } else {
      Serial.print("Failed to connect to MQTT broker. Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Message received!");

  StaticJsonDocument<1000> doc;
  deserializeJson(doc, payload, length);

  float other_lat = doc["info"]["loc"]["lat"];
  float other_lon = doc["info"]["loc"]["lon"];
  float other_temp = doc["status"]["temperature"];

  // Check if the pool is already in the cache
  if (isInCache(other_lat, other_lon)) {
    Serial.println("Pool already in cache");
    return;
  }

  // Vérifier si la distance est inférieure à 0.1 km (100 mètres)
  if (distance < 0.1) {
    // Le client est à proximité de la piscine. Allumer la LED bleue pendant 30 secondes.
    digitalWrite(BLUE_LED_PIN, HIGH);
    delay(30000);
    digitalWrite(BLUE_LED_PIN, LOW);
  }

  // Calculer la distance entre votre piscine et la piscine de l'autre propriétaire
  float distance = haversine_distance(latitude, longitude, other_lat, other_lon);
  
  Serial.print("Calculated distance: ");
  Serial.println(distance);
  
  // Vérifier si la distance est inférieure à 25 km
  if (distance < 25.0) {
    Serial.println("Activating RED LED...");
    digitalWrite(RED_LED_PIN, HIGH);
    delay(5000);
    digitalWrite(RED_LED_PIN, LOW);

    // Add the pool to the cache
    CachedPool newPool;
    newPool.lat = other_lat;
    newPool.lon = other_lon;
    poolCache.push_back(newPool);
  }
}

// Variables pour stocker les pics de température
float maxTemp = -100.0;  // initialiser à une valeur très basse
float minTemp = 100.0;   // initialiser à une valeur très élevée

// Variables pour le contrôle du feu
bool fire = false;
int lastLight = 0;

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  float temperature = dht.readTemperature();
  Serial.print("Temperature: ");
  Serial.println(temperature);

  // Mise à jour des pics de température
  if (temperature > maxTemp) maxTemp = temperature;
  if (temperature < minTemp) minTemp = temperature;

  int light = analogRead(LIGHT_SENSOR_PIN);
  Serial.print("Light: ");
  Serial.println(light);

  // Vérifier s'il y a un feu
  if (light == 0 && abs(temperature - maxTemp) < 2) {
    fire = true;
  } else {
    fire = false;
  }

  String presence = light > LIGHT_THRESHOLD ? "Oui" : "Non";

  // Déterminer l'état de la LED en fonction de la température
  String led = "Vert";

  // Créer le document JSON pour votre piscine
StaticJsonDocument<200> doc; // !!!! mettre 400 pour le message complet !!!! (Je pense que mon ESP a un problème de mémoire)
  doc["status"]["temperature"] = temperature; 
  doc["status"]["light"] = light;
  doc["status"]["heat"] = maxTemp;
  doc["status"]["cold"] = minTemp;
  doc["status"]["running"] = 1;
  doc["status"]["fire"] = fire ? "Yes" : "No";
  doc["info"]["ident"] = 22102343;
  doc["info"]["user"] = "Adem";
  doc["info"]["description"] = "Piscine Mougins";
  doc["info"]["ssid"] = ssid;
  doc["info"]["ip"] = WiFi.localIP().toString();
  doc["info"]["loc"]["lat"] = latitude;
  doc["info"]["loc"]["lon"] = longitude;
  doc["info"]["client"]["lat"] = client_latitude;  // Coordonnées du client
  doc["info"]["client"]["lon"] = client_longitude;  // Coordonnées du client
  doc["reporthost"]["target_port"] = 80;
  doc["reporthost"]["sp"] = 60;
  doc["piscine"]["led"] = "Green";
  doc["piscine"]["presence"] = presence;

  // Convertir le document JSON en chaîne JSON
  String json;
  serializeJson(doc, json);

  Serial.print("JSON: ");
  Serial.println(json);

  Serial.println("Publishing data...");

  // Publier le document JSON sur le topic MQTT
  client.publish(TOPIC_PISCINE, json.c_str());

  delay(5000);  // Publier toutes les 5 secondes

  // Mise à jour de lastLight pour le prochain tour de boucle
  lastLight = light;
  
  // Enter deep sleep mode to save power
  // Adjust the sleep duration based on your requirements
  enterDeepSleep(60);  // Sleep for 60 seconds
}

void enterDeepSleep(int seconds) {
  // Configure ESP32 for deep sleep
  esp_sleep_enable_timer_wakeup(seconds * 1000000LL);
  esp_deep_sleep_start();
}
