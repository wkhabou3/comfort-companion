/*
Implementation of all audio functionality, including:
- sine table initialization
- I2S channel creation
- Tone generation
- Stored audio playback
*/

#include "audio.h"

static i2s_chan_handle_t tx_chan;

int16_t buffer[DMA_BUF_LEN];

// Separate volume variables for pure sine tones and stored audio playback
// Tone volume should be between 0 and 1
float toneVolume = 0.1;
float playbackVolume = 0.5;

// Shared control variables
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

// Temporary buffer for WAV read
int16_t wavBuffer[DMA_BUF_LEN];

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
      toneVolume = 0.1;
      playbackVolume = 0.5;
      break;
    case VolumeLevel::MODERATE:
      toneVolume = 0.2;
      playbackVolume = 1;
      break;
    default:  // loud
      toneVolume = 0.4;
      playbackVolume = 2;
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
  toneActive = true;
  wavActive = false;
  currentFrequency = frequency;
  phase_step = SIN_TABLE_SIZE * frequency / SAMPLE_RATE; 
  toneSamplesRemaining = SAMPLE_RATE * duration / 1000;
}

void playWav(const char *filename) {
  wavFile = SD.open(filename);
  if (!wavFile) {
    Serial.println("Failed to open file");
    return;
  }

  toneActive = false;
  wavActive = true;
  // Read WAV header
  wavFile.read((uint8_t*)&wavHeader, sizeof(WAVHeader));

  if (wavHeader.audioFormat != 1) {
    Serial.println("Unsupported WAV format (must be PCM)");
    return;
  }

  if (wavHeader.bitsPerSample != 16) {
    Serial.println("Only 16-bit WAV supported");
    return;
  }
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

void audioTask(void *param) {
  size_t bytesWritten;

  while (true) {
    for (int i = 0; i < DMA_BUF_LEN; i++) {
      int32_t sample = 0; // 32-bit to avoid overflow during mixing

      // --- Tone generation ---
      if (toneActive) {
        int index = (int)phase;
        int16_t toneSample = sinTable[index];

        // Apply tone volume
        toneSample = (int16_t)(toneSample * toneVolume);
        sample += toneSample;

        // Advance phase
        phase += phase_step;
        if (phase >= SIN_TABLE_SIZE) phase -= SIN_TABLE_SIZE;

        // Decrement remaining sample count; stop playback if none remaining
        toneSamplesRemaining--;
        if (toneSamplesRemaining == 0) toneActive = false;
      }

      // --- WAV playback ---
      if (wavActive) {
        // Refill buffer if empty
        if (i % DMA_BUF_LEN == 0) {
          int bytesToRead = min((int)wavFile.available(), (int)sizeof(wavBuffer));
          int bytesRead = wavFile.read((uint8_t*)wavBuffer, bytesToRead);
          wavBytesRemaining = bytesRead / sizeof(int16_t);
          wavReadIndex = 0;
        }

        // Iterate through buffer if not empty
        if (wavBytesRemaining > 0) {
          int16_t wavSample = wavBuffer[wavReadIndex++];
          wavSample = (int16_t)(wavSample * playbackVolume); // scale by volume
          sample += wavSample;
          wavBytesRemaining--;
        } else {
          // End of file
          wavActive = false;
          wavFile.close();
        }
      }

      // Clamp final mixed sample to 16-bit
      if (sample > 32767) sample = 32767;
      if (sample < -32768) sample = -32768;

      int16_t finalSample = (int16_t)sample;

      // Write mono
      buffer[i] = finalSample;
    }

    // Always write to I2S (silence if nothing is playing)
    i2s_channel_write(tx_chan, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
  }
}