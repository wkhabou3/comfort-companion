/*
Implementation of all audio functionality, including:
- sine table initialization
- I2S channel creation
- Tone generation
- Stored audio playback
*/

#include "audio.h"

static i2s_chan_handle_t tx_chan;

int16_t buffer[DMA_BUF_LEN * 2];

// Separate volume variables for pure sine tones and stored audio playback
// Tone volume should be between 0 and 1
float toneVolume = 0.1;
float playbackVolume = 0.5;

// set flag whenever audio playback is interrupted
volatile bool audioAbort = false;

// Allocate memory for sine lookup table
int16_t sinTable[SIN_TABLE_SIZE];

// Button interrupt
void IRAM_ATTR stopAudioISR() {
  audioAbort = true;
}

void setAudioAbort(bool val) {
  audioAbort = val;
}

void setVolume(int volume) {
  switch (volume) {
    case 0: // quiet
      toneVolume = 0.04;
      playbackVolume = 0.2;
      break;
    case 1: // moderate
      toneVolume = 0.1;
      playbackVolume = 0.5;
      break;
    default:  // loud
      toneVolume = 0.2;
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
  // Initialize SD pin, set low to eliminate noise on startup
  pinMode(AMP_SD, OUTPUT);
  digitalWrite(AMP_SD, LOW);

  // initialize I2S channel
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&chan_cfg, &tx_chan, NULL);

  // Configure channel
  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_16BIT,
      numChannels == 2 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO
    ),
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

  // Send silence to stabilize audio stream
  flushAudio();

  // Enable amp
  delay(50);
  digitalWrite(AMP_SD, HIGH);
}

void playTone(float frequency, int duration) {
  // reset abort flag before continuing
  audioAbort = false;

  // Track phase and  calculate step (which index of sine table to access at each for loop iteration)
  float phase = 0.0;
  float phase_step = (frequency * SIN_TABLE_SIZE) / SAMPLE_RATE;

  // Calculate number of samples required to reach duration
  uint32_t totalSamples = (SAMPLE_RATE * duration) / 1000;
  uint32_t samplesGenerated = 0;

  size_t bytesWritten;

  while (samplesGenerated < totalSamples && !audioAbort) {
    // Fill buffer
    for (int i = 0; i < DMA_BUF_LEN; i++) {
      // Check abort flag
      if (audioAbort) {
        break;
      }
      // Cast current phase to use as index for sine table
      int index = (int) phase;
      int16_t sample = sinTable[index];

      // Scale sample by volume and write to buffer
      int16_t bufValue = (int16_t) (sample * toneVolume);
      // Double write to buffer (accounting for stereo)
      buffer[2 * i] = bufValue;
      buffer[2 * i + 1] = bufValue;

      // Increment phase and reset at full oscillation
      phase += phase_step;
      if (phase >= SIN_TABLE_SIZE) {
        phase -= SIN_TABLE_SIZE;
      }
    }

    // Write buffer values to amp
    i2s_channel_write(tx_chan, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);

    // Increment number of samples generated
    samplesGenerated += DMA_BUF_LEN;
  }

  // At this point, tone generation has finished. Flush the buffer
  flushAudio();
}

void playWav(const char *filename) {
  // reset abort flag before continuing
  audioAbort = false;

  // Attempt to open file. Return if failure
  File file = SD.open(filename);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }

  // Read WAV header
  WAVHeader header;
  file.read((uint8_t*)&header, sizeof(WAVHeader));

  if (header.audioFormat != 1) {
    Serial.println("Unsupported WAV format (must be PCM)");
    return;
  }

  if (header.bitsPerSample != 16) {
    Serial.println("Only 16-bit WAV supported");
    return;
  }
  
  size_t bytesWritten;

  // Continue while there are bytes to read from file. also check abort flag
  while (file.available()) {
    if (audioAbort) {
      break;
    }

    // Shorten buffer if number of bytes available is less than buffer size
    int bytesToRead = min((int) file.available(), (int) sizeof(buffer));

    // Read from file and write to buffer
    int bytesRead = file.read((uint8_t*)buffer, bytesToRead);

    // Calculate number of samples
    int samples = bytesRead / sizeof(int16_t);

    // Re-iterate through buffer to scale by volume and sanitize values
    for (int i = 0; i < samples; i++) {
      int32_t sample = buffer[i] * playbackVolume;
      // Prevent overflow
      if (sample > 32767) {
        sample = 32767;
      }
      if (sample < -32768) {
        sample = -32768;
      }
      buffer[i] = (int16_t) sample;
    }

    // Write to amp
    i2s_channel_write(tx_chan, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
  }

  // Stop playback and close file
  flushAudio();
  file.close();
}

void flushAudio()
{
  // Overwrite buffer with zeros
  int16_t silence[DMA_BUF_LEN * 2] = {0};
  size_t bytesWritten;

  for (int i = 0; i < 8; i++) {
    i2s_channel_write(tx_chan, silence, sizeof(silence), &bytesWritten, portMAX_DELAY);
  }
}