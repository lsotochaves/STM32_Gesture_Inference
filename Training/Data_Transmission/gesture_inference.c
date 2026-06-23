/*
 * gesture_inference.c — STM32F429-DISCO
 *
 * Reads gyro data (I3G4250D via SPI5), computes 26 features over a
 * 2-second window (~200 samples at 100 Hz ODR), and classifies the
 * gesture using the Decision Tree exported from sklearn (classifier.h).
 *
 * Usage:
 *   1. Hold the board still and power on — both LEDs light during bias
 *      calibration (~1 s), then turn off.
 *   2. Press the blue user button (PA0) to start a 2-second recording.
 *      Green LED stays solid while recording.
 *   3. Prediction is shown for 3 s:
 *        rest             -> both LEDs off
 *        horizontal_shake -> green LED (GPIO13)
 *        vertical_shake   -> red   LED (GPIO14)
 *   4. Board returns to slow-blink "ready" state.
 *
 * Class indices (alphabetical, as exported by sklearn):
 *   0 = horizontal_shake
 *   1 = rest
 *   2 = vertical_shake
 */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#include "classifier.h"

/* ---------------------------------------------------------------------------
 * Hardware definitions
 * ------------------------------------------------------------------------- */

#define LED_GREEN_PORT  GPIOG
#define LED_GREEN_PIN   GPIO13
#define LED_RED_PORT    GPIOG
#define LED_RED_PIN     GPIO14

#define BTN_PORT        GPIOA
#define BTN_PIN         GPIO0   /* Blue user button — high when pressed */

/* I3G4250D gyroscope via SPI5 */
#define GYRO_CS_PORT    GPIOC
#define GYRO_CS_PIN     GPIO1
#define GYRO_CS         GYRO_CS_PORT, GYRO_CS_PIN

#define GYRO_RDWR       (1u << 7)
#define GYRO_MS         (1u << 6)   /* auto-increment for burst read */

#define REG_WHO_AM_I    0x0F
#define REG_CTRL1       0x20
#define REG_CTRL4       0x23
#define REG_STATUS      0x27
#define REG_OUT_X_L     0x28

#define STATUS_ZYXDA    (1u << 3)

/* ---------------------------------------------------------------------------
 * Inference parameters
 * ------------------------------------------------------------------------- */

#define N_SAMPLES   200     /* 2 s × 100 Hz ODR */
#define N_CALIB     100     /* samples for startup bias estimation */
#define N_FEATURES  26

#define CLASS_HORIZONTAL  0
#define CLASS_REST        1
#define CLASS_VERTICAL    2

/* ---------------------------------------------------------------------------
 * Sample buffers (bias-corrected float values)
 * ------------------------------------------------------------------------- */

static float buf_gx[N_SAMPLES];
static float buf_gy[N_SAMPLES];
static float buf_gz[N_SAMPLES];

/* ---------------------------------------------------------------------------
 * LED helpers
 * ------------------------------------------------------------------------- */

static void leds_init(void) {
    rcc_periph_clock_enable(RCC_GPIOG);
    gpio_mode_setup(GPIOG, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                    LED_GREEN_PIN | LED_RED_PIN);
    gpio_clear(GPIOG, LED_GREEN_PIN | LED_RED_PIN);
}

static void led_green_on(void)  { gpio_set(LED_GREEN_PORT, LED_GREEN_PIN); }
static void led_green_off(void) { gpio_clear(LED_GREEN_PORT, LED_GREEN_PIN); }
static void led_red_on(void)    { gpio_set(LED_RED_PORT, LED_RED_PIN); }
static void led_red_off(void)   { gpio_clear(LED_RED_PORT, LED_RED_PIN); }

/* ---------------------------------------------------------------------------
 * User button (PA0 — high when pressed, board has external pull-down)
 * ------------------------------------------------------------------------- */

static void btn_init(void) {
    rcc_periph_clock_enable(RCC_GPIOA);
    gpio_mode_setup(BTN_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, BTN_PIN);
}

static bool btn_pressed(void) {
    return gpio_get(BTN_PORT, BTN_PIN) != 0;
}

/* ---------------------------------------------------------------------------
 * SPI / Gyroscope
 * ------------------------------------------------------------------------- */

static void spi_setup(void) {
    rcc_periph_clock_enable(RCC_SPI5);
    rcc_periph_clock_enable(RCC_GPIOF);

    /* SCK=PF7  MISO=PF8  MOSI=PF9  (AF5) */
    gpio_mode_setup(GPIOF, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO7 | GPIO8 | GPIO9);
    gpio_set_af(GPIOF, GPIO_AF5, GPIO7 | GPIO8 | GPIO9);

    /* CS — idle high */
    rcc_periph_clock_enable(RCC_GPIOC);
    gpio_set(GYRO_CS);
    gpio_mode_setup(GYRO_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GYRO_CS_PIN);

    spi_set_master_mode(SPI5);
    spi_set_baudrate_prescaler(SPI5, SPI_CR1_BR_FPCLK_DIV_16); /* ~5.25 MHz */
    spi_enable_software_slave_management(SPI5);
    spi_set_nss_high(SPI5);
    spi_set_dff_8bit(SPI5);
    spi_set_clock_polarity_1(SPI5);  /* CPOL=1, CPHA=1 → Mode 3 */
    spi_set_clock_phase_1(SPI5);
    spi_enable(SPI5);
}

static uint8_t gyro_read_reg(uint8_t reg) {
    uint8_t val;
    gpio_clear(GYRO_CS);
    spi_send(SPI5, GYRO_RDWR | (reg & 0x3F));
    spi_read(SPI5);
    spi_send(SPI5, 0x00);
    val = spi_read(SPI5);
    gpio_set(GYRO_CS);
    return val;
}

static void gyro_write_reg(uint8_t reg, uint8_t data) {
    gpio_clear(GYRO_CS);
    spi_send(SPI5, ~GYRO_RDWR & (reg & 0x3F));
    spi_read(SPI5);
    spi_send(SPI5, data);
    spi_read(SPI5);
    gpio_set(GYRO_CS);
}

static void gyro_read_burst(int16_t *gx, int16_t *gy, int16_t *gz) {
    uint8_t buf[6];
    gpio_clear(GYRO_CS);
    spi_send(SPI5, GYRO_RDWR | GYRO_MS | (REG_OUT_X_L & 0x3F));
    spi_read(SPI5);
    for (int i = 0; i < 6; i++) {
        spi_send(SPI5, 0x00);
        buf[i] = spi_read(SPI5);
    }
    gpio_set(GYRO_CS);
    *gx = (int16_t)((buf[1] << 8) | buf[0]);
    *gy = (int16_t)((buf[3] << 8) | buf[2]);
    *gz = (int16_t)((buf[5] << 8) | buf[4]);
}

static void gyro_init(void) {
    gyro_write_reg(REG_CTRL1,
                   (1u << 3) |   /* PD: normal mode */
                   (1u << 0) |   /* Xen */
                   (1u << 1) |   /* Yen */
                   (1u << 2) |   /* Zen */
                   (3u << 4));   /* BW=3, DR=00 → ODR=100 Hz */
    gyro_write_reg(REG_CTRL4, (1u << 4)); /* FS=500 dps */
}

/* ---------------------------------------------------------------------------
 * Busy-wait delay (~1 ms per call unit at 168 MHz)
 * Each inner loop: ~4 cycles (load, dec, store, branch) → 42000 × 4 = 168000
 * ------------------------------------------------------------------------- */

static void delay_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        volatile uint32_t c = 42000;
        while (c--);
    }
}

/* ---------------------------------------------------------------------------
 * Bias calibration — hold still during startup
 * ------------------------------------------------------------------------- */

static float bias_gx = 0.0f, bias_gy = 0.0f, bias_gz = 0.0f;

static void calibrate(void) {
    float sx = 0, sy = 0, sz = 0;
    int16_t gx, gy, gz;
    for (int i = 0; i < N_CALIB; i++) {
        while (!(gyro_read_reg(REG_STATUS) & STATUS_ZYXDA));
        gyro_read_burst(&gx, &gy, &gz);
        sx += gx; sy += gy; sz += gz;
    }
    bias_gx = sx / N_CALIB;
    bias_gy = sy / N_CALIB;
    bias_gz = sz / N_CALIB;
}

/* ---------------------------------------------------------------------------
 * Collect one gesture window (bias-corrected)
 * ------------------------------------------------------------------------- */

static void collect_window(void) {
    int16_t gx, gy, gz;
    for (int i = 0; i < N_SAMPLES; i++) {
        while (!(gyro_read_reg(REG_STATUS) & STATUS_ZYXDA));
        gyro_read_burst(&gx, &gy, &gz);
        buf_gx[i] = (float)gx - bias_gx;
        buf_gy[i] = (float)gy - bias_gy;
        buf_gz[i] = (float)gz - bias_gz;
    }
}

/* ---------------------------------------------------------------------------
 * Feature computation — must match feature_extractor.py exactly
 *
 * Feature order (26 total):
 *   [0-6]   gx: mean std min max range energy zcr
 *   [7-13]  gy: mean std min max range energy zcr
 *   [14-20] gz: mean std min max range energy zcr
 *   [21]    mag_mean
 *   [22]    mag_max
 *   [23]    mag_std
 *   [24]    mag_energy
 *   [25]    gy_gx_energy_ratio
 * ------------------------------------------------------------------------- */

/* Fills 7 slots: [mean, std, min, max, range, energy, zcr] */
static void axis_features(const float *x, int n, float *out) {
    float sum = 0, mn = x[0], mx = x[0], sq = 0;
    for (int i = 0; i < n; i++) {
        sum += x[i];
        sq  += x[i] * x[i];
        if (x[i] < mn) mn = x[i];
        if (x[i] > mx) mx = x[i];
    }
    float mean = sum / n;
    float energy = sq / n;

    float var = 0;
    for (int i = 0; i < n; i++) var += (x[i] - mean) * (x[i] - mean);
    float std = sqrtf(var / n);

    int crossings = 0;
    int prev = (x[0] >= 0.0f) ? 1 : -1;
    for (int i = 1; i < n; i++) {
        int cur = (x[i] >= 0.0f) ? 1 : -1;
        if (cur != prev) crossings++;
        prev = cur;
    }
    float zcr = (float)crossings / (n - 1);

    out[0] = mean;
    out[1] = std;
    out[2] = mn;
    out[3] = mx;
    out[4] = mx - mn;
    out[5] = energy;
    out[6] = zcr;
}

static void compute_features(float *feat) {
    /* Per-axis features */
    axis_features(buf_gx, N_SAMPLES, feat + 0);   /* feat[0..6]   */
    axis_features(buf_gy, N_SAMPLES, feat + 7);   /* feat[7..13]  */
    axis_features(buf_gz, N_SAMPLES, feat + 14);  /* feat[14..20] */

    /* Magnitude: two passes to get mean before computing std */
    float mag_sum = 0, mag_max = 0, mag_sq = 0;
    for (int i = 0; i < N_SAMPLES; i++) {
        float m = sqrtf(buf_gx[i]*buf_gx[i] + buf_gy[i]*buf_gy[i] + buf_gz[i]*buf_gz[i]);
        mag_sum += m;
        mag_sq  += m * m;
        if (m > mag_max) mag_max = m;
    }
    float mag_mean   = mag_sum / N_SAMPLES;
    float mag_energy = mag_sq  / N_SAMPLES;

    float mag_var = 0;
    for (int i = 0; i < N_SAMPLES; i++) {
        float m = sqrtf(buf_gx[i]*buf_gx[i] + buf_gy[i]*buf_gy[i] + buf_gz[i]*buf_gz[i]);
        mag_var += (m - mag_mean) * (m - mag_mean);
    }

    feat[21] = mag_mean;
    feat[22] = mag_max;
    feat[23] = sqrtf(mag_var / N_SAMPLES);
    feat[24] = mag_energy;

    /* gy_energy / gx_energy ratio (feat[12] and feat[5] already computed) */
    float gx_energy = feat[5];
    float gy_energy = feat[12];
    feat[25] = (gx_energy != 0.0f) ? (gy_energy / gx_energy) : 1e9f;
}

/* ---------------------------------------------------------------------------
 * LED output
 * ------------------------------------------------------------------------- */

static void show_result(int class_idx) {
    led_green_off();
    led_red_off();
    switch (class_idx) {
        case CLASS_HORIZONTAL: led_green_on(); break;
        case CLASS_VERTICAL:   led_red_on();   break;
        case CLASS_REST:                        break; /* both off */
        default:               led_green_on(); led_red_on(); break;
    }
}

/* ---------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */

int main(void) {
    rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);
    leds_init();
    btn_init();
    spi_setup();
    gyro_init();

    /* Calibrate with both LEDs on — hold board still */
    led_green_on();
    led_red_on();
    calibrate();
    led_green_off();
    led_red_off();

    while (1) {
        /* Slow green blink = ready, waiting for button press */
        led_green_on();  delay_ms(200);
        led_green_off(); delay_ms(800);

        if (!btn_pressed()) continue;

        /* Debounce */
        delay_ms(50);
        while (btn_pressed());  /* wait for release */
        delay_ms(50);

        /* Solid green = recording window */
        led_green_on();
        collect_window();
        led_green_off();

        /* Classify */
        float features[N_FEATURES];
        compute_features(features);
        int gesture = predict(features);

        /* Show result for 3 seconds, then return to ready */
        show_result(gesture);
        delay_ms(3000);
        led_green_off();
        led_red_off();
    }

    return 0;
}
