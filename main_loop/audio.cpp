/*
Implementation of all audio functionality, including:
- sine table initialization
- I2S channel creation
- Tone generation
- Stored audio playback
*/

#include "audio.h"


static i2s_chan_handle_t tx_chan;   // I2S channel handle
SemaphoreHandle_t i2sMutex;         // Mutex to manage I2S channel config and writes atomically
volatile bool i2sAlreadySetup;      // Flag determining if setupI2S was run at least once

// Stereo buffer
int16_t buffer[DMA_BUF_LEN * 2];

// Separate volume variables for pure sine tones and stored audio playback
// Tone volume should be between 0 and 1
float toneVolume = 0.05;
float playbackVolume = 0.2;

// Shared control variables
uint32_t currentSampleRate = 0;
uint16_t currentChannels = 0;
volatile bool toneActive = false;
volatile bool wavActive  = false;
float currentFrequency   = 440.0;
uint32_t toneSamplesRemaining = 0;
float phase = 0.0;
float phase_step = 0.0;

// WAV playback state
File wavFile;
WAVHeader wavHeader;
int wavBytesRemaining = 0;
int wavReadIndex = 0;

// Variables to manage SD file reads during file playback
volatile bool wavStartRequested = false;    // Use request mechanism; audio task handles SD reads
char nextFilename[64];

// Temporary buffer for WAV read
int16_t wavBuffer[DMA_BUF_LEN * 2];

// Allocate memory for sine lookup table
int16_t sinTable[SIN_TABLE_SIZE];

VolumeLevel& operator++(VolumeLevel& v) {
  v = static_cast<VolumeLevel>((static_cast<int>(v) + 1) % static_cast<int>(VolumeLevel::COUNT));
  return v;
}

// Button interrupt
void IRAM_ATTR stopAudioISR() {
  toneActive = false;
  wavActive = false;
}

void setAudioAbort(bool val) {
  toneActive = false;
  wavActive = false;
}

bool isToneActive() {
  return toneActive;
}

bool isWavActive() {
  return wavActive;
}

void setVolume(VolumeLevel volume) {
  switch (volume) {
    case VolumeLevel::QUIET:
      toneVolume = 0.083;
      playbackVolume = 0.25;
      break;
    case VolumeLevel::MODERATE:
      toneVolume = 0.17;
      playbackVolume = 0.5;
      break;
    default:  // loud
      toneVolume = 0.33;
      playbackVolume = 1;
  }
}

void initSinTable() {
  for (int i = 0; i < SIN_TABLE_SIZE; i++) {
    // Pre-compute sin values (peak-to-peak is (2^16)-1, which is full audio range)
    sinTable[i] = (int16_t) (sin(2.0 * M_PI * i / SIN_TABLE_SIZE) * 32767);
  }
}

void setupI2S(uint32_t sampleRate, uint16_t numChannels) {
  // Shut down amp during init
  pinMode(AMP_SD, OUTPUT);
  digitalWrite(AMP_SD, LOW);

  // Create I2S TX channel
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));

  // Standard I2S (Philips) configuration
  i2s_std_config_t std_cfg = {
    .clk_cfg = {
      .sample_rate_hz = sampleRate,
      .clk_src = I2S_CLK_SRC_XTAL,
      .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    },

    .slot_cfg = {
      .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
      .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
      .slot_mode = I2S_SLOT_MODE_STEREO,
      .slot_mask = I2S_STD_SLOT_BOTH,
      .ws_width = 16,
      .ws_pol = false,
      .bit_shift = true,                   // Philips I2S
      .left_align = false,
      .big_endian = false,
      .bit_order_lsb = false
    },

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

  // Create mutex and set flag if this function is run for the first time
  if (!i2sAlreadySetup) {
    i2sMutex = xSemaphoreCreateMutex();
    i2sAlreadySetup = true;
  }

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

  delay(50);
  digitalWrite(AMP_SD, HIGH);

  // Set sample rate and number of channels
  currentSampleRate = sampleRate;
  currentChannels = numChannels;
}

void playTone(float frequency, int duration) {
  toneActive = true;
  wavActive = false;

  // Set frequency, step, and total number of samples
  currentFrequency = frequency;
  phase_step = SIN_TABLE_SIZE * frequency / currentSampleRate; 
  toneSamplesRemaining = currentSampleRate * duration / 1000;
}

void playWav(const char *filename) {
  // Copy filename to nextFilename, then set request flag for main audio task
  strncpy(nextFilename, filename, sizeof(nextFilename));
  wavStartRequested = true;
  toneActive = false;
  delay(100);
}

void audioTask(void *param) {
  size_t bytesWritten;

  // Current volume state
  float currentToneVolume = toneVolume;
  float currentPlaybackVolume = playbackVolume;
  // volume ramp speeds
  const float toneVolumeStep = 0.005f;
  const float wavVolumeStep = 0.0001f;

  while (true) {
    if (wavStartRequested) {
      wavStartRequested = false;

      // Stop current playback cleanly
      if (wavFile) {
        wavFile.close();
      }

      wavActive = false;

      // Reset ALL state
      wavBytesRemaining = 0;
      wavReadIndex = 0;

      // Open new file
      wavFile = SD.open(nextFilename);
      if (!wavFile) {
        Serial.println("Failed to open file");
        continue;
      }

      // Read header safely
      int bytesRead = wavFile.read((uint8_t*)&wavHeader, sizeof(WAVHeader));
      if (bytesRead != sizeof(WAVHeader)) {
        Serial.println("Header read failed");
        wavFile.close();
        continue;
      }

      // Validate
      if (wavHeader.audioFormat != 1) {
        Serial.println("Unsupported WAV format (must be PCM)");
        wavFile.close();
        continue;
      }

      if (wavHeader.bitsPerSample != 16) {
        Serial.println("Only 16-bit WAV supported");
        wavFile.close();
        continue;
      }

      currentChannels = wavHeader.numChannels;
      wavActive = true;
    }

    for (int i = 0; i < DMA_BUF_LEN; i++) {
      int32_t sample1 = 0;  // 32-bit mix to prevent overflow
      int32_t sample2 = 0;  // Second sample value for stereo writes

      // Tone volume ramp
      currentToneVolume += (toneVolume - currentToneVolume) * toneVolumeStep;

      // Playback volume ramp
      currentPlaybackVolume += (playbackVolume - currentPlaybackVolume) * wavVolumeStep;

      // Tone generation
      if (toneActive) {
        int index = (int)phase;
        int16_t toneSample = sinTable[index];
        toneSample = (int16_t)(toneSample * currentToneVolume);
        sample1 += toneSample;
        sample2 += toneSample;

        phase += phase_step;
        if (phase >= SIN_TABLE_SIZE) phase -= SIN_TABLE_SIZE;

        toneSamplesRemaining--;
        if (toneSamplesRemaining == 0) toneActive = false;
      }

      // WAV playback
      if (wavActive) {
        // Refill buffer if empty
        if (wavBytesRemaining == 0) {
          int bytesToRead = min((int)wavFile.available(), (int)sizeof(wavBuffer));
          int bytesRead = wavFile.read((uint8_t*)wavBuffer, bytesToRead);
          wavBytesRemaining = bytesRead / sizeof(int16_t);
          wavReadIndex = 0;
        }

        // buffer refilled or non-empty: write next sample to buffer
        if (wavBytesRemaining > 0) {
          int16_t wavSample = wavBuffer[wavReadIndex++];
          wavSample = (int16_t) (wavSample * currentPlaybackVolume);
          sample1 += wavSample;
          sample2 += wavSample;
          wavBytesRemaining--;

          // If stereo, read another sample
          if (currentChannels > 1) {
            wavSample = wavBuffer[wavReadIndex++];
            wavSample = (int16_t) (wavSample * currentPlaybackVolume);
            sample2 = wavSample;
            wavBytesRemaining--;
          }
        } else {    // buffer still empty: end of file
          wavActive = false;
          wavFile.close();
        }
      }

      // Clamp to 16-bit
      if (sample1 > 32767) sample1 = 32767;
      if (sample1 < -32768) sample1 = -32768;

      if (sample2 > 32767) sample2 = 32767;
      if (sample2 < -32768) sample2 = -32768;

      int16_t finalSample1 = (int16_t) sample1;
      int16_t finalSample2 = (int16_t) sample2;

      // Stereo output
      buffer[2 * i] = finalSample1; // Left
      buffer[2 * i + 1] = finalSample2; // Right
    }

    // Protected by mutex to ensure channel exists
    if (xSemaphoreTake(i2sMutex, portMAX_DELAY)) {
      if (tx_chan != NULL) {
        i2s_channel_write(tx_chan, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
      }
      xSemaphoreGive(i2sMutex);
    }
  }
}