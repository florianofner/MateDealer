#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Adafruit_PN532.h>

#define VERSION 5
#define IO_PN532_SS     (D4)

//   MOSI <---> D7
//   MISO <---> D6
//    SCK <---> D5
//     SS <---> D4

Adafruit_PN532 nfc(IO_PN532_SS);

bool nfcReaderInitialized = false;
HTTPClient http;
ESP8266WebServer httpd(80);
WiFiUDP udp;
IPAddress ip(10,56,1,188);
const char bufferTest[] = "Boot complete.";

char buffer[255];
String token = "";
String currentUserID = "";

const char * header = R"(<!DOCTYPE html>
<html>
<head>
<title>matedealer-backpack</title>
<link rel="stylesheet" href="/style.css">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no" />
</head>
<body>
<div id="top">
        <span id="title">matedealer-backpack</span>
        <a href="/">Configuration</a>
        <a href="/debug">Debug</a>
</div>
)";

void spiffsWrite(String path, String contents) {
    File f = SPIFFS.open(path, "w");
    f.println(contents);
    f.close();
}
String spiffsRead(String path) {
    File f = SPIFFS.open(path, "r");
    String x = f.readStringUntil('\n');
    f.close();
    return x;
}

bool requestIsAuthorized() {
    return ( httpd.client().remoteIP().toString() == "10.56.0.11" );
}
void send302(String dest) {
    httpd.sendHeader("Location", dest, true);
    httpd.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    httpd.sendHeader("Pragma", "no-cache");
    httpd.sendHeader("Expires", "-1");
    httpd.send ( 302, "text/plain", "");
    httpd.client().stop();
}        

void setup()
{
    //Serial.begin(115200);
    Serial.begin(38400);
    Serial.println();
    delay(200);
    Serial.print("MateDealer BACKPACK v"); Serial.println(VERSION);

    Serial.print("Mounting disk... ");
    FSInfo fs_info;
    SPIFFS.begin();
    if ( ! SPIFFS.info(fs_info) ) {
        //the FS info was not retrieved correctly. it's probably not formatted
        Serial.print("failed. Formatting disk... ");
        SPIFFS.format();
        Serial.print("done. Mounting disk... ");
        SPIFFS.begin();
        SPIFFS.info(fs_info);
    }
    Serial.print("done. ");
    Serial.print(fs_info.usedBytes); Serial.print("/"); Serial.print(fs_info.totalBytes); Serial.println(" bytes used");
    
    Serial.println("Checking version");
    String lastVerString = spiffsRead("/version");
    if ( lastVerString.toInt() != VERSION ) {
        //we just got upgrayedded or downgrayedded
        Serial.print("We just moved from v"); Serial.println(lastVerString);
        spiffsWrite("/version", String(VERSION));
        Serial.print("Welcome to v"); Serial.println(VERSION);
    }

    Serial.println("Starting wireless.");
    WiFi.hostname("matedealer-backpack");
    WiFiManager wifiManager; //Load the Wi-Fi Manager library.
    wifiManager.setTimeout(300); //Give up with the AP if no users gives us configuration in this many secs.
    if(!wifiManager.autoConnect("matedealer-backpack Setup")) {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        ESP.restart();
    }
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());

    Serial.println("Starting OTA.");
    /*
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    */
    ArduinoOTA.setHostname("matedealer-backpack");
    ArduinoOTA.begin();

    Serial.println("Starting Web server.");
    httpd.on("/style.css", [&](){
        httpd.send(200, "text/css",R"(
            html {
                font-family:sans-serif;
                background-color:black;
                color: #e0e0e0;
            }
            div {
                background-color: #202020;
            }
            h1,h2,h3,h4,h5 {
                color: #2020e0;
            }
            a {
                color: #50f050;
            }
            form * {
                display:block;
                border: 1px solid #000;
                font-size: 14px;
                color: #fff;
                background: #004;
                padding: 5px;
            }
        )");
        httpd.client().stop();
    });
    httpd.on("/", [&](){
        httpd.setContentLength(CONTENT_LENGTH_UNKNOWN);
        httpd.send(200, "text/html", header);
        httpd.client().stop();
    });
    httpd.on("/update", HTTP_POST, [&](){
        httpd.sendHeader("Connection", "close");
        httpd.sendHeader("Access-Control-Allow-Origin", "*");
        httpd.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
    },[&](){
        // handler for the file upload, get's the sketch bytes, and writes
        // them through the Update object
        HTTPUpload& upload = httpd.upload();
        if(upload.status == UPLOAD_FILE_START){
            Serial.printf("Starting HTTP update from %s - other functions will be suspended.\r\n", upload.filename.c_str());
            WiFiUDP::stopAll();

            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if(!Update.begin(maxSketchSpace)){//start with max available size
                Update.printError(Serial);
            }
        } else if(upload.status == UPLOAD_FILE_WRITE){
            Serial.print(upload.totalSize); Serial.printf(" bytes written\r");

            if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
                Update.printError(Serial);
            }
        } else if(upload.status == UPLOAD_FILE_END){
            if(Update.end(true)){ //true to set the size to the current progress
                Serial.printf("Update Success: %u\n", upload.totalSize);
                ESP.restart();
            } else {
                Update.printError(Serial);
            }
        } else if(upload.status == UPLOAD_FILE_ABORTED){
            Update.end();
            Serial.println("Update was aborted");
        }
        delay(0);
    });
    httpd.on("/debug", [&](){
        String content = header;
        content += ("<h1>Debug</h1><ul>");

        unsigned long uptime = millis();
        content += (String("<li>Version ") + VERSION + "</li>");
        content += (String("<li>Booted about ") + (uptime/60000) + " minutes ago (" + ESP.getResetReason() + ")</li>");
        content += (String("<li>Raw Vcc: ") + ESP.getVcc() + ")</li>");
        content += ("</ul>");
    
        content += (R"(
            <h2>Debugging buttons</h2>
            <form method="POST" action="/debug/start-session">
                <button type="submit">start-session 1337</button>
            </form>
            <form method='POST' action='/debug/approve-vend'>
                <button type='submit'>approve-vend</button>
            </form>
            <form method="POST" action="/debug/deny-vend">
                <button type="submit">deny-vend</button>
            </form>
        )");
        httpd.send(200, "text/html", content);
    });
    httpd.on("/debug/start-session", [&](){
        Serial.println("start-session 1337");
        send302("/debug?done");
    });
    httpd.on("/debug/approve-vend", [&](){
        Serial.println("approve-vend");
        send302("/debug?done");
    });
    httpd.on("/debug/deny-vend", [&](){
        Serial.println("deny-vend");
        send302("/debug?done");
    });
    httpd.begin();

    Serial.print("Loading token from disk... ");
    if ( SPIFFS.exists("/token") ) {
        token = spiffsRead("/token");
        Serial.println(token);
    } else {
        Serial.println("Not found. Generating...");
        String letters[]= {"a", "b", "c", "d", "e", "f","g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
        for ( int i = 0; i < 40; i++ ) {
                token = token + letters[random(0, 36)];
        }
        Serial.println("Done generating. Please wait 10 seconds...");
        delay(10000);
        Serial.print("Here it is: ");
        Serial.println(token);
        spiffsWrite("/token", token);
        Serial.println("Written to disk.");
    }

    Serial.print("Starting NFC... ");
    nfc.begin();
    if ( initializeNFCReader() ) {
        nfcReaderInitialized = true;
        Serial.println("OK");
    } else {
        Serial.println("No NFC for now.");
    }

    Serial.println("Startup complete. Shifting bitrate and starting interface to atmega.");
    delay(500);
    Serial.end();
    delay(500);
    Serial.begin(38400);
    //Serial.swap();

    udp.beginPacket(ip,12345);
    udp.write(bufferTest);
    udp.endPacket();
}

void loop()
{
    
    ArduinoOTA.handle();
    httpd.handleClient();

    if ( Serial.available() ) {
        String command = Serial.readString();

        command.toCharArray(buffer, 254);
        udp.beginPacket(ip, 12345);
        udp.write(buffer);
        udp.endPacket();

        if ( command.indexOf("vend-request") > -1 ) {
            //Send the charge request
            if ( chargeAccount(currentUserID, 0.50) ) {
                //Charge succeeded, approve the vend
                Serial.println("approve-vend");
            } else {
                Serial.println("deny-vend");
            }
            currentUserID = "";
        } 
    }

    if ( isNFCReaderPresent() ) {
        if ( ! nfcReaderInitialized ) {
            nfcReaderInitialized = initializeNFCReader();
        }
        
        String nfcID = checkNFC();
        if ( nfcID.length() > 0 ) {
            //found a tag
            nfcID.toCharArray(buffer, 254);
            udp.beginPacket(ip, 12345);
            udp.write(buffer);
            udp.endPacket();

            Serial.println(String("start-session ") + String(getBalanceForAccount(getUserIDForTag(nfcID)) * 100));
        }
    } else {
        nfcReaderInitialized = false;
        delay(1000);
    }

}



String checkNFC() {
    boolean success;
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };   // Buffer to store the returned UID
    uint8_t uidLength;                         // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
    String strUID = "";
  
    // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
    // 'uid' will be populated with the UID, and uidLength will indicate
    // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength, 1000);
  
    if (success) {
        for (uint8_t i=0; i < uidLength; i++) {
            strUID += hexlify(uid[i]);
        }
    }
    return strUID;
}

String hexlify(uint8_t byte) {
    // Conver the integer to a string with the hex value:
    String hexedString = String(byte, HEX);

    // Add prepended '0' if needed:
    if (hexedString.length() == 1) {
        hexedString = "0" + hexedString;
    }

    return hexedString;
}

void determineCurrentState() {
    if (isDoorClosed()) {
        lockDoor();
        currentState = DOOR_CLOSED_AND_LOCKED;
    } else {
        unlockDoor();
        currentState = DOOR_OPEN_AND_UNLOCKED;
    }
}
bool initializeNFCReader() {
    if ( isNFCReaderPresent() ) {
        nfc.setPassiveActivationRetries(0xFE);
        nfc.SAMConfig();
        return true;
    }
    return false;
}

bool isNFCReaderPresent() {
    uint32_t versiondata = nfc.getFirmwareVersion();
    if ( versiondata ) {
        return true;
    }
    return false;
}

