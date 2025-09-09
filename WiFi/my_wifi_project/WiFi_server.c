#include <stdio.h>
#include <string.h> 
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"

// TCP/IP (lwIP) のためのヘッダー
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#define TCP_PORT 80

//NOTE: UART用のPIN
#define UART_ID uart0
#define BAUD_RATE 115200

//NOTE: UART用のPIN(書き込み装置使用時)
#define UART_TX_PIN 12
#define UART_RX_PIN 13

//NOTE: PWM用のPIN
#define PWM_PIN 11

// Wi-Fiの認証情報
char ssid[] = "SPWH_L12_af31f5";
char pass[] = "1a73892d66094";

// コールバック関数 (ネットワーク処理) と main ループの間で情報をやり取りするためのグローバル変数
volatile int g_web_signal_received = 0;


/**
 * [コールバック関数 1] クライアントからデータを受信した時
 */
static err_t tcp_server_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {

    char request_buffer[128];

    if (p != NULL) {
        // --- リクエスト内容の解析 ---
        if (p->len < sizeof(request_buffer) - 1) {
            memcpy(request_buffer, p->payload, p->len);
            request_buffer[p->len] = '\0';
        } else {
            memcpy(request_buffer, p->payload, sizeof(request_buffer) - 1);
            request_buffer[sizeof(request_buffer) - 1] = '\0';
        }

        if (strstr(request_buffer, "GET /push1")) {
            // PUSH1 が押された
            printf("get push1\n"); 
            g_web_signal_received = 1; // フラグに「1」をセット

        } else if (strstr(request_buffer, "GET /push2")) {
            // PUSH2 が押された
            printf("get push2\n"); 
            g_web_signal_received = 2; // フラグに「2」をセット

        } else if (strstr(request_buffer, "GET /push3")) {
            // PUSH3 が押された
            printf("get push3\n"); 
            g_web_signal_received = 3; // フラグに「3」をセット
        
        } else {
            // それ以外 (ルート "/" へのアクセスなど)
            printf("Root access or unknown request.\n");
        }
        
        // --- レスポンスの送信 ---
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);

        const char http_response[] = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<html><body>"
            "<h1>Ground Station</h1>"
            "<p>Take the picture of the earth!</p>"
            "<br>"
            "<a href=\"/push1\"><button style=\"font-size: 20px; padding: 10px;\">PUSH1!</button></a>"
            "<a href=\"/push2\"><button style=\"font-size: 20px; padding: 10px;\">PUSH2!</button></a>"
            "<a href=\"/push3\"><button style=\"font-size: 20px; padding: 10px;\">PUSH3!</button></a>"
            "</body></html>";

        cyw43_arch_lwip_check();
        tcp_write(tpcb, http_response, sizeof(http_response) - 1, TCP_WRITE_FLAG_COPY); 
        tcp_close(tpcb);

    } else {
        tcp_close(tpcb);
    }
    return ERR_OK;
}

/**
 * [コールバック関数 2] 新しいクライアントが接続してきた時
 */
static err_t tcp_server_accept_callback(void *arg, struct tcp_pcb *new_pcb, err_t err) {
    if (new_pcb == NULL) {
        return ERR_VAL;
    }
    printf("Client connected from browser.\n");
    tcp_recv(new_pcb, tcp_server_recv_callback); // データ受信時のコールバックを設定
    return ERR_OK;
}

/**
 * [セットアップ関数] TCPサーバーを開始する
 */
static void setup_tcp_server(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) { return; }

    if (tcp_bind(pcb, IP_ADDR_ANY, TCP_PORT) != ERR_OK) {
        printf("Failed to bind\n");
        return;
    }

    pcb = tcp_listen(pcb);
    if (!pcb) { return; }

    printf("TCP Server is Listening on port %d\n", TCP_PORT);
    tcp_accept(pcb, tcp_server_accept_callback); // 接続受付時のコールバックを設定
}


/**
 * Wi-Fiの初期化、接続、Webサーバーの起動までを一つにまとめた関数
 */
void start_wifi_web_server(void) {
    
    // 1. Wi-Fiチップ (CYW43) を初期化
    if (cyw43_arch_init()) {
        printf("FAILED to initialise Wi-Fi chip\n");
        return;
    }
    printf("Initialised.\n");

    // 2. ステーションモード (STAモード) を有効化
    cyw43_arch_enable_sta_mode();
    printf("Connecting to Wi-Fi: %s ...\n", ssid);

    // 3. Wi-Fiへの接続
    if (cyw43_arch_wifi_connect_timeout_ms(ssid, pass, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("FAILED to connect Wifi.\n");
    } else {
        printf("Connected successfully Wifi.\n");
        printf("My IP address is: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list))); 
        
        // 4. Webサーバーをセットアップ
        setup_tcp_server();
    }
}


/**
 * --- メイン関数 ---
 */
int main() {
    stdio_init_all();

    //NOTE: UARTの初期化
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    uart_puts(UART_ID, " Hello, UART!\n");

    gpio_set_function(PWM_PIN, GPIO_FUNC_PWM);


    //NOTE: PWMの初期化
    // 指定したGPIOピンが接続されているPWMスライス番号を取得
    uint slice_num = pwm_gpio_to_slice_num(PWM_PIN);

    // --- 周波数設定 (50Hz) ---
    // 1. クロック分周比を設定 (125.0f)
    // システムクロック(125MHz)を125で分周 -> 1MHzのPWMカウンタークロック
    // これにより、1カウントが1マイクロ秒(µs)になります。
    pwm_set_clkdiv(slice_num, 125.0f);

    // 2. ラップ値を設定 (19999)
    // 周期が20000カウント = 20000µs = 20ms となり、周波数が50Hzになります。
    uint16_t wrap_value = 19999;
    pwm_set_wrap(slice_num, wrap_value);

    // 目標のデューティ比からレベル値を計算
    uint16_t level_cw_per = 4;  // NOTE: 目標duty比4%
    uint16_t level_cw_per_ver2 = 6; // NOTE: 目標duty比10%
    uint16_t level_stop_per = 8; // NOTE: 目標duty比10%
    uint16_t level_ccw_per = 10; // NOTE: 目標duty比10%

    // (19999 + 1) * 4 / 100 = 800
    uint16_t level_cw = (wrap_value + 1) * level_cw_per / 100;
    uint16_t level_cw_ver2 = (wrap_value + 1) * level_cw_per_ver2 / 100;
    uint16_t level_stop = (wrap_value + 1) * level_stop_per / 100;
    // (19999 + 1) * 10 / 100 = 2000
    uint16_t level_ccw = (wrap_value + 1) * level_ccw_per / 100;

    //NOTE: Wi-Fiの初期化
    start_wifi_web_server();
    // メインの無限ループ
     while (true) {
        // ネットワークスタックを動かし続けるために必須 (絶対にループから外したり、長時間停止させないこと)
        cyw43_arch_poll();

        if (g_web_signal_received == 1) {
            // --- PUSH1 (CW)が押された時の処理 ---
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(1000);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            sleep_ms(1000);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(1000);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

        } else if (g_web_signal_received == 2) {
            // --- PUSH2 (CCW)が押された時の処理 ---
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(2000);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            sleep_ms(2000);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(2000);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

        } else if (g_web_signal_received == 3) {
            // --- PUSH3 (STOP)が押された時の処理 ---
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(5000);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            sleep_ms(5000);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(5000);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        }

        // フラグが処理されたら(0以外だったら)、必ず0に戻す
        if (g_web_signal_received != 0) {
             g_web_signal_received = 0;
        }

        sleep_ms(1); // CPUを少し休ませる (この程度の短いsleepはOK)
    }
}
