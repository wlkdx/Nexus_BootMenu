#include <Arduino.h>
#include <M5Unified.h>
#include <LittleFS.h>
#include <Update.h>
#include <vector>
#include <esp_partition.h>
#include <esp_ota_ops.h>


#define FPS_LIMIT 60

M5Canvas canvas(&M5.Display);
std::vector<String> binFiles;
int selectedFile = 0;
String logTerminal = "UART READY";

enum SystemState { STATE_MENU, STATE_RECEIVING, STATE_FLASHING, STATE_ERROR };
SystemState currentState = STATE_MENU;

void scanLittleFS() {
    binFiles.clear();
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        logTerminal = "ERR: FS MOUNT FAIL";
        currentState = STATE_ERROR;
        return;
    }

    File file = root.openNextFile();
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".bin") && !fileName.startsWith(".")) {
            if (!fileName.startsWith("/")) fileName = "/" + fileName;
            binFiles.push_back(fileName);
        }
        file = root.openNextFile();
    }

    if (binFiles.empty()) {
        logTerminal = "EMPTY FS";
        selectedFile = 0;
    } else {
        logTerminal = "FOUND " + String(binFiles.size()) + " OS BINARIES";
        if (selectedFile >= binFiles.size()) selectedFile = 0;
    }
}

void handleSerialCommands() {
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();

        if (cmd.startsWith("START_UPLOAD:")) {
            currentState = STATE_RECEIVING;
            int firstColon = cmd.indexOf(':');
            int lastColon = cmd.lastIndexOf(':');
            
            if (firstColon == -1 || lastColon == firstColon) {
                Serial.println("ERROR: BAD_FORMAT");
                currentState = STATE_MENU;
                return;
            }

            String filename = "/" + cmd.substring(firstColon + 1, lastColon);
            size_t fileSize = cmd.substring(lastColon + 1).toInt();

            if (LittleFS.exists(filename)) LittleFS.remove(filename);

            File file = LittleFS.open(filename, "w");
            if (!file) {
                Serial.println("ERROR: CANNOT_WRITE");
                currentState = STATE_MENU;
                return;
            }

            delay(50);
            while(Serial.available() > 0) Serial.read(); 
            Serial.println("READY"); 

            uint8_t buffer[256];
            size_t bytesReceived = 0;
            uint32_t lastTimeout = millis();

            while (bytesReceived < fileSize) {
                if (Serial.available() > 0) {
                    size_t toRead = min((size_t)256, fileSize - bytesReceived);
                    size_t readNow = Serial.readBytes(buffer, toRead);
                    
                    if (readNow > 0) {
                        file.write(buffer, readNow);
                        bytesReceived += readNow;
                        lastTimeout = millis();

                        Serial.println("ACK"); 

                        canvas.fillSprite(TFT_BLACK);
                        canvas.setTextColor(TFT_MAGENTA);
                        canvas.setTextDatum(middle_center);
                        canvas.drawString("DOWNLOADING VIA CABLE", canvas.width() / 2, canvas.height() / 2 - 10);
                        canvas.drawString(String((bytesReceived * 100) / fileSize) + "%", canvas.width() / 2, canvas.height() / 2 + 10);
                        canvas.pushSprite(0, 0);
                    }
                }
                if (millis() - lastTimeout > 5000) {
                    logTerminal = "ERR: UART TIMEOUT";
                    file.close();
                    currentState = STATE_MENU;
                    return;
                }
            }

            file.close();
            delay(100); 
            Serial.println("SUCCESS");
            logTerminal = "RECEIVED: " + filename;
            currentState = STATE_MENU;
            scanLittleFS();
        }
        
        else if (cmd.startsWith("DELETE:")) {
            String filename = "/" + cmd.substring(7);
            if (LittleFS.exists(filename)) {
                if (LittleFS.remove(filename)) {
                    Serial.println("DEL_OK");
                    logTerminal = "DELETED: " + filename;
                } else {
                    Serial.println("DEL_FAIL");
                }
            } else {
                Serial.println("DEL_NOT_FOUND");
            }
            scanLittleFS();
        }
    }
}

void injectFirmware(String filename) {
    currentState = STATE_FLASHING;
    File file = LittleFS.open(filename, "r");
    if (!file) { logTerminal = "ERR: OPEN FILE"; currentState = STATE_MENU; return; }

    const esp_partition_t* target = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!target) { logTerminal = "ERR: NO OTA_0"; currentState = STATE_MENU; return; }

    size_t fileSize = file.size();
    esp_ota_handle_t handle;
    if (esp_ota_begin(target, fileSize, &handle) != ESP_OK) {
        logTerminal = "ERR: OTA BEGIN"; currentState = STATE_MENU; return;
    }

    uint8_t buf[1024];
    size_t written = 0;
    while (file.available()) {
        size_t len = file.read(buf, 1024);
        esp_ota_write(handle, buf, len);
        written += len;

        canvas.fillSprite(TFT_BLACK);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(TFT_YELLOW);
        canvas.drawString("STARTING APP...", canvas.width() / 2, canvas.height() / 2 - 20);
        canvas.drawString(String((written * 100) / fileSize) + "%", canvas.width() / 2, canvas.height() / 2 + 10);
        canvas.pushSprite(0, 0);
        vTaskDelay(1);
    }
    file.close();

    if (esp_ota_end(handle) == ESP_OK && esp_ota_set_boot_partition(target) == ESP_OK) {
        canvas.fillSprite(TFT_BLACK);
        canvas.setTextColor(TFT_GREEN);
        canvas.setTextDatum(middle_center);
        canvas.drawString("LAUNCH SUCCESS!", canvas.width() / 2, canvas.height() / 2);
        canvas.pushSprite(0, 0);
        delay(500);
        ESP.restart();
    } else {
        logTerminal = "ERR: SET BOOT FAILED";
        currentState = STATE_MENU;
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    canvas.createSprite(M5.Display.width(), M5.Display.height());

    auto bootLog = [&](const char* tag, const char* name, bool ok, const char* extra = "") {
        canvas.fillSprite(TFT_BLACK);
        canvas.setTextSize(1);
        canvas.setTextDatum(top_left);

        canvas.setTextColor(0x07FF);
        canvas.drawString("NEXUS-BOOT v1.0 [ESP32-S3]", 2, 2);
        canvas.setTextColor(TFT_DARKGREY);
        canvas.drawString("----------------", 2, 12);

        static std::vector<String> lines;
        String color_tag = ok ? "" : "!";
        lines.push_back(String("[") + tag + "] " + name + (ok ? "....OK" : "..FAIL") + extra);
        
        int y = 22;
        for (auto& l : lines) {
            canvas.setTextColor(l.indexOf("FAIL") != -1 ? TFT_RED : TFT_GREEN);
            canvas.drawString(l, 2, y);
            y += 10;
        }
        canvas.pushSprite(0, 0);
        delay(80);
    };

    bootLog("SYS", "M5Unified", true);
    bootLog("SYS", "Display  ", true);
    bootLog("SYS", "Serial   ", true);

    bool fsOk = LittleFS.begin(true);
    bootLog("FS ", "LittleFS ", fsOk);

    if (!fsOk) {
        logTerminal = "ERR: FS INIT FAILED";
        currentState = STATE_ERROR;
        return;
    }

    bool factOk = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL) != NULL;
    bool otaOk  = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL) != NULL;
    bootLog("OTA", "factory  ", factOk);
    bootLog("OTA", "ota_0    ", otaOk);

    scanLittleFS();
    String found = binFiles.empty() ? " (0)" : " (" + String(binFiles.size()) + ")";
    String binLabel = "bin scan (" + String(binFiles.size()) + ")";
bootLog("FS ", binLabel.c_str(), true);
    delay(600);
}

void drawUI() {
    canvas.fillSprite(TFT_BLACK);
    canvas.setTextDatum(top_center);
    canvas.setTextColor(TFT_CYAN);
    canvas.drawString("NEXUS-BOOT v1.0", canvas.width() / 2, 5);
    canvas.drawLine(0, 20, canvas.width(), 20, TFT_DARKGREY);

    if (binFiles.empty()) {
        canvas.setTextColor(TFT_DARKGREY);
        canvas.drawString("NO FIRMWARES", canvas.width() / 2, canvas.height() / 2);
    } else {
        int yOffset = 30;
        for (int i = 0; i < binFiles.size(); i++) {
            if (i == selectedFile) {
                canvas.setTextColor(TFT_GREEN);
                canvas.drawString("> " + binFiles[i].substring(1) + " <", canvas.width() / 2, yOffset + (i * 20));
            } else {
                canvas.setTextColor(TFT_WHITE);
                canvas.drawString(binFiles[i].substring(1), canvas.width() / 2, yOffset + (i * 20));
            }
        }
    }

    canvas.drawLine(0, canvas.height() - 25, canvas.width(), canvas.height() - 25, TFT_DARKGREY);
    canvas.setTextColor(TFT_MAGENTA);
    canvas.drawString(logTerminal, canvas.width() / 2, canvas.height() - 18);
    canvas.pushSprite(0, 0);
}

void loop() {
    M5.update();

    if (currentState == STATE_MENU) {
        handleSerialCommands(); 
    }

    if (currentState == STATE_MENU && !binFiles.empty()) {
        if (M5.BtnB.wasClicked()) {
            selectedFile = (selectedFile + 1) % binFiles.size();
        }
        if (M5.BtnA.wasClicked()) {
            injectFirmware(binFiles[selectedFile]);
        }
    }

    static uint32_t lastFrameTime = 0;
    if (millis() - lastFrameTime >= (1000 / FPS_LIMIT)) {
        lastFrameTime = millis();
        if (currentState != STATE_FLASHING && currentState != STATE_RECEIVING) {
            drawUI();
        }
    }
}