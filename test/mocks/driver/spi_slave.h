// Firmas tomadas de ESP-IDF v5.5 (components/esp_driver_spi/include/driver/spi_slave.h)
#pragma once
#include <stddef.h>
#include <deque>
#include <vector>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
typedef enum { SPI1_HOST = 0, SPI2_HOST = 1, SPI3_HOST = 2 } spi_host_device_t;
typedef enum { SPI_DMA_DISABLED = 0, SPI_DMA_CH1 = 1, SPI_DMA_CH2 = 2, SPI_DMA_CH_AUTO = 3 } spi_dma_chan_t;
typedef struct { int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num, max_transfer_sz; uint32_t flags; int intr_flags; } spi_bus_config_t;
typedef struct { int spics_io_num; uint32_t flags; int queue_size; uint8_t mode; void *post_setup_cb; void *post_trans_cb; } spi_slave_interface_config_t;
typedef struct { size_t length; size_t trans_len; const void *tx_buffer; void *rx_buffer; void *user; } spi_slave_transaction_t;
namespace mock { inline int spi_host = -1, spi_dma = -1; inline spi_slave_interface_config_t spi_cfg; }
inline esp_err_t spi_slave_initialize(spi_host_device_t h, const spi_bus_config_t *, const spi_slave_interface_config_t *c, spi_dma_chan_t d) { mock::spi_host = h; mock::spi_dma = d; mock::spi_cfg = *c; return ESP_OK; }
#define ESP_ERROR_CHECK(x) do { if ((x) != ESP_OK) { std::printf("ESP_ERROR_CHECK falló\n"); } } while (0)
inline esp_err_t spi_slave_transmit(spi_host_device_t, spi_slave_transaction_t *, TickType_t) { return -1; }
