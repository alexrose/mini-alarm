// Mini alarm system with audio & email alert
// www.alextrandafir.ro

#include <FS.h> 
#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

// Define used pins
#define PIR_PIN D1
#define SWITCH_PIN D4
#define BUZZER_PIN D2

// Define default values for used variables
String result;

bool firstRun                 = true;
bool firstNotification        = true;
bool shouldSaveConfig         = false;

char server_name[]            = "mydomain.tld";
char script_file[]            = "path/to/script.php";
char script_token[]           = "token";
char alarm_cycles[32]         = "600";
char activation_cycles[32]    = "30";
char deactivation_cycles[32]  = "30";

// Set server port
ESP8266WebServer server(80);

// Init WiFi manager and client
WiFiClient wifiClient;
WiFiManager wifiManager;

// Callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}

// Init script
// Define pins mode and create wifi connection
void setup()
{
  Serial.begin(115200);
  
  // Define pins
  pinMode(PIR_PIN, INPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  // Because, silence is golden
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(SWITCH_PIN, HIGH);
  
  // Mounting FS
  if (SPIFFS.begin()) {
    if (SPIFFS.exists("/config.json")) {
      File configFile = SPIFFS.open("/config.json", "r");

      if (configFile) {
        // Allocate a buffer to store contents of the file.
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);

        if (json.success()) {
          strcpy(alarm_cycles, json["alarm_cycles"]);
          strcpy(activation_cycles, json["activation_cycles"]);
          strcpy(deactivation_cycles, json["deactivation_cycles"]);
          strcpy(server_name, json["server_name"]);
          strcpy(script_file, json["script_file"]);
          strcpy(script_token, json["script_token"]); 
        }
      } else {
        Serial.println("File config.json can't be opened; running resetESP().");
        resetESP();
      }
    } else {
      Serial.println("File config.json not found; running resetESP().");
      resetESP();
    }
  }

  // Set custom params
  WiFiManagerParameter custom_alarm_cycles("alarm_cycles", "Alarm cycles(sec)", alarm_cycles, 32);
  WiFiManagerParameter custom_activation_cycles("activation_cycles", "Activation cycles(sec)", activation_cycles, 32);
  WiFiManagerParameter custom_deactivation_cycles("deactivation_cycles", "Deactivation cycles(sec)", deactivation_cycles, 32);
  WiFiManagerParameter custom_server_name("server_name", "Server name(no http)", server_name, 255);
  WiFiManagerParameter custom_script_file("script_file", "Script file", script_file, 255);
  WiFiManagerParameter custom_script_token("script_token", "Script token", script_token, 255);

  // Set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_alarm_cycles);
  wifiManager.addParameter(&custom_activation_cycles);
  wifiManager.addParameter(&custom_deactivation_cycles);
  wifiManager.addParameter(&custom_server_name);
  wifiManager.addParameter(&custom_script_file);
  wifiManager.addParameter(&custom_script_token);

  if (!wifiManager.autoConnect("MiniAlarmAP")) {
    Serial.println("Failed to connect and hit timeout.");
    delay(3000);
    
    // Reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  strcpy(alarm_cycles, custom_alarm_cycles.getValue());
  strcpy(activation_cycles, custom_activation_cycles.getValue());
  strcpy(deactivation_cycles, custom_deactivation_cycles.getValue());
  strcpy(server_name, custom_server_name.getValue());
  strcpy(script_file, custom_script_file.getValue());
  strcpy(script_token, custom_script_token.getValue());

  // Save the custom parameters to FS
  if (shouldSaveConfig) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();

    json["server_name"] = server_name;
    json["script_file"] = script_file;
    json["script_token"] = script_token;
    json["alarm_cycles"] = alarm_cycles;
    json["activation_cycles"] = activation_cycles;
    json["deactivation_cycles"] = deactivation_cycles;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("Failed to open config file for writing.");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  }

  server.on("/", handleRoot);
  server.on("/reset", handleReset);
  server.onNotFound(handleNotFound);
  server.begin();
  
  Serial.println("HTTP server started");
}

void handleRoot() {
  String message = "{";
  message += "'error':'false',";
  message += "'message':'All is fine, all is good.'";
  message += "}";
  
  server.send(200, "application/json", message);
}

void handleReset() {
  String message = "{";
  message += "'error':'false',";
  message += "'message':'It is now safe to reboot your ESP.'";
  message += "}";
  
  resetESP();
  server.send(200, "application/json", message);
}

void resetESP() {
  SPIFFS.format();
  wifiManager.resetSettings();
}

void handleNotFound() {
  String message = "{";
  message += "'error':'true',";
  message += "'message':'Invalid request.'";
  message += "}";
  
  server.send(200, "application/json", message);
}

void beep(unsigned long delayms){
  digitalWrite(BUZZER_PIN, LOW);
  delay(delayms);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(delayms);
}

void sendDataToServer() {
  if (wifiClient.connect(server_name, 80)) {
    Serial.println("Connected to "+(String)server_name);
    wifiClient.println("GET /"+(String)script_file+"?token="+(String)script_token+" HTTP/1.1");
    wifiClient.println("Host: "+(String)server_name);
    wifiClient.println("Connection: close"); 
    wifiClient.println();
  } 
  else {
    Serial.println("Connection to "+(String)server_name+" failed."); 
    Serial.println();
  }

 // waits for data
 while(wifiClient.connected() && !wifiClient.available()) delay(1);
  // connected or data available
  while (wifiClient.connected() || wifiClient.available()) { 
    char c = wifiClient.read(); //gets byte from ethernet buffer
      result = result+c;
    }
  // stop client
  wifiClient.stop(); 
  Serial.println(result);
}

void loop()
{
  // Serial.println("Web server, handle client requests.");
  server.handleClient();

  // cast to int because reasons
  int cast_alarm_cycles = strtoul(alarm_cycles, NULL, 10);
  int cast_deactivation_cycles = strtoul(deactivation_cycles, NULL, 10);
  int cast_activation_cycles = strtoul(activation_cycles, NULL, 10);
  // Serial.println("Cast to int settings retrieved from JSON:");
  // Serial.println("Alarm cycles: "+(String)cast_alarm_cycles);
  // Serial.println("Activation cycles: "+(String)cast_activation_cycles);
  // Serial.println("Deactivation cycles: "+(String)cast_deactivation_cycles);

  // if switch is on and this is not the activation step
  if (digitalRead(SWITCH_PIN) == LOW && firstRun == true) {

    Serial.println("Enabling PIR sensor.");
    Serial.println(">> firstRun: "+(String)firstRun + " >> firstNotification: "+(String)firstNotification);
    
    // this is not the activation step anymore
    firstRun = false;
    
    // short audio notification for sensor enabling
    int activation_index = 0;
    while(activation_index <= cast_activation_cycles && digitalRead(SWITCH_PIN) == LOW){
      Serial.println("-- Activation Sound: "+(String)activation_index);
      activation_index++;
      beep(50);beep(50);beep(50);beep(50);delay(600);
    }

    Serial.println("PIR sensor was enabled.");
    Serial.println(">> firstRun: "+(String)firstRun + " >> firstNotification: "+(String)firstNotification);
  }

  // if switch is turned off, after it was on for a period of time
  // reset first run and mark the event with an audio signal
  if (digitalRead(SWITCH_PIN) == HIGH && firstRun == false) {
    Serial.println("Disabling PIR sensor.");
    Serial.println(">> firstRun: "+(String)firstRun + " >> firstNotification: "+(String)firstNotification);
    
    firstRun = true;
    firstNotification = true;
    beep(1000);

    Serial.println("PIR sensor was disabled.");
    Serial.println(">> firstRun: "+(String)firstRun + " >> firstNotification: "+(String)firstNotification);
  }
  
  // if motion is detected, mark the event with an audio signal (@todo) and send a notification
  if (digitalRead(SWITCH_PIN) == LOW && digitalRead(PIR_PIN) == HIGH) {
    Serial.println("Motion was detected.");
    
    // prevent
    if (firstNotification == true) {
      Serial.println("Preventing sound started.");
      int index = 0;
      firstNotification = false;
      
      while(index <= cast_deactivation_cycles && digitalRead(SWITCH_PIN) == LOW) {
        Serial.println("-- Preventing sound: "+(String)index);
        index++;
        beep(50); beep(50); delay(800);  
      }
     
      Serial.println("Preventing sound ended.");
    }
    
    // sound the alarm
    if (digitalRead(SWITCH_PIN) == LOW) {
      Serial.println("Send an email notification.");
      sendDataToServer();

      Serial.println("Sound the alarm.");
      for(int i=0; i<=cast_alarm_cycles; i++) {
        beep(500);  
      }
    }
  }
}
