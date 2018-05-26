#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#define VERSION 5

HTTPClient http;
ESP8266WebServer httpd(80);
WiFiUDP udp;
IPAddress ip(10,56,1,188);
const char bufferTest[] = "Boot complete.";

char buffer[255];
String token = "";

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
	Serial.begin(115200);
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

        if ( command.indexOf("vend-request") > 0 ) {
            //Send the charge request
            if ( false ) {
                //Charge succeeded, approve the vend
		        Serial.println("approve-vend");
            } else {
                Serial.println("deny-vend");
            }
        } 
	}

}



