#ifndef ILI9488DRIVER_H
#define ILI9488DRIVER_H

#include <Arduino.h>
#include <SPI.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include <esp_heap_caps.h>

#include "DisplayConfig.h"
#include "DisplayDriverBase.h"
#include "../../../Core/Core.h"

namespace pip3D
{

#ifndef TFT_MOSI
#define TFT_MOSI 7
#endif
#ifndef TFT_MISO
#define TFT_MISO -1
#endif
#ifndef TFT_SCLK
#define TFT_SCLK 17
#endif

    static constexpr uint8_t ILI9488_SWRESET = 0x01;
    static constexpr uint8_t ILI9488_SLPOUT = 0x11;
    static constexpr uint8_t ILI9488_DISPON = 0x29;
    static constexpr uint8_t ILI9488_CASET = 0x2A;
    static constexpr uint8_t ILI9488_PASET = 0x2B;
    static constexpr uint8_t ILI9488_RAMWR = 0x2C;
    static constexpr uint8_t ILI9488_COLMOD = 0x3A;
    static constexpr uint8_t ILI9488_MADCTL = 0x36;
    static constexpr uint8_t ILI9488_INVON = 0x21;

    static constexpr uint8_t ILI9488_MADCTL_MY = 0x80;
    static constexpr uint8_t ILI9488_MADCTL_MX = 0x40;
    static constexpr uint8_t ILI9488_MADCTL_MV = 0x20;
    static constexpr uint8_t ILI9488_MADCTL_BGR = 0x08;

    static constexpr size_t ILI9488_DMA_CHUNK_BYTES = 4092;
    static constexpr size_t ILI9488_DMA_CHUNK_PIXELS = ILI9488_DMA_CHUNK_BYTES / 3;

    class alignas(16) ILI9488Driver : public DisplayDriverBase
    {
    private:
        spi_device_handle_t spi_device;

        int8_t cs_pin, dc_pin, rst_pin, bl_pin;
        uint32_t cs_mask, dc_mask;
        volatile uint32_t *cs_reg_set;
        volatile uint32_t *cs_reg_clr;
        volatile uint32_t *dc_reg_set;
        volatile uint32_t *dc_reg_clr;

        uint16_t width, height;
        uint8_t rotation;

        alignas(16) uint8_t *swapBuffer;
        size_t swapBufferBytes;

        __attribute__((always_inline)) inline void CS_LOW()
        {
            if (likely(cs_pin >= 0))
                *cs_reg_clr = cs_mask;
        }

        __attribute__((always_inline)) inline void CS_HIGH()
        {
            if (likely(cs_pin >= 0))
                *cs_reg_set = cs_mask;
        }

        __attribute__((always_inline)) inline void DC_LOW()
        {
            *dc_reg_clr = dc_mask;
        }

        __attribute__((always_inline)) inline void DC_HIGH()
        {
            *dc_reg_set = dc_mask;
        }

        __attribute__((always_inline)) inline void sendCmdData(uint8_t cmd, const uint8_t *data, size_t len)
        {
            DC_LOW();
            CS_LOW();

            spi_transaction_t trans = {};
            trans.length = 8;
            trans.tx_data[0] = cmd;
            trans.flags = SPI_TRANS_USE_TXDATA;
            spi_device_polling_transmit(spi_device, &trans);

            if (data && len > 0)
            {
                DC_HIGH();
                if (len <= 4)
                {
                    trans.length = len * 8;
                    memcpy(trans.tx_data, data, len);
                    trans.flags = SPI_TRANS_USE_TXDATA;
                }
                else
                {
                    trans.length = len * 8;
                    trans.tx_buffer = data;
                    trans.flags = 0;
                }
                spi_device_polling_transmit(spi_device, &trans);
            }

            CS_HIGH();
        }

        __attribute__((always_inline)) inline void sendCommand(uint8_t cmd)
        {
            sendCmdData(cmd, nullptr, 0);
        }

        static __attribute__((always_inline)) inline void convert565To666(const uint16_t *src, uint8_t *dst, size_t count)
        {
            size_t i = 0;
            size_t n = count & ~static_cast<size_t>(1); // process 2 pixels per iteration

            for (; i < n; i += 2)
            {
                uint16_t c0 = src[i];
                uint16_t c1 = src[i + 1];

                uint8_t r0 = (uint8_t)((c0 >> 8) & 0xF8);
                uint8_t g0 = (uint8_t)((c0 >> 3) & 0xFC);
                uint8_t b0 = (uint8_t)((c0 << 3) & 0xF8);

                uint8_t r1 = (uint8_t)((c1 >> 8) & 0xF8);
                uint8_t g1 = (uint8_t)((c1 >> 3) & 0xFC);
                uint8_t b1 = (uint8_t)((c1 << 3) & 0xF8);

                *dst++ = r0;
                *dst++ = g0;
                *dst++ = b0;

                *dst++ = r1;
                *dst++ = g1;
                *dst++ = b1;
            }

            // Tail pixel if count is odd
            if (i < count)
            {
                uint16_t c = src[i];
                uint8_t r = (uint8_t)((c >> 8) & 0xF8);
                uint8_t g = (uint8_t)((c >> 3) & 0xFC);
                uint8_t b = (uint8_t)((c << 3) & 0xF8);
                *dst++ = r;
                *dst++ = g;
                *dst++ = b;
            }
        }

    public:
        ILI9488Driver() : spi_device(nullptr), cs_pin(-1), dc_pin(-1), rst_pin(-1), bl_pin(-1),
                          cs_mask(0), dc_mask(0), width(320), height(480), rotation(0),
                          swapBuffer(nullptr), swapBufferBytes(0) {}

        ~ILI9488Driver()
        {
            if (swapBuffer)
            {
                heap_caps_free(swapBuffer);
                swapBuffer = nullptr;
                swapBufferBytes = 0;
            }
            if (spi_device)
            {
                spi_bus_remove_device(spi_device);
                spi_bus_free(SPI2_HOST);
                spi_device = nullptr;
            }
        }

        bool init(const LCD &config = LCD()) override
        {
            if (swapBuffer)
            {
                heap_caps_free(swapBuffer);
                swapBuffer = nullptr;
                swapBufferBytes = 0;
            }

            if (spi_device)
            {
                spi_bus_remove_device(spi_device);
                spi_bus_free(SPI2_HOST);
                spi_device = nullptr;
            }

            width = config.w;
            height = config.h;
            cs_pin = config.cs;
            dc_pin = config.dc;
            rst_pin = config.rst;
            bl_pin = config.bl;

            if (cs_pin >= 0)
            {
                cs_mask = (1ULL << (cs_pin & 31));
                cs_reg_set = (cs_pin < 32) ? &GPIO.out_w1ts : &GPIO.out1_w1ts.val;
                cs_reg_clr = (cs_pin < 32) ? &GPIO.out_w1tc : &GPIO.out1_w1tc.val;
            }
            dc_mask = (1ULL << (dc_pin & 31));
            dc_reg_set = (dc_pin < 32) ? &GPIO.out_w1ts : &GPIO.out1_w1ts.val;
            dc_reg_clr = (dc_pin < 32) ? &GPIO.out_w1tc : &GPIO.out1_w1tc.val;

            if (cs_pin >= 0)
            {
                gpio_set_direction((gpio_num_t)cs_pin, GPIO_MODE_OUTPUT);
                CS_HIGH();
            }

            gpio_set_direction((gpio_num_t)dc_pin, GPIO_MODE_OUTPUT);
            DC_HIGH();

            if (rst_pin >= 0)
            {
                gpio_set_direction((gpio_num_t)rst_pin, GPIO_MODE_OUTPUT);
                gpio_set_level((gpio_num_t)rst_pin, 1);
            }

            if (bl_pin >= 0)
            {
                gpio_set_direction((gpio_num_t)bl_pin, GPIO_MODE_OUTPUT);
                gpio_set_level((gpio_num_t)bl_pin, 1);
            }

            spi_bus_config_t buscfg = {};
            buscfg.mosi_io_num = TFT_MOSI;
            buscfg.miso_io_num = TFT_MISO;
            buscfg.sclk_io_num = TFT_SCLK;
            buscfg.quadwp_io_num = -1;
            buscfg.quadhd_io_num = -1;
            buscfg.max_transfer_sz = width * height * 3 + 8;

            esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "SPI bus init failed (ILI9488): err=%d", (int)ret);
                return false;
            }

            spi_device_interface_config_t devcfg = {};
            devcfg.clock_speed_hz = config.freq;
            devcfg.mode = 0;
            devcfg.spics_io_num = -1;
            devcfg.queue_size = 7;
            devcfg.pre_cb = nullptr;
            devcfg.flags = SPI_DEVICE_NO_DUMMY | SPI_DEVICE_HALFDUPLEX;

            ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_device);
            if (ret != ESP_OK)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "SPI device add failed (ILI9488): err=%d", (int)ret);
                return false;
            }

            if (rst_pin >= 0)
            {
                gpio_set_level((gpio_num_t)rst_pin, 0);
                delay(10);
                gpio_set_level((gpio_num_t)rst_pin, 1);
                delay(120);
            }

            sendCommand(ILI9488_SWRESET);
            delay(150);

            // Use panel factory defaults for gamma/power/frame rate.

            uint8_t colmod = 0x66; // 18-bit for SPI
            sendCmdData(ILI9488_COLMOD, &colmod, 1);

            sendCommand(ILI9488_SLPOUT);
            delay(120);

            sendCommand(ILI9488_INVON);

            sendCommand(ILI9488_DISPON);
            delay(25);

            // Final orientation: landscape 480x320, rotated so that
            // the rendered scene is not upside-down on the physical panel.
            rotation = 3;
            uint8_t madctl = ILI9488_MADCTL_MX | ILI9488_MADCTL_MY | ILI9488_MADCTL_MV | ILI9488_MADCTL_BGR;
            sendCmdData(ILI9488_MADCTL, &madctl, 1);
            width = config.w;
            height = config.h;

            LOGI(::pip3D::Debug::LOG_MODULE_RENDER,
                 "ILI9488Driver init OK: %dx%d @ %dMHz",
                 width,
                 height,
                 (int)(config.freq / 1000000));

            return true;
        }

        void setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
        {
            uint8_t data[4];

            data[0] = x0 >> 8;
            data[1] = x0 & 0xFF;
            data[2] = x1 >> 8;
            data[3] = x1 & 0xFF;
            sendCmdData(ILI9488_CASET, data, 4);

            data[0] = y0 >> 8;
            data[1] = y0 & 0xFF;
            data[2] = y1 >> 8;
            data[3] = y1 & 0xFF;
            sendCmdData(ILI9488_PASET, data, 4);

            sendCommand(ILI9488_RAMWR);
        }

        __attribute__((hot)) void pushImage(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *buffer) override
        {
            if (!buffer || w <= 0 || h <= 0)
                return;

            if (x >= width || y >= height || x + w <= 0 || y + h <= 0)
                return;

            int16_t x_start = (x < 0) ? 0 : x;
            int16_t y_start = (y < 0) ? 0 : y;
            int16_t x_end = (x + w > width) ? width : (x + w);
            int16_t y_end = (y + h > height) ? height : (y + h);

            w = x_end - x_start;
            h = y_end - y_start;

            if (w <= 0 || h <= 0)
                return;

            if (x_start == 0 && w == width)
            {
                setAddrWindow(0, y_start, width - 1, y_end - 1);

                DC_HIGH();
                CS_LOW();

                const size_t totalPixels = (size_t)w * (size_t)h;
                const size_t chunkPixels = ILI9488_DMA_CHUNK_PIXELS;
                const size_t chunkBytes = chunkPixels * 3;
                const size_t requiredBytes = chunkBytes * 2;

                if (!swapBuffer || swapBufferBytes < requiredBytes)
                {
                    if (swapBuffer)
                        heap_caps_free(swapBuffer);

                    size_t bytes = requiredBytes;
                    swapBuffer = (uint8_t *)heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
                    if (!swapBuffer)
                    {
                        LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                             "ILI9488Driver pushImage: DMA buffer alloc failed (bytes=%u)",
                             (unsigned int)bytes);
                        CS_HIGH();
                        return;
                    }
                    swapBufferBytes = bytes;
                }

                uint8_t *dmaBuf[2];
                dmaBuf[0] = swapBuffer;
                dmaBuf[1] = swapBuffer + chunkBytes;

                spi_transaction_t trans[2];
                memset(trans, 0, sizeof(trans));

                bool inFlight[2] = {false, false};
                int queued = 0;

                size_t offsetPixels = 0;

                while (offsetPixels < totalPixels)
                {
                    int bufIndex;
                    if (!inFlight[0])
                        bufIndex = 0;
                    else if (!inFlight[1])
                        bufIndex = 1;
                    else
                    {
                        spi_transaction_t *retTrans = nullptr;
                        esp_err_t ret = spi_device_get_trans_result(spi_device, &retTrans, portMAX_DELAY);
                        if (ret == ESP_OK)
                        {
                            if (retTrans == &trans[0])
                                inFlight[0] = false;
                            else if (retTrans == &trans[1])
                                inFlight[1] = false;
                            queued--;
                        }
                        continue;
                    }

                    size_t remaining = totalPixels - offsetPixels;
                    size_t thisPixels = (remaining > chunkPixels) ? chunkPixels : remaining;

                    convert565To666(buffer + offsetPixels, dmaBuf[bufIndex], thisPixels);

                    spi_transaction_t &t = trans[bufIndex];
                    t.length = thisPixels * 24;
                    t.tx_buffer = dmaBuf[bufIndex];
                    t.flags = 0;

                    esp_err_t qret = spi_device_queue_trans(spi_device, &t, portMAX_DELAY);
                    if (qret != ESP_OK)
                    {
                        LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                             "ILI9488Driver pushImage: queue_trans failed (err=%d)",
                             (int)qret);
                        break;
                    }

                    inFlight[bufIndex] = true;
                    queued++;
                    offsetPixels += thisPixels;
                }

                while (queued > 0)
                {
                    spi_transaction_t *retTrans = nullptr;
                    esp_err_t ret = spi_device_get_trans_result(spi_device, &retTrans, portMAX_DELAY);
                    if (ret == ESP_OK)
                    {
                        if (retTrans == &trans[0])
                            inFlight[0] = false;
                        else if (retTrans == &trans[1])
                            inFlight[1] = false;
                        queued--;
                    }
                }

                CS_HIGH();
                return;
            }

            setAddrWindow(x_start, y_start, x_end - 1, y_end - 1);

            DC_HIGH();
            CS_LOW();

            const size_t rowPixels = (size_t)w;
            const size_t rowBytes = rowPixels * 3;

            if (!swapBuffer || swapBufferBytes < rowBytes)
            {
                if (swapBuffer)
                    heap_caps_free(swapBuffer);
                swapBuffer = (uint8_t *)heap_caps_malloc(rowBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
                if (!swapBuffer)
                {
                    CS_HIGH();
                    return;
                }
                swapBufferBytes = rowBytes;
            }

            for (int16_t row = 0; row < h; row++)
            {
                uint16_t *rowPtr = buffer + (size_t)row * (size_t)w;

                convert565To666(rowPtr, swapBuffer, rowPixels);

                spi_transaction_t trans = {};
                trans.length = rowPixels * 24;
                trans.tx_buffer = swapBuffer;
                trans.flags = 0;
                spi_device_polling_transmit(spi_device, &trans);
            }

            CS_HIGH();
        }

        __attribute__((always_inline)) inline uint16_t getWidth() const override { return width; }
        __attribute__((always_inline)) inline uint16_t getHeight() const override { return height; }
    };

}

#endif
