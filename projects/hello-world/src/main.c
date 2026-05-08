#include <stdio.h>
#include "pico/stdlib.h"

int main() {
    stdio_init_all();

    // Wait for USB CDC serial connection before printing
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    while (true) {
        printf("Hello, World!\n");
        sleep_ms(1000);
    }
}
