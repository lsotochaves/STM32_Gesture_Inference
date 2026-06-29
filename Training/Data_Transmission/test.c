#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>

#include <libopencm3-plus/newlib/syscall.h>
#include <libopencm3-plus/newlib/devices/cdcacm.h>

#include <stdio.h>

#define LED_GREEN_PORT GPIOG
#define LED_GREEN_PIN  GPIO13

void system_init(void) {
    rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);

    rcc_periph_clock_enable(RCC_GPIOG);
    gpio_mode_setup(GPIOG, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13);
    gpio_clear(GPIOG, GPIO13);

    devoptab_list[0] = &dotab_cdcacm;
    devoptab_list[1] = &dotab_cdcacm;
    devoptab_list[2] = &dotab_cdcacm;
    cdcacm_f429_init();

    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
}

int main(void) {
    system_init();

    while (1) {
        printf("hola\r\n");
        gpio_toggle(LED_GREEN_PORT, LED_GREEN_PIN);
        for (volatile int i = 0; i < 1000000; i++);
    }
}
