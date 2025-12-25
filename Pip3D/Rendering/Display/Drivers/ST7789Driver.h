#ifndef ST7789DRIVER_H
#define ST7789DRIVER_H

#include <Arduino.h>
#include <SPI.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include <esp_heap_caps.h>
#include "../../../Core/Core.h"
#include "DisplayConfig.h"

namespace pip3D
{

    static constexpr uint8_t ST7789_SWRESET = 0x01;
    static constexpr uint8_t ST7789_SLPOUT = 0x11;
    static constexpr uint8_t ST7789_NORON = 0x13;
    static constexpr uint8_t ST7789_INVON = 0x21;
    static constexpr uint8_t ST7789_DISPON = 0x29;
    static constexpr uint8_t ST7789_CASET = 0x2A;
    static constexpr uint8_t ST7789_RASET = 0x2B;
    static constexpr uint8_t ST7789_RAMWR = 0x2C;
    static constexpr uint8_t ST7789_COLMOD = 0x3A;
    static constexpr uint8_t ST7789_MADCTL = 0x36;

    static constexpr uint8_t ST7789_MADCTL_MY = 0x80;
    static constexpr uint8_t ST7789_MADCTL_MX = 0x40;
    static constexpr uint8_t ST7789_MADCTL_MV = 0x20;
    static constexpr uint8_t ST7789_MADCTL_RGB = 0x00;

    static constexpr size_t DMA_CHUNK_SIZE = 4092;
    static constexpr size_t DMA_CHUNK_PIXELS = DMA_CHUNK_SIZE / 2;
    static constexpr size_t SMALL_BUFFER_THRESHOLD = 2046;

    class alignas(16) ST7789Driver
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

        alignas(16) uint16_t *dmaBuffer;
        size_t dmaBufferSize;

        alignas(16) uint16_t *swapBuffer;
        size_t swapBufferSize;

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

    public:
        ST7789Driver() : spi_device(nullptr), cs_pin(-1), dc_pin(-1), rst_pin(-1), bl_pin(-1),
                         cs_mask(0), dc_mask(0), width(240), height(320), rotation(0),
                         dmaBuffer(nullptr), dmaBufferSize(0), swapBuffer(nullptr), swapBufferSize(0) {}

        ~ST7789Driver()
        {
            if (dmaBuffer)
            {
                heap_caps_free(dmaBuffer);
                dmaBuffer = nullptr;
            }
            if (swapBuffer)
            {
                heap_caps_free(swapBuffer);
                swapBuffer = nullptr;
            }
            if (spi_device)
            {
                spi_bus_remove_device(spi_device);
                spi_bus_free(SPI2_HOST);
                spi_device = nullptr;
            }
        }

        bool init(const LCD &config = LCD())
        {
            if (dmaBuffer)
            {
                heap_caps_free(dmaBuffer);
                dmaBuffer = nullptr;
                dmaBufferSize = 0;
            }
            if (swapBuffer)
            {
                heap_caps_free(swapBuffer);
                swapBuffer = nullptr;
                swapBufferSize = 0;
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
            buscfg.mosi_io_num = 7;
            buscfg.miso_io_num = -1;
            buscfg.sclk_io_num = 17;
            buscfg.quadwp_io_num = -1;
            buscfg.quadhd_io_num = -1;
            buscfg.max_transfer_sz = 320 * 240 * 2 + 8;

            esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
            {
                LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                     "SPI bus init failed: err=%d", (int)ret);
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
                     "SPI device add failed: err=%d", (int)ret);
                return false;
            }

            if (rst_pin >= 0)
            {
                gpio_set_level((gpio_num_t)rst_pin, 0);
                delay(10);
                gpio_set_level((gpio_num_t)rst_pin, 1);
                delay(120);
            }

            sendCommand(ST7789_SWRESET);
            delay(150);

            sendCommand(ST7789_SLPOUT);
            delay(120);

            uint8_t data;
            data = ST7789_MADCTL_RGB;
            sendCmdData(ST7789_MADCTL, &data, 1);

            data = 0x55;
            sendCmdData(ST7789_COLMOD, &data, 1);

            sendCommand(ST7789_INVON);
            sendCommand(ST7789_NORON);
            delay(10);

            sendCommand(ST7789_DISPON);
            delay(120);

            setRotation(3);

            dmaBufferSize = width * 16;
            dmaBuffer = (uint16_t *)heap_caps_malloc(dmaBufferSize * sizeof(uint16_t), MALLOC_CAP_DMA);
            if (!dmaBuffer)
            {
                dmaBuffer = (uint16_t *)malloc(dmaBufferSize * sizeof(uint16_t));
                if (!dmaBuffer)
                {
                    LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                         "ST7789Driver DMA buffer alloc failed (size=%u)",
                         (unsigned int)(dmaBufferSize * sizeof(uint16_t)));
                    return false;
                }
            }

            LOGI(::pip3D::Debug::LOG_MODULE_RENDER,
                 "ST7789Driver init OK: %dx%d @ %dMHz",
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
            sendCmdData(ST7789_CASET, data, 4);

            data[0] = y0 >> 8;
            data[1] = y0 & 0xFF;
            data[2] = y1 >> 8;
            data[3] = y1 & 0xFF;
            sendCmdData(ST7789_RASET, data, 4);

            sendCommand(ST7789_RAMWR);
        }

        void pushColors(uint16_t *colors, uint16_t len)
        {
            if (!colors || len == 0)
                return;

            DC_HIGH();
            CS_LOW();

            size_t remaining = len;
            uint16_t *src = colors;
            const size_t maxChunk = SMALL_BUFFER_THRESHOLD;

            while (remaining > 0)
            {
                size_t chunk = (remaining > maxChunk) ? maxChunk : remaining;

                if (!swapBuffer || swapBufferSize < chunk)
                {
                    if (swapBuffer)
                        heap_caps_free(swapBuffer);
                    swapBuffer = (uint16_t *)heap_caps_malloc(chunk * sizeof(uint16_t), MALLOC_CAP_DMA);
                    if (!swapBuffer)
                    {
                        swapBuffer = (uint16_t *)malloc(chunk * sizeof(uint16_t));
                    }
                    if (!swapBuffer)
                    {
                        CS_HIGH();
                        return;
                    }
                    swapBufferSize = chunk;
                }

                uint32_t *src32 = (uint32_t *)src;
                uint32_t *dst32 = (uint32_t *)swapBuffer;
                size_t chunks32 = chunk / 2;

                for (size_t i = 0; i < chunks32; i++)
                {
                    uint32_t val = src32[i];
                    dst32[i] = ((val & 0x00FF00FF) << 8) | ((val & 0xFF00FF00) >> 8);
                }

                if (chunk & 1)
                {
                    uint16_t pixel = src[chunk - 1];
                    swapBuffer[chunk - 1] = (pixel >> 8) | (pixel << 8);
                }

                spi_transaction_t trans = {};
                trans.length = chunk * 16;
                trans.tx_buffer = swapBuffer;
                spi_device_polling_transmit(spi_device, &trans);

                src += chunk;
                remaining -= chunk;
            }

            CS_HIGH();
        }

        __attribute__((always_inline)) inline void drawPixelFast(int16_t x, int16_t y, uint16_t color)
        {
            setAddrWindow(x, y, x, y);
            DC_HIGH();
            CS_LOW();

            spi_transaction_t trans = {};
            trans.length = 16;
            trans.tx_data[0] = color >> 8;
            trans.tx_data[1] = color & 0xFF;
            trans.flags = SPI_TRANS_USE_TXDATA;
            spi_device_polling_transmit(spi_device, &trans);

            CS_HIGH();
        }

        void drawPixel(int16_t x, int16_t y, uint16_t color)
        {
            if (x < 0 || x >= width || y < 0 || y >= height)
                return;
            drawPixelFast(x, y, color);
        }

        void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
        {
            if (x >= width || y >= height)
                return;
            if (x + w <= 0 || y + h <= 0)
                return;

            if (x < 0)
            {
                w += x;
                x = 0;
            }
            if (y < 0)
            {
                h += y;
                y = 0;
            }
            if (x + w > width)
                w = width - x;
            if (y + h > height)
                h = height - y;

            setAddrWindow(x, y, x + w - 1, y + h - 1);

            DC_HIGH();
            CS_LOW();

            uint32_t totalPixels = (uint32_t)w * h;

            if (totalPixels > 64 && dmaBuffer)
            {
                for (size_t i = 0; i < dmaBufferSize && i < totalPixels; i++)
                {
                    dmaBuffer[i] = color;
                }

                while (totalPixels > 0)
                {
                    size_t chunk = (totalPixels > dmaBufferSize) ? dmaBufferSize : totalPixels;

                    spi_transaction_t trans = {};
                    trans.length = chunk * 16;
                    trans.tx_buffer = dmaBuffer;
                    spi_device_polling_transmit(spi_device, &trans);

                    totalPixels -= chunk;
                }
            }
            else
            {
                uint16_t swapped = (color >> 8) | (color << 8);

                if (totalPixels <= SMALL_BUFFER_THRESHOLD)
                {
                    if (!swapBuffer || swapBufferSize < totalPixels)
                    {
                        if (swapBuffer)
                            heap_caps_free(swapBuffer);
                        swapBuffer = (uint16_t *)heap_caps_malloc(totalPixels * sizeof(uint16_t), MALLOC_CAP_DMA);
                        if (!swapBuffer)
                        {
                            swapBuffer = (uint16_t *)malloc(totalPixels * sizeof(uint16_t));
                        }
                        if (!swapBuffer)
                        {
                            CS_HIGH();
                            return;
                        }
                        swapBufferSize = totalPixels;
                    }

                    uint32_t color32 = (swapped << 16) | swapped;
                    uint32_t *buf32 = (uint32_t *)swapBuffer;
                    uint32_t pixels32 = totalPixels / 2;

                    for (uint32_t i = 0; i < pixels32; i++)
                    {
                        buf32[i] = color32;
                    }
                    if (totalPixels & 1)
                    {
                        swapBuffer[totalPixels - 1] = swapped;
                    }

                    spi_transaction_t trans = {};
                    trans.length = totalPixels * 16;
                    trans.tx_buffer = swapBuffer;
                    spi_device_polling_transmit(spi_device, &trans);
                }
                else
                {
                    spi_transaction_t trans = {};
                    trans.length = 16;
                    trans.tx_data[0] = color >> 8;
                    trans.tx_data[1] = color & 0xFF;
                    trans.flags = SPI_TRANS_USE_TXDATA;

                    for (uint32_t i = 0; i < totalPixels; i++)
                    {
                        spi_device_polling_transmit(spi_device, &trans);
                    }
                }
            }

            CS_HIGH();
        }

        void fillScreen(uint16_t color)
        {
            fillRect(0, 0, width, height, color);
        }

        __attribute__((hot)) void pushImage(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *buffer)
        {
            if (x == 0 && y == 0 && w == width && h == height)
            {
                setAddrWindow(0, 0, width - 1, height - 1);

                DC_HIGH();
                CS_LOW();

                const size_t totalPixels = (size_t)width * height;

                const size_t halfBufferPixels = DMA_CHUNK_PIXELS;
                const size_t requiredPixels = halfBufferPixels * 2;

                if (!swapBuffer || swapBufferSize < requiredPixels)
                {
                    if (swapBuffer)
                        heap_caps_free(swapBuffer);

                    size_t bytes = requiredPixels * sizeof(uint16_t);
                    swapBuffer = (uint16_t *)heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_DMA);
                    if (!swapBuffer)
                    {
                        LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                             "ST7789Driver pushImage: DMA buffer alloc failed (bytes=%u)",
                             (unsigned int)bytes);
                        CS_HIGH();
                        return;
                    }
                    swapBufferSize = requiredPixels;
                }

                uint16_t *dmaBuf[2];
                dmaBuf[0] = swapBuffer;
                dmaBuf[1] = swapBuffer + halfBufferPixels;

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
                    size_t chunkPixels = (remaining > halfBufferPixels) ? halfBufferPixels : remaining;

                    uint32_t *src32 = (uint32_t *)(buffer + offsetPixels);
                    uint32_t *dst32 = (uint32_t *)dmaBuf[bufIndex];
                    size_t chunks32 = chunkPixels / 2;

                    for (size_t i = 0; i < chunks32; i++)
                    {
                        uint32_t val = src32[i];
                        dst32[i] = ((val & 0x00FF00FF) << 8) | ((val & 0xFF00FF00) >> 8);
                    }

                    if (chunkPixels & 1)
                    {
                        uint16_t pixel = buffer[offsetPixels + chunkPixels - 1];
                        dmaBuf[bufIndex][chunkPixels - 1] = (pixel >> 8) | (pixel << 8);
                    }

                    spi_transaction_t &t = trans[bufIndex];
                    t.length = chunkPixels * 16;
                    t.tx_buffer = dmaBuf[bufIndex];
                    t.flags = 0;

                    esp_err_t qret = spi_device_queue_trans(spi_device, &t, portMAX_DELAY);
                    if (qret != ESP_OK)
                    {
                        LOGE(::pip3D::Debug::LOG_MODULE_RENDER,
                             "ST7789Driver pushImage: queue_trans failed (err=%d)",
                             (int)qret);
                        break;
                    }

                    inFlight[bufIndex] = true;
                    queued++;
                    offsetPixels += chunkPixels;
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

            setAddrWindow(x_start, y_start, x_end - 1, y_end - 1);

            DC_HIGH();
            CS_LOW();

            for (int16_t row = 0; row < h; row++)
            {
                uint16_t *rowPtr = buffer + ((y_start + row) * width + x_start);

                size_t rowPixels = (size_t)w;

                if (!swapBuffer || swapBufferSize < rowPixels)
                {
                    if (swapBuffer)
                        heap_caps_free(swapBuffer);
                    swapBuffer = (uint16_t *)heap_caps_malloc(rowPixels * sizeof(uint16_t), MALLOC_CAP_DMA);
                    if (!swapBuffer)
                    {
                        swapBuffer = (uint16_t *)malloc(rowPixels * sizeof(uint16_t));
                    }
                    if (!swapBuffer)
                    {
                        CS_HIGH();
                        return;
                    }
                    swapBufferSize = rowPixels;
                }

                uint32_t *src32 = (uint32_t *)rowPtr;
                uint32_t *dst32 = (uint32_t *)swapBuffer;
                size_t chunks32 = rowPixels / 2;

                for (size_t i = 0; i < chunks32; i++)
                {
                    uint32_t val = src32[i];
                    dst32[i] = ((val & 0x00FF00FF) << 8) | ((val & 0xFF00FF00) >> 8);
                }

                if (rowPixels & 1)
                {
                    uint16_t pixel = rowPtr[rowPixels - 1];
                    swapBuffer[rowPixels - 1] = (pixel >> 8) | (pixel << 8);
                }

                spi_transaction_t trans = {};
                trans.length = w * 16;
                trans.tx_buffer = swapBuffer;
                trans.flags = 0;
                spi_device_polling_transmit(spi_device, &trans);
            }

            CS_HIGH();
        }

        __attribute__((always_inline)) inline void drawHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
        {
            if (y < 0 || y >= height)
                return;
            if (x < 0)
            {
                w += x;
                x = 0;
            }
            if (x + w > width)
                w = width - x;
            if (w <= 0)
                return;

            fillRect(x, y, w, 1, color);
        }

        __attribute__((always_inline)) inline void drawVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
        {
            if (x < 0 || x >= width)
                return;
            if (y < 0)
            {
                h += y;
                y = 0;
            }
            if (y + h > height)
                h = height - y;
            if (h <= 0)
                return;

            fillRect(x, y, 1, h, color);
        }

        void setRotation(uint8_t r)
        {
            rotation = r & 3;
            uint8_t madctl = 0;

            switch (rotation)
            {
            case 0:
                madctl = ST7789_MADCTL_RGB;
                width = 240;
                height = 320;
                break;
            case 1:
                madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MV | ST7789_MADCTL_RGB;
                width = 320;
                height = 240;
                break;
            case 2:
                madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MY | ST7789_MADCTL_RGB;
                width = 240;
                height = 320;
                break;
            case 3:
                madctl = ST7789_MADCTL_MY | ST7789_MADCTL_MV | ST7789_MADCTL_RGB;
                width = 320;
                height = 240;
                break;
            }

            sendCmdData(ST7789_MADCTL, &madctl, 1);
        }

        __attribute__((always_inline)) inline uint16_t getWidth() const { return width; }
        __attribute__((always_inline)) inline uint16_t getHeight() const { return height; }
    };

}

#endif
