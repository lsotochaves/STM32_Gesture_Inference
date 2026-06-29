#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>

#include <libopencm3-plus/newlib/syscall.h>
#include <libopencm3-plus/newlib/devices/cdcacm.h>
#include <libopencm3-plus/utils/misc.h>

#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "classifier_invariant.h"

/* ------------------------------------------------------------------ */
/* Hardware defines (same as stm_transmitter.c)                       */
/* ------------------------------------------------------------------ */

#define LED_GREEN_PIN    GPIO13
#define LED_GREEN_PORT   GPIOG
#define LED_RED_PIN      GPIO14
#define LED_RED_PORT     GPIOG

#define I3G4250D_RDWR        (1 << 7)
#define I3G4250D_CS          GPIOC, GPIO1

#define I3G4250D_WHO_AM_I    0x0F
#define I3G4250D_STATUS_REG  0x27
#define I3G4250D_CTRL_REG1   0x20
#define I3G4250D_CTRL_REG4   0x23

#define I3G4250D_CTRL_REG1_XEN   (1 << 0)
#define I3G4250D_CTRL_REG1_YEN   (1 << 1)
#define I3G4250D_CTRL_REG1_ZEN   (1 << 2)
#define I3G4250D_CTRL_REG1_PD    (1 << 3)
#define I3G4250D_CTRL_REG1_BW_SHIFT 4

#define I3G4250D_CTRL_REG4_FS_SHIFT 4

#define I3G4250D_STATUS_ZYXDA (1 << 3)

#define I3G4250D_OUT_X_L 0x28

/* ------------------------------------------------------------------ */
/* Sampling config                                                    */
/* ------------------------------------------------------------------ */

#define CALIB_SAMPLES   50   /* ~0.5 s at 100 Hz ODR */
#define RECORD_SAMPLES  200  /* ~2.0 s at 100 Hz ODR */
#define NUM_FEATURES    18

/* ------------------------------------------------------------------ */
/* Buffers                                                            */
/* ------------------------------------------------------------------ */

static int16_t calib_raw[CALIB_SAMPLES][3];

static float rec_gx[RECORD_SAMPLES];
static float rec_gy[RECORD_SAMPLES];
static float rec_gz[RECORD_SAMPLES];

/* ------------------------------------------------------------------ */
/* LED helpers                                                        */
/* ------------------------------------------------------------------ */

static void leds_init(void) {
    rcc_periph_clock_enable(RCC_GPIOG);
    gpio_mode_setup(GPIOG, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13 | GPIO14);
    gpio_clear(GPIOG, GPIO13 | GPIO14);
}

/* ------------------------------------------------------------------ */
/* SPI / gyro helpers (identical to transmitter)                      */
/* ------------------------------------------------------------------ */

static uint8_t read_reg(uint8_t reg) {
    uint8_t tmp = I3G4250D_RDWR | (reg & 0x3F);
    gpio_clear(I3G4250D_CS);
    spi_send(SPI5, tmp);  spi_read(SPI5);
    spi_send(SPI5, 0x00); tmp = spi_read(SPI5);
    gpio_set(I3G4250D_CS);
    return tmp;
}

static uint8_t write_reg(uint8_t reg, uint8_t data) {
    uint8_t tmp = ~I3G4250D_RDWR & (reg & 0x3F);
    gpio_clear(I3G4250D_CS);
    spi_send(SPI5, tmp);  spi_read(SPI5);
    spi_send(SPI5, data); tmp = spi_read(SPI5);
    gpio_set(I3G4250D_CS);
    return tmp;
}

static void read_axes_burst(int16_t *gx, int16_t *gy, int16_t *gz) {
    uint8_t buf[6];
    uint8_t cmd = I3G4250D_RDWR | (1 << 6) | (I3G4250D_OUT_X_L & 0x3F);
    gpio_clear(I3G4250D_CS);
    spi_send(SPI5, cmd); spi_read(SPI5);
    for (int i = 0; i < 6; i++) {
        spi_send(SPI5, 0x00);
        buf[i] = spi_read(SPI5);
    }
    gpio_set(I3G4250D_CS);
    *gx = (int16_t)((buf[1] << 8) | buf[0]);
    *gy = (int16_t)((buf[3] << 8) | buf[2]);
    *gz = (int16_t)((buf[5] << 8) | buf[4]);
}

static void wait_data_ready(void) {
    while (!(read_reg(I3G4250D_STATUS_REG) & I3G4250D_STATUS_ZYXDA))
        ;
}

/* ------------------------------------------------------------------ */
/* Init                                                               */
/* ------------------------------------------------------------------ */

static void spi_setup(void) {
    rcc_periph_clock_enable(RCC_SPI5);
    rcc_periph_clock_enable(RCC_GPIOF);
    gpio_mode_setup(GPIOF, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO7 | GPIO8 | GPIO9);
    gpio_set_af(GPIOF, GPIO_AF5, GPIO7 | GPIO8 | GPIO9);
    rcc_periph_clock_enable(RCC_GPIOC);
    gpio_set(I3G4250D_CS);
    gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO1);
    spi_set_master_mode(SPI5);
    spi_set_baudrate_prescaler(SPI5, SPI_CR1_BR_FPCLK_DIV_16);
    spi_enable_software_slave_management(SPI5);
    spi_set_nss_high(SPI5);
    spi_set_dff_8bit(SPI5);
    spi_set_clock_polarity_1(SPI5);
    spi_set_clock_phase_1(SPI5);
    spi_enable(SPI5);
}

static void system_setup(void) {
    rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);
    leds_init();
    devoptab_list[0] = &dotab_cdcacm;
    devoptab_list[1] = &dotab_cdcacm;
    devoptab_list[2] = &dotab_cdcacm;
    cdcacm_f429_init();
}

static void gyro_setup(void) {
    write_reg(I3G4250D_CTRL_REG1,
              I3G4250D_CTRL_REG1_PD  |
              I3G4250D_CTRL_REG1_XEN |
              I3G4250D_CTRL_REG1_YEN |
              I3G4250D_CTRL_REG1_ZEN |
              (3 << I3G4250D_CTRL_REG1_BW_SHIFT));
    write_reg(I3G4250D_CTRL_REG4, (1 << I3G4250D_CTRL_REG4_FS_SHIFT));
}

/* ------------------------------------------------------------------ */
/* Feature extraction helpers                                         */
/* ------------------------------------------------------------------ */

static float feat_mean(const float *v, int n) {
    float s = 0;
    for (int i = 0; i < n; i++) s += v[i];
    return s / n;
}

static float feat_std(const float *v, int n) {
    float m = feat_mean(v, n);
    float s = 0;
    for (int i = 0; i < n; i++) {
        float d = v[i] - m;
        s += d * d;
    }
    return sqrtf(s / n);
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

/* ------------------------------------------------------------------ */
/* Build the 18-element invariant feature vector                      */
/*                                                                    */
/* Order (must match feature_extractor_invariant.py):                 */
/*  [0]  gx_std    [4]  gy_std    [8]  gz_std                        */
/*  [1]  gx_range  [5]  gy_range  [9]  gz_range                      */
/*  [2]  gx_energy [6]  gy_energy [10] gz_energy                     */
/*  [3]  gx_zcr    [7]  gy_zcr    [11] gz_zcr                        */
/*  [12] mag_mean  [13] mag_max   [14] mag_std  [15] mag_energy      */
/*  [16] max_energy_ratio  [17] min_energy_ratio                     */
/* ------------------------------------------------------------------ */

static void extract_features(int n, float *out) {
    float lo, hi;
    float mag[RECORD_SAMPLES];

    for (int i = 0; i < n; i++)
        mag[i] = sqrtf(rec_gx[i]*rec_gx[i] +
                        rec_gy[i]*rec_gy[i] +
                        rec_gz[i]*rec_gz[i]);

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

    out[12] = feat_mean(mag, n);
    feat_minmax(mag, n, &lo, &hi);
    out[13] = hi;
    out[14] = feat_std(mag, n);
    out[15] = feat_energy(mag, n);

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

/* ------------------------------------------------------------------ */
/* Calibration: collect CALIB_SAMPLES while board is stationary,      */
/* return per-axis bias (mean).                                       */
/* ------------------------------------------------------------------ */

static void calibrate(float *bx, float *by, float *bz) {
    gpio_set(LED_RED_PORT, LED_RED_PIN);

    for (int i = 0; i < CALIB_SAMPLES; i++) {
        wait_data_ready();
        read_axes_burst(&calib_raw[i][0], &calib_raw[i][1], &calib_raw[i][2]);
    }

    int32_t sx = 0, sy = 0, sz = 0;
    for (int i = 0; i < CALIB_SAMPLES; i++) {
        sx += calib_raw[i][0];
        sy += calib_raw[i][1];
        sz += calib_raw[i][2];
    }
    *bx = (float)sx / CALIB_SAMPLES;
    *by = (float)sy / CALIB_SAMPLES;
    *bz = (float)sz / CALIB_SAMPLES;

    gpio_clear(LED_RED_PORT, LED_RED_PIN);
}

/* ------------------------------------------------------------------ */
/* Record: collect RECORD_SAMPLES with bias correction                */
/* ------------------------------------------------------------------ */

static void record(float bx, float by, float bz) {
    int16_t raw_x, raw_y, raw_z;

    gpio_set(LED_GREEN_PORT, LED_GREEN_PIN);

    for (int i = 0; i < RECORD_SAMPLES; i++) {
        wait_data_ready();
        read_axes_burst(&raw_x, &raw_y, &raw_z);
        rec_gx[i] = (float)raw_x - bx;
        rec_gy[i] = (float)raw_y - by;
        rec_gz[i] = (float)raw_z - bz;
    }

    gpio_clear(LED_GREEN_PORT, LED_GREEN_PIN);
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    system_setup();

    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    while (lo_poll(stdin) > 0) getc(stdin);

    spi_setup();
    gyro_setup();

    float bx, by, bz;
    float features[NUM_FEATURES];

    /* One-time calibration at boot — hold the board still */
    calibrate(&bx, &by, &bz);

    while (1) {
        record(bx, by, bz);
        extract_features(RECORD_SAMPLES, features);

        int cls = predict_invariant(features);
        const char *name = GESTURE_NAMES_INV[cls];
        fwrite(name, 1, strlen(name), stdout);
        fwrite("\n", 1, 1, stdout);
    }
}
