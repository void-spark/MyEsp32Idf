#include "i2sstuff.h"

void i2s_setup(i2s_port_t port, int bck, int lck, int din, int buf_cnt, int buf_len, i2s_bits_per_sample_t bits, int sampleRate) {
    i2s_config_t i2s_config {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate = sampleRate;
    i2s_config.bits_per_sample = bits;
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
    i2s_config.dma_buf_count = buf_cnt;
    i2s_config.dma_buf_len = buf_len;
    i2s_config.use_apll = false,
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;

    i2s_pin_config_t pin_config {};
    pin_config.bck_io_num = bck;
    pin_config.ws_io_num = lck;
    pin_config.data_out_num = din;
    pin_config.data_in_num = -1;

    i2s_driver_install(port, &i2s_config, 0, NULL);
    i2s_set_pin(port, &pin_config);
}
