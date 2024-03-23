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
#endif

#include <Wire.h>
// #define DAC_ADDRESS (0x36>>1)
#define DAC_ADDRESS 0x1B
#define SUB_ADDRESS_VOL 0x7
#define VAL_VOLUME_NORMAL 0x17

#define TAS5713_CLOCK_CTRL 0x00
#define TAS5713_DEVICE_ID 0x01
#define TAS5713_ERROR_STATUS 0x02
#define TAS5713_SYSTEM_CTRL1 0x03
#define TAS5713_SERIAL_DATA_INTERFACE 0x04
#define TAS5713_SYSTEM_CTRL2 0x05
#define TAS5713_SOFT_MUTE 0x06
#define TAS5713_VOL_MASTER 0x07
#define TAS5713_VOL_CH1 0x08
#define TAS5713_VOL_CH2 0x09
#define TAS5713_VOL_HEADPHONE 0x0A
#define TAS5713_OSC_TRIM 0x1B

// AudioBoardStream i2s(AudioKitEs8388V1);  // or replace with e.g. 
I2SStream i2s;
//WAVDecoder pcm;
OpusAudioDecoder opus;
WiFiClient wifi;
SnapProcessorRTOS rtos(1024*8); // define queue with 8 kbytes
SnapTimeSyncDynamic synch(0, 10); // optional configuratioin
SnapClient client(wifi, i2s, opus);


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

void dacSet(uint8_t address, uint8_t value) {
  Wire.beginTransmission(DAC_ADDRESS);
  Wire.write(address);
  Wire.write(value);
  Wire.endTransmission(); 
}

void dacSetChunk( int size, const char * data) {
  Wire.beginTransmission(DAC_ADDRESS);
  for(int i = 0; i<size; i++)
  Wire.write(data[i]);
  Wire.endTransmission(); 
}

uint8_t dacRead(uint8_t address) {
  Wire.requestFrom(DAC_ADDRESS, 1);    
    // Slave may send less than requested
    while(Wire.available()) {
        uint8_t c = Wire.read();    // Receive a byte as character
        Serial.print(c);         // Print the character
        return c;
    }
    return 0;
}

void scanI2CDevices() {
   byte error, address;
  int nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address<16) {
        Serial.print("0");
      }
      Serial.println(address,HEX);
      nDevices++;
    }
    else if (error==4) {
      Serial.print("Unknow error at address 0x");
      if (address<16) {
        Serial.print("0");
      }
      Serial.println(address,HEX);
    }    
  }
  if (nDevices == 0) {
    Serial.println("No I2C devices found\n");
  }
  else {
    Serial.println("done\n");
  }
}

void dacClearError(){
  dacSet(TAS5713_ERROR_STATUS, 0x00);
}



// init sequence used from https://github.com/sle118/squeezelite-esp32/blob/master-v4.3/components/squeezelite/tas57xx/dac_5713.c
void setupDAC() {
  // pinMode(2, INPUT_PULLUP);  
  Wire.begin(2,4,100000);
  Wire.setClock(100000);
  // scanI2CDevices();

  uint8_t ret = dacRead(0x00);
  if(ret == 255) {
    Serial.println("DAC not found");
  } else {
    Serial.print("DAC found: ");
    Serial.println(ret);
  }

  dacClearError();
  dacSet(TAS5713_OSC_TRIM, 0x00); /* a delay is required after this */
  delay(1000);
  dacClearError();
  dacSet(TAS5713_CLOCK_CTRL, 0x60);
  dacSet(TAS5713_SERIAL_DATA_INTERFACE, 0x05); /* I2S  LJ 16 bit */
  // dacSet(TAS5713_SERIAL_DATA_INTERFACE, 0x05); /* I2S  LJ 24 bit */
  Serial.println("Read error"); dacRead(0x02);
  dacSet(TAS5713_SYSTEM_CTRL2, 0x00); /* exit all channel shutdown */
  dacSet(TAS5713_SOFT_MUTE, 0x00);    /* unmute */
  dacSet(TAS5713_VOL_MASTER, 0x00);
  dacSet(TAS5713_OSC_TRIM, 0x00);
  // dacSet(TAS5713_VOL_CH1, 0x30);
  // dacSet(TAS5713_VOL_CH2, 0x30);
  // dacSet(TAS5713_VOL_HEADPHONE, 0x80);
  Serial.println("Read error"); dacRead(0x02);

  // Now start programming the default initialization sequence
  int i = 0;
	while (++i) {
    if(tas5713_init_sequence[i].size == 0) break;

    dacSetChunk(tas5713_init_sequence[i].size, tas5713_init_sequence[i].data);

		// if (ret < 0) {
		// 	Serial.print("TAS5713 CODEC PROBE: InitSeq returns: ");
    //   Serial.println(ret);
		// }
	}
	
	// Unmute
	// dacSet(TAS5713_SYSTEM_CTRL2, 0x00);
  dacSet(TAS5713_SYSTEM_CTRL2, 0x00);
  dacSet(TAS5713_SERIAL_DATA_INTERFACE, 0x03);
  dacSet(TAS5713_SOFT_MUTE, 0x00);
  
}

void setup() {
  Serial.begin(115200);
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
  cfg.bits_per_sample = 32;
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
  setupDAC();
}

void loop() {
  client.doLoop();
}