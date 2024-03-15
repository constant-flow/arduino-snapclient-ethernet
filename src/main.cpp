#include <Arduino.h>
#include "AudioTools.h"
#include "SnapClient.h"
#include "api/SnapProcessorRTOS.h" // install https://github.com/pschatzmann/arduino-freertos-addons
// #include "AudioLibs/AudioBoardStream.h" // install https://github.com/pschatzmann/arduino-audio-driver
#include "AudioCodecs/CodecOpus.h" // https://github.com/pschatzmann/arduino-libopus
#ifdef CONFIG_ETHERNET
#include <ETH.h>
#endif
// AudioBoardStream out(AudioKitEs8388V1);  // or replace with e.g. 
I2SStream out;
//WAVDecoder pcm;
OpusAudioDecoder opus;
WiFiClient wifi;
SnapProcessorRTOS rtos(1024*8); // define queue with 8 kbytes
SnapTimeSyncDynamic synch(0, 10); // optional configuratioin
SnapClient client(wifi, out, opus);

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
#endif

void setup() {
  Serial.begin(115200);

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
#endif

  auto cfg = out.defaultConfig();
  // initialize output
  #ifdef CONFIG_ETHERNET
  cfg.pin_bck = 14;
  cfg.pin_ws = 12;
  cfg.pin_data = 15;
  //cfg.buffer_size = 512;
  //cfg.buffer_count = 6;
  #else
  cfg.pin_bck = 14;
  cfg.pin_ws = 15;
  cfg.pin_data = 22;
  #endif
  out.begin(cfg);

  // use full volume of kit - volume control done by client
  // out.setVolume(1.0);

  // Use FreeRTOS
  client.setSnapProcessor(rtos);

  // Define CONFIG_SNAPCAST_SERVER_HOST in SnapConfig.h or here
  client.setServerIP(IPAddress(192,168,1,33));

  // start snap client
  client.begin(synch);
}

void loop() {
  client.doLoop();
}