#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>

#include <libopencm3-plus/newlib/syscall.h>
#include <libopencm3-plus/newlib/devices/cdcacm.h>

#include <stdio.h>
#include <string.h>

#include "classifier_invariant.h"

#define LED_GREEN_PORT GPIOG
#define LED_GREEN_PIN  GPIO13
#define LED_RED_PORT   GPIOG
#define LED_RED_PIN    GPIO14

static void system_init(void) {
    rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);

    rcc_periph_clock_enable(RCC_GPIOG);
    gpio_mode_setup(GPIOG, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13 | GPIO14);
    gpio_clear(GPIOG, GPIO13 | GPIO14);

    devoptab_list[0] = &dotab_cdcacm;
    devoptab_list[1] = &dotab_cdcacm;
    devoptab_list[2] = &dotab_cdcacm;
    cdcacm_f429_init();

    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
}

static void delay(void) {
    for (volatile int i = 0; i < 2000000; i++);
}

int main(void) {
    system_init();

    /* Hardcoded feature vectors (18 elements each) to exercise every branch
     * of the decision tree in classifier_invariant.h:
     *
     *   input[4] (gy_range) <= 766.09  → rest
     *   input[4] > 766.09 && input[5] (gy_energy) <= 23020.5 → vertical_shake
     *   input[4] > 766.09 && input[5] (gy_energy) >  23020.5 → horizontal_shake
     */

    float feat_rest[18]       = {0};
    feat_rest[4] = 500.0f;
    feat_rest[5] = 100.0f;

    float feat_vertical[18]   = {0};
    feat_vertical[4] = 1000.0f;
    feat_vertical[5] = 10000.0f;

    float feat_horizontal[18] = {0};
    feat_horizontal[4] = 1000.0f;
    feat_horizontal[5] = 30000.0f;

    float *test_cases[] = { feat_rest, feat_vertical, feat_horizontal };
    int n_cases = 3;

    while (1) {
        for (int i = 0; i < n_cases; i++) {
            int cls = predict_invariant(test_cases[i]);
            const char *name = GESTURE_NAMES_INV[cls];

            printf("Test %d: %s\r\n", i, name);

            if (cls == 0) gpio_set(LED_GREEN_PORT, LED_GREEN_PIN);
            if (cls == 2) gpio_set(LED_RED_PORT, LED_RED_PIN);

            delay();

            gpio_clear(GPIOG, GPIO13 | GPIO14);

            delay();
        }
    }
}
