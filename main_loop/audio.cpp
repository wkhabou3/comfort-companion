/*
Implementation of all audio functionality, including:
- sine table initialization
- I2S channel creation
- Tone generation
- Stored audio playback
*/

#include "audio.h"

static i2s_chan_handle_t tx_chan;

// Separate volume variables for pure sine tones and stored audio playback
// Tone volume should be between 0 and 1
float toneVolume = 0.5;
float playbackVolume = 1;

// Allocate memory for sine lookup table
int16_t sinTable[SIN_TABLE_SIZE];

void initSinTable() {
  for (int i = 0; i < SIN_TABLE_SIZE; i++) {
    // Pre-compute sin values (amplitude 2^15 - 1, max audio value)
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
  int16_t silence[256] = {0};
  size_t bytes_written;

  for (int i = 0; i < 10; i++) {
    i2s_channel_write(tx_chan, silence, sizeof(silence), &bytes_written, portMAX_DELAY);
  }

  // Enable amp
  delay(50);
  digitalWrite(AMP_SD, HIGH);
}

void playTone(float frequency, int duration) {
  // Not pretty, but we double the length of the buffer to account for stereo I2S channel
  // Otherwise, we would have to reconfigure the I2S channel which would be less ideal.
  int16_t buffer[DMA_BUF_LEN * 2];

  // Track phase and  calculate step (which index of sine table to access at each for loop iteration)
  float phase = 0.0;
  float phase_step = (frequency * SIN_TABLE_SIZE) / SAMPLE_RATE;

  // Calculate number of samples required to reach duration
  uint32_t totalSamples = (SAMPLE_RATE * duration) / 1000;
  uint32_t samplesGenerated = 0;

  size_t bytesWritten;

  while (samplesGenerated < totalSamples) {
    // Fill buffer
    for (int i = 0; i < DMA_BUF_LEN; i++) {
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

  // At this point, tone genersation has finished. Flush the buffer
  int16_t silence[DMA_BUF_LEN] = {0}; // stereo silence

  for (int i = 0; i < 8; i++) {
    i2s_channel_write(tx_chan, silence, sizeof(silence), &bytesWritten, portMAX_DELAY);
  }
}

void playWav(const char *filename) {
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

  int16_t buffer[DMA_BUF_LEN * 2];  // stereo
  size_t bytesWritten;

  // Continue while there are bytes to read from file
  while (file.available()) {
    // Shorten buffer if number of bytes available is less than buffer size
    int bytesToRead = min((int)file.available(), (int)sizeof(buffer));

    // Read from file and write to buffer
    int bytesRead = file.read((uint8_t*)buffer, bytesToRead);

    // Calculate number of samples
    int samples = bytesRead / sizeof(int16_t);

    // Re-iterate through buffer to scale by volume and sanitize values
    for (int i = 0; i < samples; i++) {
      buffer[i] = (int16_t)(buffer[i] * playbackVolume);
      // Prevent overflow (REDO THIS)
      if (buffer[i] > 32767) {
        buffer[i] = 32767;
      }
      if (buffer[i] < -32768) {
        buffer[i] = -32768;
      }
    }

    // Write to amp
    i2s_channel_write(tx_chan, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
  }

  // At this point, audio playback has finished. Flush the buffer
  int16_t silence[DMA_BUF_LEN * 2] = {0}; // stereo silence

  for (int i = 0; i < 8; i++) {
    i2s_channel_write(tx_chan, silence, sizeof(silence), &bytesWritten, portMAX_DELAY);
  }

  file.close();
}