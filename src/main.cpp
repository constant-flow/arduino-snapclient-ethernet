#include <Arduino.h>
#include "AudioTools.h"
#include "SnapClient.h"
#include "api/SnapProcessorRTOS.h" // install https://github.com/pschatzmann/arduino-freertos-addons
// #include "AudioLibs/AudioBoardStream.h" // install https://github.com/pschatzmann/arduino-audio-driver
#include "AudioCodecs/CodecOpus.h" // https://github.com/pschatzmann/arduino-libopus
#ifdef CONFIG_ETHERNET
#include <ETH.h>
#include <SPI.h>
#include <ESPmDNS.h>                   // Bonjour/multicast DNS, finds the device on network by name   
#include <ArduinoOTA.h>                // OTA Upload via ArduinoIDE
#include "tas5713.h"
#include "AudioTools.h"
#endif


// AudioBoardStream i2s(AudioKitEs8388V1);  // or replace with e.g. 
I2SStream i2s;
//WAVDecoder pcm;
OpusAudioDecoder opus;
WiFiClient wifi;
SnapProcessorRTOS rtos(1024*8); // define queue with 8 kbytes
SnapTimeSyncDynamic synch(0, 10); // optional configuratioin
NumberFormatConverterStream nfc(i2s);
SnapClient client(wifi, nfc, opus);



// allows OTA only shortly after boot
#define MAX_INITIAL_OTA_TIME 5000

#ifdef CONFIG_ETHERNET
static bool eth_connected = false;
static bool eth_reboot_on_reconnect = false;

void EthernetEvent(WiFiEvent_t event)
{
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname("esp32-ethernet");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      if(eth_reboot_on_reconnect) ESP.restart();
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_reboot_on_reconnect = true;
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_reboot_on_reconnect = true;
      eth_connected = false;
      break;
    default:
      break;
  }
}

void ArduinoOTASetup() {
  //IDE OTA
#ifdef ARDUINO_ARCH_ESP32
  char myhostname[16] {"esp"};         // you could use your TXT_BOARDNAME also. The sketch will add the board ID.
  char val[8];
  itoa(1, val, 10);            // before we can concatenate the precompiler integer, we have to convert it to char array
  strcat(myhostname, val);
  ArduinoOTA.setHostname(myhostname);  // give a name to your ESP for the Arduino IDE - not needed for ESP8266
#endif
  ArduinoOTA.begin();                  // OTA Upload via ArduinoIDE https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html
  // OTA Messages
  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static byte previous = 0;
    byte result = progress / (total / 100);
    if (previous != result) {
      if ((result % 10) == 0) {
        Serial.print(result);
        Serial.println("%");
      }
      else Serial.print(".");
    }
    previous = result;
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("\nOTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("\nAuth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("\nBegin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("\nConnect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("\nReceive Failed");
    else if (error == OTA_END_ERROR) Serial.println("\nEnd Failed");
  });
}

#endif

void setup() {
  Serial.begin(115200);
  setupDAC();
  
  AudioLogger::instance().begin(Serial, AudioLogger::Info);  

#ifndef CONFIG_ETHERNET
  // login to wifi -> Define values in SnapConfig.h or replace them here
  WiFi.begin(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  // print ip address
  Serial.println();
  Serial.println(WiFi.localIP());
#else
  WiFi.onEvent(EthernetEvent);
  ETH.begin();
  while (eth_connected != true) {
    Serial.print("Connecting to Ethernet ..");
    Serial.print('.');
    delay(1000);
  }

  ETH.setHostname("esp32-ethernet");
  ArduinoOTASetup();
#endif

  auto cfg = i2s.defaultConfig();
  // initialize output
  #ifdef CONFIG_ETHERNET
  cfg.pin_bck = 14;
  cfg.pin_ws = 12;
  cfg.pin_data = 15;
  // cfg.bits_per_sample = 32;
  //cfg.buffer_size = 512;
  //cfg.buffer_count = 6;
  #else
  cfg.pin_bck = 14;
  cfg.pin_ws = 15;
  cfg.pin_data = 22;
  cfg.bits_per_sample = 32;
  #endif
  i2s.begin(cfg);

  // use full volume of kit - volume control done by client
  // i2s.setVolume(1.0);

  // Use FreeRTOS
  client.setSnapProcessor(rtos);

  // Define CONFIG_SNAPCAST_SERVER_HOST in SnapConfig.h or here
  client.setServerIP(IPAddress(192,168,1,33));

  // start snap client
  client.begin(synch);

  Serial.println("Allow OTA updates"); 
  long ota_start_time = millis();
  while(millis()-ota_start_time < MAX_INITIAL_OTA_TIME) {
    ArduinoOTA.handle();                 // OTA Upload via ArduinoIDE
    delay(10);
  }
  Serial.println("Stop allowing OTA updates");
  nfc.begin(16, 32);
}

void loop() {
  client.doLoop();

}