/*
Header file for audio functionality, including pinouts, constants, and function declarations.
*/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <math.h>
#include "driver/i2s_std.h"

// microSD breakout pinout
#define SD_CS GPIO_NUM_42
#define SD_MOSI GPIO_NUM_39
#define SD_MISO GPIO_NUM_40
#define SD_SCK GPIO_NUM_41

// I2S amp pinout
#define I2S_BCLK GPIO_NUM_18
#define I2S_LRC  GPIO_NUM_16
#define I2S_DOUT GPIO_NUM_17

// SD pin (set low to mute amp)
// Use during boot and during low-power
#define AMP_SD   GPIO_NUM_8

// Maximum number of samples to write to the DMA buffer at a time
#define DMA_BUF_LEN 512

// Audio sample rate (all files on microSD must be 44100Hz)
#define SAMPLE_RATE 44100

// Size of sin lookup table (much easier computationally to play sin tones)
#define SIN_TABLE_SIZE 256

enum class VolumeLevel {QUIET, MODERATE, LOUD, COUNT};

// Increment operator to cycle through VolumeLevels
VolumeLevel& operator++(VolumeLevel& v);

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

// Button interrupt
void IRAM_ATTR stopAudioISR();

// Access and set audio abort flag
void setAudioAbort(bool val);

// Return toneActive; can be used for iteration until playback finishes
bool isToneActive();

// Return wavActive; can be used for iteration until wav playback finishes
bool isWavActive();

/*
Access and set playback volume.

Volume 0: quiet
Volume 1: moderate
Volume 2: loud

Params:
- volume: requested volume level (see enum VolumeLevel)
*/
void setVolume(VolumeLevel volume);

/*
Initialize sine lookup table.

Reduces computational time to play sin wave tones.
*/
void initSinTable();

/*
Set up and configure stereo I2S channel based on given sample rate and mono/stereo.
If first run, 

Params:
- sampleRate: sampling rate in Hz
- numChannels: number of channels (1 for mono, 2 for stereo)
*/
void setupI2S(uint32_t sampleRate, uint16_t numChannels);

/*
Reconfigure the I2S channel as needed, changing the sample rate and switching between mono/stereo.
If sample rate and number of channels are same as before, return without doing anything.

Params:
- sampleRate: the sample rate to switch to.
- numChannels: the number of channels to switch to.
*/
void reconfigureI2S(uint32_t sampleRate, uint16_t numChannels);

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

/*
Main audio loop. Should run continuously in parallel to other tasks.
Keep the I2S channel active by sending silence (zeros) if nothing is playing.
Use toneActive (set by playTone) and wavActive (set by playWav) flags to determine what audio to play.
Only plays mono audio.
*/
void audioTask(void *param);
