#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>

#include <libopencm3-plus/newlib/syscall.h>
#include <libopencm3-plus/newlib/devices/cdcacm.h>

#include <stdio.h>

int main(void) {
    rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);

    devoptab_list[0] = &dotab_cdcacm;
    devoptab_list[1] = &dotab_cdcacm;
    devoptab_list[2] = &dotab_cdcacm;
    cdcacm_f429_init();

    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    rcc_periph_clock_enable(RCC_SPI5);
    rcc_periph_clock_enable(RCC_GPIOF);
    gpio_mode_setup(GPIOF, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO7 | GPIO8 | GPIO9);
    gpio_set_af(GPIOF, GPIO_AF5, GPIO7 | GPIO8 | GPIO9);
    rcc_periph_clock_enable(RCC_GPIOC);
    gpio_set(GPIOC, GPIO1);
    gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO1);
    spi_set_master_mode(SPI5);
    spi_set_baudrate_prescaler(SPI5, SPI_CR1_BR_FPCLK_DIV_16);
    spi_enable_software_slave_management(SPI5);
    spi_set_nss_high(SPI5);
    spi_set_dff_8bit(SPI5);
    spi_set_clock_polarity_1(SPI5);
    spi_set_clock_phase_1(SPI5);
    spi_enable(SPI5);

    uint8_t tmp;

    tmp = ~(1 << 7) & (0x20 & 0x3F);
    gpio_clear(GPIOC, GPIO1);
    spi_send(SPI5, tmp); spi_read(SPI5);
    spi_send(SPI5, (1<<3)|(1<<0)|(1<<1)|(1<<2)|(3<<4)); spi_read(SPI5);
    gpio_set(GPIOC, GPIO1);

    tmp = ~(1 << 7) & (0x23 & 0x3F);
    gpio_clear(GPIOC, GPIO1);
    spi_send(SPI5, tmp); spi_read(SPI5);
    spi_send(SPI5, (1<<4)); spi_read(SPI5);
    gpio_set(GPIOC, GPIO1);

    while (1) {
        while (1) {
            tmp = (1 << 7) | (0x27 & 0x3F);
            gpio_clear(GPIOC, GPIO1);
            spi_send(SPI5, tmp); spi_read(SPI5);
            spi_send(SPI5, 0x00); tmp = spi_read(SPI5);
            gpio_set(GPIOC, GPIO1);
            if (tmp & (1 << 3)) break;
        }

        uint8_t buf[6];
        uint8_t cmd = (1 << 7) | (1 << 6) | (0x28 & 0x3F);
        gpio_clear(GPIOC, GPIO1);
        spi_send(SPI5, cmd); spi_read(SPI5);
        for (int i = 0; i < 6; i++) {
            spi_send(SPI5, 0x00);
            buf[i] = spi_read(SPI5);
        }
        gpio_set(GPIOC, GPIO1);

        int16_t gx = (int16_t)((buf[1] << 8) | buf[0]);
        int16_t gy = (int16_t)((buf[3] << 8) | buf[2]);
        int16_t gz = (int16_t)((buf[5] << 8) | buf[4]);

        printf("PASO 3: %d %d %d\r\n", gx, gy, gz);
        for (volatile int i = 0; i < 1000000; i++);
    }
}
