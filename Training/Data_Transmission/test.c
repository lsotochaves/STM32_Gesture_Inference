#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>

#include <libopencm3-plus/newlib/syscall.h>
#include <libopencm3-plus/newlib/devices/cdcacm.h>

#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define RECORD_SAMPLES 50

static float rec_gx[RECORD_SAMPLES];
static float rec_gy[RECORD_SAMPLES];
static float rec_gz[RECORD_SAMPLES];
static float mag_buf[RECORD_SAMPLES];

#define NUM_FEATURES 18

static float feat_mean(const float *v, int n) {
    float s = 0;
    for (int i = 0; i < n; i++) s += v[i];
    return s / n;
}

static float feat_std(const float *v, int n) {
    float m = feat_mean(v, n);
    float s = 0;
    for (int i = 0; i < n; i++) { float d = v[i] - m; s += d * d; }
    return s / n;
}

static float feat_energy(const float *v, int n) {
    float s = 0;
    for (int i = 0; i < n; i++) s += v[i] * v[i];
    return s / n;
}

static float feat_zcr(const float *v, int n) {
    if (n < 2) return 0.0f;
    int crossings = 0;
    for (int i = 1; i < n; i++) {
        int prev = (v[i-1] >= 0) ? 1 : -1;
        int curr = (v[i]   >= 0) ? 1 : -1;
        if (prev != curr) crossings++;
    }
    return (float)crossings / (n - 1);
}

static void feat_minmax(const float *v, int n, float *lo, float *hi) {
    *lo = v[0]; *hi = v[0];
    for (int i = 1; i < n; i++) {
        if (v[i] < *lo) *lo = v[i];
        if (v[i] > *hi) *hi = v[i];
    }
}

static void extract_features(int n, float *out) {
    float lo, hi;
    for (int i = 0; i < n; i++) {
        mag_buf[i] = rec_gx[i]*rec_gx[i] +
                     rec_gy[i]*rec_gy[i] +
                     rec_gz[i]*rec_gz[i];
    }

    const float *axes[3] = { rec_gx, rec_gy, rec_gz };
    float energies[3];
    for (int a = 0; a < 3; a++) {
        int base = a * 4;
        out[base + 0] = feat_std(axes[a], n);
        feat_minmax(axes[a], n, &lo, &hi);
        out[base + 1] = hi - lo;
        energies[a]   = feat_energy(axes[a], n);
        out[base + 2] = energies[a];
        out[base + 3] = feat_zcr(axes[a], n);
    }
    out[12] = feat_mean(mag_buf, n);
    feat_minmax(mag_buf, n, &lo, &hi);
    out[13] = hi;
    out[14] = feat_mean(mag_buf, n);
    out[15] = feat_energy(mag_buf, n);
    float total_e = energies[0] + energies[1] + energies[2];
    if (total_e > 0) {
        float mx = energies[0], mn = energies[0];
        for (int i = 1; i < 3; i++) {
            if (energies[i] > mx) mx = energies[i];
            if (energies[i] < mn) mn = energies[i];
        }
        out[16] = mx / total_e;
        out[17] = mn / total_e;
    } else {
        out[16] = 0.0f;
        out[17] = 0.0f;
    }
}

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

    int32_t sx = 0, sy = 0, sz = 0;
    for (int s = 0; s < 50; s++) {
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
        sx += (int16_t)((buf[1] << 8) | buf[0]);
        sy += (int16_t)((buf[3] << 8) | buf[2]);
        sz += (int16_t)((buf[5] << 8) | buf[4]);
    }
    float bx = (float)sx / 50;
    float by = (float)sy / 50;
    float bz = (float)sz / 50;

    while (1) {
        printf("A: grabando\r\n");
        for (int s = 0; s < RECORD_SAMPLES; s++) {
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
            rec_gx[s] = (float)((int16_t)((buf[1] << 8) | buf[0])) - bx;
            rec_gy[s] = (float)((int16_t)((buf[3] << 8) | buf[2])) - by;
            rec_gz[s] = (float)((int16_t)((buf[5] << 8) | buf[4])) - bz;
        }

        printf("B: grabacion lista\r\n");

        float features[NUM_FEATURES];
        extract_features(RECORD_SAMPLES, features);

        printf("C: features listos, f4=%.1f f5=%.1f\r\n", features[4], features[5]);
        for (volatile int i = 0; i < 1000000; i++);
    }
}
