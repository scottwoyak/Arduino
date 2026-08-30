//
// Over-the-air (OTA) firmware update display for Feather boards.
//
// Shows the current sketch version and a prompt to press Button A to check for and
// install a firmware update. While the update is downloading, the percent complete is
// shown on the display. Hardware: Feather display board with Button A support.
//

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_A_SUPPORTED
#error "This sketch requires a board with Button A support (e.g. Feather ESP32-S3)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "SerialX.h"
#include "WiFiSettings.h"

// URL of the firmware .bin to download when an update is requested.
constexpr auto FIRMWARE_URL = "https://github.com/scottwoyak/Arduino/releases/download/OTA_Display/OTA_Display.ino.bin";

constexpr uint8_t HEADER_SIZE = 3;
constexpr uint8_t TEXT_SIZE = 2;

constexpr int16_t PROGRESS_BAR_HEIGHT = 12;
constexpr int16_t PROGRESS_BAR_MARGIN = 4;

Arduino arduino;
Format percentFormat("###%", Format::Alignment::RIGHT);
int16_t downloadRowY = 0;
int16_t progressBarY = 0;

void showReadyScreen();
void performUpdate();

void setup()
{
   SerialX::begin();

   arduino.begin();

   showReadyScreen();
}

void loop()
{
   if (arduino.buttonA.wasPressed())
   {
	  performUpdate();
	  showReadyScreen();
   }
}

///
/// <summary>
/// Draws the idle screen showing the sketch version and the Button A prompt.
/// </summary>
///
void showReadyScreen()
{
   arduino.clearDisplay();

   arduino.setTextSize(HEADER_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("OTA Update Demo", Color::HEADING);

   arduino.setTextSize(TEXT_SIZE);
   arduino.println();
   arduino.print("Press ", Color::LABEL);
   arduino.print("Button A", Color::VALUE);
   arduino.println(" to", Color::LABEL);
   arduino.println("update firmware", Color::LABEL);
}

///
/// <summary>
/// Reports OTA download progress as a percentage on the display.
/// </summary>
/// <param name="current">Number of bytes downloaded so far.</param>
/// <param name="total">Total number of bytes to download.</param>
///
void onUpdateProgress(int current, int total)
{
   uint8_t percent = (total > 0) ? (current * 100 / total) : 0;

   arduino.setTextSize(TEXT_SIZE);
   arduino.setCursorY(downloadRowY);
   arduino.printR((float)percent, percentFormat, Color::VALUE);

   int16_t barWidth = arduino.width() - 2 * PROGRESS_BAR_MARGIN;
   int16_t fillWidth = barWidth * percent / 100;
   arduino.fillRect(PROGRESS_BAR_MARGIN, progressBarY, fillWidth, PROGRESS_BAR_HEIGHT, Color::LIME);
}

///
/// <summary>
/// Connects to WiFi and performs the OTA firmware update, showing progress and the
/// final result on the display.
/// </summary>
///
void performUpdate()
{
   arduino.printHeader("Updating Firmware");

   if (!arduino.initWifi(WIFI_SSID, WIFI_PASSWORD))
   {
	  arduino.println("WiFi connect failed", Color::RED);
	  delay(3000);
	  return;
   }

   arduino.setTextSize(TEXT_SIZE);
   arduino.print("Downloading...", Color::LABEL);
   downloadRowY = arduino.getCursorY();

   progressBarY = downloadRowY + arduino.charH() + PROGRESS_BAR_MARGIN;
   arduino.fillRect(PROGRESS_BAR_MARGIN, progressBarY, arduino.width() - 2 * PROGRESS_BAR_MARGIN, PROGRESS_BAR_HEIGHT, Color::DARKGRAY);

   WiFiClientSecure client;
   client.setInsecure();

   httpUpdate.onProgress(onUpdateProgress);
   httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
   httpUpdate.rebootOnUpdate(false);

   t_httpUpdate_return result = httpUpdate.update(client, FIRMWARE_URL);

   switch (result)
   {
	  case HTTP_UPDATE_FAILED:
		  arduino.println();
		  arduino.println("Update failed", Color::RED);
		  arduino.println(httpUpdate.getLastErrorString().c_str(), Color::RED);
		  Serial.printf("HTTP Update failed: %s\n", httpUpdate.getLastErrorString().c_str());
		  delay(3000);
		  break;

	  case HTTP_UPDATE_NO_UPDATES:
		 arduino.println();
		 arduino.println("No update available", Color::RED);
		 delay(3000);
		 break;

	  case HTTP_UPDATE_OK:
		  arduino.println("\n\nRestarting...", Color::LABEL);
		  ESP.restart();
		  break;
   }
}
