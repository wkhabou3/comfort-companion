/*
Test MicroSD with WAV player by loading a wav file from the card and writing it to the audio amp.
*/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "driver/i2s_std.h"

#define SD_CS GPIO_NUM_22
#define SD_MOSI GPIO_NUM_19
#define SD_MISO GPIO_NUM_20
#define SD_SCK GPIO_NUM_21

#define I2S_BCLK GPIO_NUM_11
#define I2S_LRC  GPIO_NUM_23
#define I2S_DOUT GPIO_NUM_10

#define SAMPLE_RATE 44100
#define DMA_BUF_LEN 512

SPIClass sdSPI(FSPI);
static i2s_chan_handle_t tx_chan;

void setupI2S() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_chan, NULL);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK,
            .ws   = I2S_LRC,
            .dout = I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    i2s_channel_init_std_mode(tx_chan, &std_cfg);
    i2s_channel_enable(tx_chan);
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }

  File file = root.openNextFile();

  while (file) {
    if (file.isDirectory()) {
      Serial.print("DIR : ");
      Serial.println(file.name());
    } else {
      Serial.print("FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("Initializing SD card...");

  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("Card Mount Failed");
    return;
  }

  Serial.println("SD card initialized.");
  Serial.println("Files on card:");

  listDir(SD, "/", 0);

  setupI2S();

  playWav("/clair.wav");
}

void loop() {
}

void playWav(const char *filename) {
    File file = SD.open(filename);
    if (!file) {
        Serial.println("Failed to open file");
        return;
    }

    // Skip WAV header (44 bytes)
    file.seek(44);

    int16_t buffer[DMA_BUF_LEN * 2];  // stereo
    size_t bytesWritten;

    Serial.println("Playing...");

    while (file.available()) {
        int bytesToRead = min((int)file.available(), (int)sizeof(buffer));
        file.read((uint8_t*)buffer, bytesToRead);
        i2s_channel_write(tx_chan, buffer, bytesToRead, &bytesWritten, portMAX_DELAY);
    }

    Serial.println("Playback finished.");
    file.close();
}