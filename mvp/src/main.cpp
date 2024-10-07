#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "SPIFFS.h"
#include <mbedtls/md5.h>

// Button pin
const int buttonPin = 2;

// API endpoints
const String baseUrl = "https://api.notion.com/v1/";
String databaseQueryUrl;

// OLED display settings
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define OLED_RESET -1     // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

struct Task {
  String id;
  String name;
  String category;
  String dueDate;
  bool done;
};

Task currentTask;
bool tasksDone = false;
bool animationInProgress = false;
bool buttonPressed = false;

// Wi-Fi Manager variables
const char* input_parameter1 = "ssid";
const char* input_parameter2 = "pass";
const char* input_parameter3 = "api_key";
const char* input_parameter4 = "database_id";

String ssid;
String pass;
String notionApiKey;
String databaseId;

const char* SSID_path = "/ssid.txt";
const char* Password_path = "/pass.txt";
const char* API_KEY_path = "/api_key.txt";
const char* DATABASE_ID_path = "/database_id.txt";

unsigned long previousMillis = 0;

AsyncWebServer server(80);

String readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

bool initialize_Wifi() {
  if (ssid == "" || pass == "") {
    Serial.println("Undefined SSID or password.");
    return false;
  }

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while (WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= 10000) {
      Serial.println("Failed to connect.");
      return false;
    }
  }

  Serial.println(WiFi.localIP());
  return true;
}

String formatDate(String dateString) {
  // Assuming the dateString format is "YYYY-MM-DDTHH:MM:SS.SSSZ"
  String formattedDate = dateString.substring(5, 7) + "/" + dateString.substring(8, 10) + "/" + dateString.substring(2, 4);
  return formattedDate;
}

void fetchTasks() {
  HTTPClient https;
  https.begin(databaseQueryUrl);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + notionApiKey);
  https.addHeader("Notion-Version", "2022-06-28");
  int httpResponseCode = https.POST("{\"filter\":{\"and\":[{\"property\":\"Category\",\"multi_select\":{\"contains\":\"Club\"}}]}}");

  if (httpResponseCode == 200) {
    String response = https.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    JsonArray results = doc["results"].as<JsonArray>();

    for (JsonVariant result : results) {
      if (!result["properties"]["Done"]["checkbox"].as<bool>()) {
        currentTask.id = result["id"].as<String>();
        currentTask.name = result["properties"]["Name"]["title"][0]["plain_text"].as<String>();
        currentTask.category = result["properties"]["Category"]["multi_select"][0]["name"].as<String>();
        currentTask.done = false;

        // Extract the due date
        JsonObject dueProperty = result["properties"]["Due"]["date"].as<JsonObject>();
        if (dueProperty.containsKey("start")) {
          String dueDate = dueProperty["start"].as<String>();
          currentTask.dueDate = formatDate(dueDate);
        } else {
          currentTask.dueDate = "No due date";
        }

        tasksDone = false;
        break;
      }
    }

    if (results.size() == 0 || (results.size() > 0 && currentTask.id.length() == 0)) {
      tasksDone = true;
    }
  } else {
    Serial.print("Error fetching tasks: ");
    Serial.println(httpResponseCode);
    String response = https.getString();
    Serial.println("Response body: " + response);
  }

  https.end();
}

void markTaskAsDone(Task task) {
  HTTPClient https;
  String url = baseUrl + "pages/" + task.id;
  https.begin(url);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + notionApiKey);
  https.addHeader("Notion-Version", "2022-06-28");
  int httpResponseCode = https.PATCH("{\"properties\":{\"Done\":{\"checkbox\":true}}}");

  if (httpResponseCode == 200) {
    Serial.println("Task marked as done");
    tasksDone = true;
  } else {
    Serial.print("Error marking task as done: ");
    Serial.println(httpResponseCode);
    String response = https.getString();
    Serial.println("Response body: " + response);
  }

  https.end();
}

void displayTaskWithDate(Task task) {
  display.clearDisplay();
  display.setTextColor(WHITE);

  if (tasksDone) {
    display.setTextSize(2);
    display.setCursor(0, 16);
    display.println("No tasks");
    display.display();
  } else {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Due: " + currentTask.dueDate);
    display.setTextSize(2);
    display.display();
  }
}

void animateTaskDone() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 16);
  display.println("Task Done!");
  display.display();

  // Slide the "Task Done!" text off the screen
  for (int16_t x = 0; x < SCREEN_WIDTH; x += 4) {
    display.clearDisplay();
    display.setCursor(x, 16);
    display.println("Task Done!");
    display.display();
    delay(25);
  }
}

void scrollTaskName() {
  animationInProgress = true;
  int16_t currentX = -display.width();
  int16_t textWidth = display.width();

  while (currentX < textWidth) {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Due: " + currentTask.dueDate);

    display.setTextSize(2);
    display.setCursor(currentX, 16);
    display.println(currentTask.name);

    display.setTextSize(1);
    display.setCursor(0, 32);
    display.println("Category: " + currentTask.category);

    display.display();

    currentX += 4;

    if (digitalRead(buttonPin) == LOW) {
      buttonPressed = true;
      markTaskAsDone(currentTask);
      animateTaskDone();
      fetchTasks();
      displayTaskWithDate(currentTask);
      break;
    }

    delay(100);
  }

  animationInProgress = false;
}

void checkButtonPress() {
  if (digitalRead(buttonPin) == LOW && !buttonPressed) {
    buttonPressed = true;
    markTaskAsDone(currentTask);
    animateTaskDone();
    fetchTasks();
    displayTaskWithDate(currentTask);
  } else if (digitalRead(buttonPin) == HIGH) {
    buttonPressed = false;
  }
}

void setup() {
  Serial.begin(115200);

  Serial.print("SDA Pin: ");
  Serial.println(SDA);
  
  Serial.print("SCL Pin: ");
  Serial.println(SCL);

  Wire.begin(5, 6);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Failed to initialize OLED display");
    while (1);
  } else {
    Serial.println("OLED display initialized");
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("OLED initialized");
  display.display();
  delay(1000);

  Serial.println("Initializing SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("SPIFFS Issue");
    display.display();
    delay(1000);
  } else {
    Serial.println("SPIFFS mounted successfully");
  }

  // Initialize the OLED display
  

  pinMode(buttonPin, INPUT_PULLUP);

  Serial.println("Reading Wi-Fi credentials and Notion API credentials from SPIFFS...");
  ssid = readFile(SPIFFS, SSID_path);
  pass = readFile(SPIFFS, Password_path);
  notionApiKey = readFile(SPIFFS, API_KEY_path);
  databaseId = readFile(SPIFFS, DATABASE_ID_path);

  Serial.println("SSID: " + ssid);
  Serial.println("Password: " + pass);
  Serial.println("Notion API Key: " + notionApiKey);
  Serial.println("Database ID: " + databaseId);

  if (initialize_Wifi()) {
    Serial.println("Connected to Wi-Fi");
    // Update the database query URL with the retrieved database ID
    databaseQueryUrl = baseUrl + "databases/" + databaseId + "/query";

    // Fetch tasks on boot
    fetchTasks();
    displayTaskWithDate(currentTask);
  } else {
    Serial.println("Setting up Wi-Fi Manager");
    WiFi.softAP("BOTION CUBE", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("Connect to Wifi:");
    display.println("BOTION CUBE ");
    display.println();
    display.print("Then enter URL on phone browser: ");
    display.println(IP.toString());
    display.display();

    delay(100);


    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      Serial.println("Received GET request on /");
      request->send(SPIFFS, "/wifimanager.html", "text/html");
    });

    server.serveStatic("/", SPIFFS, "/");

    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      Serial.println("Received POST request on /");
      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
          if (p->name() == input_parameter1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            writeFile(SPIFFS, SSID_path, ssid.c_str());
          } else if (p->name() == input_parameter2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            writeFile(SPIFFS, Password_path, pass.c_str());
          } else if (p->name() == input_parameter3) {
            notionApiKey = p->value().c_str();
            Serial.print("Notion API Key set to: ");
            Serial.println(notionApiKey);
            writeFile(SPIFFS, API_KEY_path, notionApiKey.c_str());
          } else if (p->name() == input_parameter4) {
            databaseId = p->value().c_str();
            Serial.print("Database ID set to: ");
            Serial.println(databaseId);
            writeFile(SPIFFS, DATABASE_ID_path, databaseId.c_str());
          }
        }
      }
      request->send(200, "text/plain", "Success. ESP32 will now restart.");
      delay(3000);
      ESP.restart();
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
      Serial.println("Request not found");
      request->send(404, "text/plain", "Not found");
    });

    Serial.println("Starting server...");
    server.begin();
  }
}

void loop() {
  checkButtonPress();
  if (!animationInProgress && !tasksDone) {
    scrollTaskName();
  }
  delay(1000);
}