#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <driver/i2s.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#include <Arduino.h>
#include <FS.h>

#define I2S_WS 21
#define I2S_SD 18  
#define I2S_SCK 20 
#define I2S_PORT I2S_NUM_0
#define I2S_SAMPLE_RATE   (16000)
#define I2S_SAMPLE_BITS   (16)
#define I2S_READ_LEN      (16 * 1024)
#define RECORD_TIME       (5) //Seconds
#define I2S_CHANNEL_NUM   (1)
#define FLASH_RECORD_SIZE (I2S_CHANNEL_NUM * I2S_SAMPLE_RATE * I2S_SAMPLE_BITS / 8 * RECORD_TIME)

#define FILESYSTEM SPIFFS
// You only need to format the filesystem once
#define FORMAT_FILESYSTEM false

const char *ssid = "RESNET-GUEST-DEVICE";
const char *password = "ResnetConnect";

File file;
const char filename[] = "/recording.wav";
const int headerSize = 44;
const char* serverUrl = "https://ece140.frosty-sky-f43d.workers.dev/api/transcribe";
const char PID[] = "A17323782"; 

// Assuming these are declared and initialized elsewhere
String notionApiKey;
String databaseId;

struct Task {
  String id;
  String name;
  String category;
  bool done;
  String dueDate; // In the format "YYYY-MM-DD"
};

void addTask(Task task) {
  HTTPClient https;
  String url = "https://api.notion.com/v1/pages";
  https.begin(url);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + notionApiKey);
  https.addHeader("Notion-Version", "2022-06-28");

  // Create the request body
  DynamicJsonDocument requestBody(1024);
  requestBody["parent"]["database_id"] = databaseId;
  JsonObject properties = requestBody.createNestedObject("properties");
  JsonArray titleArray = properties["Name"].createNestedArray("title");
  JsonObject titleObject = titleArray.createNestedObject();
  titleObject["text"]["content"] = task.name;
  JsonArray categoryArray = properties["Category"].createNestedArray("multi_select");
  JsonObject categoryObject = categoryArray.createNestedObject();
  categoryObject["name"] = task.category;
  properties["Due"]["date"]["start"] = task.dueDate;

  String requestBodyString;
  serializeJson(requestBody, requestBodyString);

  int httpResponseCode = https.POST(requestBodyString);

  if (httpResponseCode == 200) {
    String response = https.getString();
    DynamicJsonDocument responseJson(1024);
    deserializeJson(responseJson, response);

    String taskId = responseJson["id"].as<String>();
    String taskName = responseJson["properties"]["Name"]["title"][0]["text"]["content"].as<String>();

    Task newTask;
    newTask.id = taskId;
    newTask.name = taskName;
    newTask.category = task.category;
    newTask.dueDate = task.dueDate;
    newTask.done = false;

    // Handle the newly created task as needed
    // For example, you can add it to a list of tasks or update the display

    Serial.println("Task created successfully");
  } else {
    Serial.print("Error creating task: ");
    Serial.println(httpResponseCode);
    String response = https.getString();
    Serial.println("Response body: " + response);
  }

  https.end();
}

void listSPIFFS(void) {
  Serial.println(F("\r\nListing SPIFFS files:"));
  static const char line[] PROGMEM =  "=================================================";

  Serial.println(FPSTR(line));
  Serial.println(F("  File name                              Size"));
  Serial.println(FPSTR(line));

  fs::File root = SPIFFS.open("/");
  if (!root) {
    Serial.println(F("Failed to open directory"));
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(F("Not a directory"));
    return;
  }

  fs::File file = root.openNextFile();
  while (file) {

    if (file.isDirectory()) {
      Serial.print("DIR : ");
      String fileName = file.name();
      Serial.print(fileName);
    } else {
      String fileName = file.name();
      Serial.print("  " + fileName);
      // File path can be 31 characters maximum in SPIFFS
      int spaces = 33 - fileName.length(); // Tabulate nicely
      if (spaces < 1) spaces = 1;
      while (spaces--) Serial.print(" ");
      String fileSize = (String) file.size();
      spaces = 10 - fileSize.length(); // Tabulate nicely
      if (spaces < 1) spaces = 1;
      while (spaces--) Serial.print(" ");
      Serial.println(fileSize + " bytes");
    }

    file = root.openNextFile();
  }

  Serial.println(FPSTR(line));
  Serial.println();
  delay(1000);
}

void wavHeader(byte* header, int wavSize){
  header[0] = 'R';
  header[1] = 'I';
  header[2] = 'F';
  header[3] = 'F';
  unsigned int fileSize = wavSize + headerSize - 8;
  header[4] = (byte)(fileSize & 0xFF);
  header[5] = (byte)((fileSize >> 8) & 0xFF);
  header[6] = (byte)((fileSize >> 16) & 0xFF);
  header[7] = (byte)((fileSize >> 24) & 0xFF);
  header[8] = 'W';
  header[9] = 'A';
  header[10] = 'V';
  header[11] = 'E';
  header[12] = 'f';
  header[13] = 'm';
  header[14] = 't';
  header[15] = ' ';
  header[16] = 0x10;
  header[17] = 0x00;
  header[18] = 0x00;
  header[19] = 0x00;
  header[20] = 0x01;
  header[21] = 0x00;
  header[22] = 0x01;
  header[23] = 0x00;
  header[24] = 0x80;
  header[25] = 0x3E;
  header[26] = 0x00;
  header[27] = 0x00;
  header[28] = 0x00;
  header[29] = 0x7D;
  header[30] = 0x00;
  header[31] = 0x00;
  header[32] = 0x02;
  header[33] = 0x00;
  header[34] = 0x10;
  header[35] = 0x00;
  header[36] = 'd';
  header[37] = 'a';
  header[38] = 't';
  header[39] = 'a';
  header[40] = (byte)(wavSize & 0xFF);
  header[41] = (byte)((wavSize >> 8) & 0xFF);
  header[42] = (byte)((wavSize >> 16) & 0xFF);
  header[43] = (byte)((wavSize >> 24) & 0xFF);
  
}

void SPIFFSInit(){
  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS initialisation failed!");
    while(1) yield();
  }

  SPIFFS.remove(filename);
  file = SPIFFS.open(filename, FILE_WRITE);
  if(!file){
    Serial.println("File is not available!");
  }

  byte header[headerSize];
  wavHeader(header, FLASH_RECORD_SIZE);

  file.write(header, headerSize);
  listSPIFFS();
}

void i2sInit(){
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = i2s_bits_per_sample_t(I2S_SAMPLE_BITS),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 64,
    .dma_buf_len = 1024,
    .use_apll = 1
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_set_pin(I2S_PORT, &pin_config);
}


void i2s_adc_data_scale(uint8_t * d_buff, uint8_t* s_buff, uint32_t len)
{
    uint32_t j = 0;
    uint32_t dac_value = 0;
    for (int i = 0; i < len; i += 2) {
        dac_value = ((((uint16_t) (s_buff[i + 1] & 0xf) << 8) | ((s_buff[i + 0]))));
        d_buff[j++] = 0;
        d_buff[j++] = dac_value * 256 / 2048;
    }
}

void i2s_adc()
{
    int i2s_read_len = I2S_READ_LEN;
    int flash_wr_size = 0;
    size_t bytes_read;

    char* i2s_read_buff = (char*) calloc(i2s_read_len, sizeof(char));
    uint8_t* flash_write_buff = (uint8_t*) calloc(i2s_read_len, sizeof(char));

    i2s_read(I2S_PORT, (void*) i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
    i2s_read(I2S_PORT, (void*) i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
    
    Serial.println(" *** Recording Start *** ");
    while (flash_wr_size < FLASH_RECORD_SIZE) {
        //read data from I2S bus, in this case, from ADC.
        i2s_read(I2S_PORT, (void*) i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
        //example_disp_buf((uint8_t*) i2s_read_buff, 64);
        //save original data from I2S(ADC) into flash.
        i2s_adc_data_scale(flash_write_buff, (uint8_t*)i2s_read_buff, i2s_read_len);
        file.write((const byte*) flash_write_buff, i2s_read_len);
        flash_wr_size += i2s_read_len;
        ets_printf("Sound recording %u%%\n", flash_wr_size * 100 / FLASH_RECORD_SIZE);
    }
    file.close();

    free(i2s_read_buff);
    i2s_read_buff = NULL;
    free(flash_write_buff);
    flash_write_buff = NULL;
    
    listSPIFFS();
}

String sendAudioFile() {
    if (!SPIFFS.begin(true)) {
        Serial.println("An error has occurred while mounting SPIFFS");
        return String();
    }

    File audioFile = SPIFFS.open(filename, FILE_READ);
    if (!audioFile) {
        Serial.println("Failed to open audio file for reading");
        return String();
    }

    HTTPClient httpClient;

    // Define boundary for multipart request
    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";

    // Prepare headers for multipart/form-data request
    String header = "multipart/form-data; boundary=" + boundary;

    // Start building the request body
    String requestBody1 = "--" + boundary + "\r\n";
    requestBody1 += "Content-Disposition: form-data; name=\"auth\"\r\n\r\n";
    requestBody1 += String(PID) + "\r\n";
    requestBody1 += "--" + boundary + "\r\n";
    requestBody1 += "Content-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\n";
    requestBody1 += "Content-Type: audio/wav\r\n\r\n";

    // Append closing boundary
    String requestBody2 = "\r\n--" + boundary + "--\r\n";

    // Send headers and request body
    httpClient.begin(serverUrl);
     httpClient.addHeader("Content-Type", header);
    int httpResponseCode = httpClient.sendRequestFile("POST", requestBody1, audioFile, requestBody2);

    if (httpResponseCode == 200) {
        String response = httpClient.getString();
        audioFile.close();
        httpClient.end();
        Serial.println("Response received from server:");

        // Deserialize JSON and extract transcription field
        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, response);

        if (error) {
            Serial.print("Failed to parse JSON: ");
            Serial.println(error.c_str());
            return String();
        }

        const char* transcription = doc["transcription"];
        if (transcription) {
            Serial.print("Transcription: ");
            Serial.println(transcription);
            return transcription;
        } else {
            Serial.println("Transcription field not found in JSON response.");
            return String();
        }
    } else {
        Serial.println("Error in sending POST request");
        Serial.println(httpResponseCode);
        audioFile.close();
        httpClient.end();
        return String();
    }
}

void setup(void) {
  Serial.begin(115200);

  i2sInit();

  //WIFI INIT
  Serial.printf("Connecting to %s\n", ssid);
  if (String(WiFi.SSID()) != String(ssid)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
  
  SPIFFSInit();
  i2s_adc();
  String transcription = sendAudioFile();

  if (!transcription.isEmpty()) {
    Task newTask;
    newTask.name = transcription;
    newTask.category = "Club";
    newTask.done = false;
    newTask.dueDate = "2024-10-10";

    addTask(newTask);
  }
}

void loop(void) {
}
