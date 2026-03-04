#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_system.h"

#define SAMPLE_RATE     44100
#define TONE_FREQ       440
#define DMA_BUF_LEN     512

// MicroSD breakout pins
// MOSI: GPIO 19
// MISO: GPIO 20
// SCK: GPIO 21
#define CS GPIO_NUM_22

// MAX98357A pins
#define SD = GPIO_NUM_18
#define DOUT GPIO_NUM_10
#define BCLK GPIO_NUM_11
#define LRC GPIO_NUM_23

static i2s_chan_handle_t tx_chan;

void setup() {
  Serial.begin(115200);
}

void loop() {
  // Create I2S channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_chan, NULL);

    // Standard I2S configuration
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BCLK,
            .ws   = LRC,
            .dout = DOUT,
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

    int16_t buffer[DMA_BUF_LEN];
    float phase = 0.0;
    float phase_step = 2.0 * M_PI * TONE_FREQ / SAMPLE_RATE;

    while (1) {
        for (int i = 0; i < DMA_BUF_LEN; i++) {
            buffer[i] = (int16_t)(sin(phase) * 30000);
            phase += phase_step;
            if (phase >= 2.0 * M_PI) {
                phase -= 2.0 * M_PI;
            }
        }

        size_t bytes_written;
        i2s_channel_write(tx_chan, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
    }
}
