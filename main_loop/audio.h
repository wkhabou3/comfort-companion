/*
Header file for audio functionality, including pinouts, constants, and function declarations.
*/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <math.h>
#include "driver/i2s_std.h"

// microSD breakout pinout
#define SD_CS GPIO_NUM_22
#define SD_MOSI GPIO_NUM_19
#define SD_MISO GPIO_NUM_20
#define SD_SCK GPIO_NUM_21

// I2S amp pinout
#define I2S_BCLK GPIO_NUM_11
#define I2S_LRC  GPIO_NUM_23
#define I2S_DOUT GPIO_NUM_10

// Maximum number of samples to write to the DMA buffer at a time
#define DMA_BUF_LEN 512

// Audio sample rate (all files on microSD must be 44100Hz)
#define SAMPLE_RATE 44100

// Size of sin lookup table (much easier computationally to play sin tones)
#define SIN_TABLE_SIZE 256

// Headers used to configure I2s channel per WAV file
struct WAVHeader {
  char riff[4];
  uint32_t fileSize;
  char wave[4];
  char fmt[4];
  uint32_t fmtSize;
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
  char data[4];
  uint32_t dataSize;
};

/*
Initialize sine lookup table.

Reduces computational time to play sin wave tones.
*/
void initSinTable();

/*
Set up and configure I2S channel based on given sample rate and mono/stereo output.

Params:
- sampleRate: sampling rate in Hz
- numChannels: 1 for mono, 2+ for stereo
*/
void setupI2S(uint32_t sampleRate, uint16_t numChannels);

/*
Generates and plays sine wave tone for specified duration.

Params:
- frequency: tone frequency in Hz
- duration: tone duration in ms
*/
void playTone(float frequency, int duration);

/*
Reads a WAV file from the microSD card and plays it on the speaker.

Params:
- filename: full path to WAV file on microSD card
*/
void playWav(const char *filename);
