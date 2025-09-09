#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h" // Wi-Fi と Pico W内蔵LED の両方を制御するヘッダ

// Wi-Fiの認証情報
char ssid[] = "SPWH_L12_af31f5";
char pass[] = "1a73892d66094";

// LEDの点滅間隔 (ミリ秒)
#define LED_DELAY_MS 250

int main() {
    stdio_init_all();

    // 1. チップを初期化
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_WORLDWIDE)) {
        printf("FAILED to initialise Wi-Fi chip\n");
        return 1; // ここでの失敗は致命的なので終了する
    }
    printf("Initialised.\n");

    // ★リクエスト: 初期化が完了したら（接続試行前でも）LEDを点灯させる
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    // 2. Wi-Fi接続を試みる
    cyw43_arch_enable_sta_mode();
    printf("Connecting to Wi-Fi: %s ...\n", ssid);

    if (cyw43_arch_wifi_connect_timeout_ms(ssid, pass, CYW43_AUTH_WPA2_AES_PSK, 100000)) {
        printf("FAILED to connect Wifi.\n");
        // ★リクエスト: 失敗してもLEDは点灯させ続けたいので、ここでは return しない
    } else {
        printf("Connected successfully Wifi.\n");
    }

    // 3. 無限ループに入り、LEDを点灯させ続ける
    while (true) {
        sleep_ms(1000); // LEDは点灯したままプログラムだけが動作を続ける
    }
}