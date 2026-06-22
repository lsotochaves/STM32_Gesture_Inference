#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>

#include <libopencm3-plus/newlib/syscall.h>
#include <libopencm3-plus/newlib/devices/cdcacm.h>

#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>

#include <libopencm3-plus/utils/misc.h>

// LED pins - STM32F429-DISCO
#define LED_GREEN_PIN    GPIO13
#define LED_GREEN_PORT   GPIOG
#define LED_RED_PIN      GPIO14
#define LED_RED_PORT     GPIOG

// SPI control
#define I3G4250D_RDWR        (1 << 7)
#define I3G4250D_CS          GPIOC, GPIO1

// Gyro registers
#define I3G4250D_WHO_AM_I    0x0F
#define I3G4250D_STATUS_REG  0x27
#define I3G4250D_CTRL_REG1   0x20
#define I3G4250D_CTRL_REG4   0x23

// CTRL_REG1 bits
#define I3G4250D_CTRL_REG1_XEN          (1 << 0)
#define I3G4250D_CTRL_REG1_YEN          (1 << 1)
#define I3G4250D_CTRL_REG1_ZEN          (1 << 2)
#define I3G4250D_CTRL_REG1_PD           (1 << 3)
#define I3G4250D_CTRL_REG1_BW_SHIFT     4

// CTRL_REG4 bits
#define I3G4250D_CTRL_REG4_FS_SHIFT     4

// Status register
#define I3G4250D_STATUS_ZYXDA   (1 << 3)

// Output registers
#define I3G4250D_OUT_X_L    0x28
#define I3G4250D_OUT_X_H    0x29
#define I3G4250D_OUT_Y_L    0x2A
#define I3G4250D_OUT_Y_H    0x2B
#define I3G4250D_OUT_Z_L    0x2C
#define I3G4250D_OUT_Z_H    0x2D


// LED functions

void leds_init(void) {
    rcc_periph_clock_enable(RCC_GPIOG);
    gpio_mode_setup(GPIOG, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13 | GPIO14);
    gpio_clear(GPIOG, GPIO13 | GPIO14);
}

void led_green_on(void)     { gpio_set(LED_GREEN_PORT, LED_GREEN_PIN); }
void led_green_off(void)    { gpio_clear(LED_GREEN_PORT, LED_GREEN_PIN); }
void led_green_toggle(void) { gpio_toggle(LED_GREEN_PORT, LED_GREEN_PIN); }

void led_red_on(void)       { gpio_set(LED_RED_PORT, LED_RED_PIN); }
void led_red_off(void)      { gpio_clear(LED_RED_PORT, LED_RED_PIN); }
void led_red_toggle(void)   { gpio_toggle(LED_RED_PORT, LED_RED_PIN); }


// SPI read/write

uint8_t read_reg(uint8_t reg) {
    uint8_t tmp;
    tmp = I3G4250D_RDWR | (reg & 0x3F);

    gpio_clear(I3G4250D_CS);
    spi_send(SPI5, tmp);
    spi_read(SPI5);

    spi_send(SPI5, 0x00);
    tmp = spi_read(SPI5);
    gpio_set(I3G4250D_CS);

    return tmp;
}

uint8_t write_reg(uint8_t reg, uint8_t data) {
    uint8_t tmp;
    tmp = ~I3G4250D_RDWR & (reg & 0x3F);

    gpio_clear(I3G4250D_CS);
    spi_send(SPI5, tmp);
    spi_read(SPI5);

    spi_send(SPI5, data);
    tmp = spi_read(SPI5);
    gpio_set(I3G4250D_CS);

    return tmp;
}

// Burst read: X, Y, Z in a single SPI transaction using auto-increment
void read_axes_burst(int16_t *gx, int16_t *gy, int16_t *gz) {
    uint8_t buf[6];
    uint8_t cmd = I3G4250D_RDWR | (1 << 6) | (I3G4250D_OUT_X_L & 0x3F);

    gpio_clear(I3G4250D_CS);
    spi_send(SPI5, cmd);
    spi_read(SPI5);

    for (int i = 0; i < 6; i++) {
        spi_send(SPI5, 0x00);
        buf[i] = spi_read(SPI5);
    }
    gpio_set(I3G4250D_CS);

    *gx = (int16_t)((buf[1] << 8) | buf[0]);
    *gy = (int16_t)((buf[3] << 8) | buf[2]);
    *gz = (int16_t)((buf[5] << 8) | buf[4]);
}


// Peripheral init

void spi_init(void) {
    rcc_periph_clock_enable(RCC_SPI5);
    rcc_periph_clock_enable(RCC_GPIOF);

    gpio_mode_setup(GPIOF, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO7 | GPIO8 | GPIO9);
    gpio_set_af(GPIOF, GPIO_AF5, GPIO7 | GPIO8 | GPIO9);

    rcc_periph_clock_enable(RCC_GPIOC);
    gpio_set(I3G4250D_CS);
    gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO1);

    spi_set_master_mode(SPI5);
    spi_set_baudrate_prescaler(SPI5, SPI_CR1_BR_FPCLK_DIV_16);  // 84MHz/16 = 5.25MHz
    spi_enable_software_slave_management(SPI5);
    spi_set_nss_high(SPI5);
    spi_set_dff_8bit(SPI5);
    spi_set_clock_polarity_1(SPI5);  // CPOL=1
    spi_set_clock_phase_1(SPI5);     // CPHA=1 -> Mode 3

    spi_enable(SPI5);
}

void system_init(void) {
    rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);
    leds_init();

    devoptab_list[0] = &dotab_cdcacm;
    devoptab_list[1] = &dotab_cdcacm;
    devoptab_list[2] = &dotab_cdcacm;
    cdcacm_f429_init();
}

void console_init(void) {
    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    while (lo_poll(stdin) > 0) {
        getc(stdin);
    }
    printf("Ready\r\n");
}


// Main

int main(void) {
    system_init();
    console_init();
    spi_init();

    uint8_t who = read_reg(I3G4250D_WHO_AM_I);
    printf("WHO_AM_I: 0x%02X (expect 0xD3)\r\n", who);

    write_reg(I3G4250D_CTRL_REG1,
              I3G4250D_CTRL_REG1_PD  |
              I3G4250D_CTRL_REG1_XEN |
              I3G4250D_CTRL_REG1_YEN |
              I3G4250D_CTRL_REG1_ZEN |
              (3 << I3G4250D_CTRL_REG1_BW_SHIFT));

    write_reg(I3G4250D_CTRL_REG4, (1 << I3G4250D_CTRL_REG4_FS_SHIFT));

    int16_t gx, gy, gz;

    while (1) {
        
        while (!(read_reg(I3G4250D_STATUS_REG) & I3G4250D_STATUS_ZYXDA));

        read_axes_burst(&gx, &gy, &gz);

        fwrite(&gx, 2, 1, stdout);
        fwrite(&gy, 2, 1, stdout);
        fwrite(&gz, 2, 1, stdout);

        led_green_on();
        for (int i = 0; i < 100000; i++); // delay (must be set to a small enough value so its visible but does not stall data transmission).
        led_green_off();        
    }
}