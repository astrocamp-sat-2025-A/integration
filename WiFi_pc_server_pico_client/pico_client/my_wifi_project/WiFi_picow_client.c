#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "hardware/uart.h"
#include "hardware/spi.h"


#include "SunSensor.h"
#include "pwm.h"
#include "ov7675.h"

// =================================================================
// --- User Settings ---
// =================================================================
const char WIFI_SSID[]     = "SPWH_L12_af31f5"; // Your Wi-Fi SSID
const char WIFI_PASSWORD[] = "1a73892d66094"; // Your Wi-Fi Password
const char SERVER_IP[]     = "192.168.179.31"; // Your PC's IP Address
const uint16_t DATA_PORT    = 12345;
const uint16_t COMMAND_PORT = 12346;

// --- Global State ---
// Flag set by network callback to trigger action in the main loop
volatile int g_start_blink_flag = 0;

//NOTE: カメラ用のグローバル変数
volatile int g_capture_flag = 0; 
// Cumulative counter for total blinks
int g_total_blink_count = 0;
// Persistent connection handle (PCB) for the data channel
struct tcp_pcb *g_data_pcb = NULL;

// =================================================================
// --- TCP_NODELAY Configuration Function ---
// =================================================================

/**
 * @brief TCP_NODELAY設定を切り替える関数
 * @param enable true: NODELAY有効（即座に送信）、false: NODELAY無効（Nagleアルゴリズム有効）
 */
void configure_tcp_nodelay(bool enable) {
    if (g_data_pcb == NULL) {
        printf("Error: No data connection available for TCP_NODELAY configuration\n");
        return;
    }
    
    if (enable) {
        g_data_pcb->flags |= TF_NODELAY;
        printf("TCP_NODELAY enabled (immediate transmission for real-time data)\n");
    } else {
        g_data_pcb->flags &= ~TF_NODELAY;
        printf("TCP_NODELAY disabled (Nagle algorithm enabled for bulk data)\n");
    }
}

//NOTE: 光源と目標物の角度の変数
float sun_angle_from_target = 135.0;


//NOTE: UARTの設定
#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 12
#define UART_RX_PIN 13

//NOTE: SPIの設定
#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19




//NOTE: ifdefの定義, 本番直前に決める
#define USE_SUN_SENSOR
//#define NOT_USE_SUN_SENSOR




// =================================================================
// --- Command Receiver Client (Listens for "START") ---
// =================================================================

// Callback function for when a command is received from the PC
static err_t cmd_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        printf("COMMAND_CLIENT: Connection closed by PC. Halting.\n");
        return ERR_ABRT; // Abort the connection
    }
    
    // Check if the received data contains "START"
    if (pbuf_memfind(p, "START", 5, 0) != 0xFFFF) {
        printf("COMMAND_CLIENT: 'START' command received! Setting flag.\n");
        g_start_blink_flag = 1; // Set the flag for the main loop
    }
    
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

// Callback function for when the command connection is established
static err_t cmd_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    if (err != ERR_OK) {
        printf("COMMAND_CLIENT: Failed to connect: %d\n", err);
        return err;
    }
    printf("COMMAND_CLIENT: Connected to PC. Waiting for commands...\n");
    tcp_recv(tpcb, cmd_client_recv); // Set the function to call when data arrives
    return ERR_OK;
}

// Function to initiate the connection to the command server
static void run_command_client(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    ip_addr_t remote_addr;
    ipaddr_aton(SERVER_IP, &remote_addr);
    
    printf("COMMAND_CLIENT: Connecting to %s:%d...\n", SERVER_IP, COMMAND_PORT);
    cyw43_arch_lwip_begin();
    tcp_connect(pcb, &remote_addr, COMMAND_PORT, cmd_client_connected);
    cyw43_arch_lwip_end();
}


// =================================================================
// --- Data Sender Client (Sends count updates) ---
// =================================================================

// Callback function for when the data connection is established
static err_t data_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    if (err != ERR_OK) {
        printf("DATA_CLIENT: Failed to connect: %d\n", err);
        g_data_pcb = NULL;
        return err;
    }
    printf("DATA_CLIENT: Persistent connection established. Ready to send counts.\n");
    g_data_pcb = tpcb; // Store the connection handle globally
    return ERR_OK;
}

// Function to initiate the connection to the data server
static void run_data_client(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    ip_addr_t remote_addr;
    ipaddr_aton(SERVER_IP, &remote_addr);

    printf("DATA_CLIENT: Connecting to %s:%d...\n", SERVER_IP, DATA_PORT);
    cyw43_arch_lwip_begin();
    tcp_connect(pcb, &remote_addr, DATA_PORT, data_client_connected);
    cyw43_arch_lwip_end();
}

void send_data_to_server(const char* message) {
    // データ接続が確立されていない場合は何もしない
    if (g_data_pcb == NULL) {
        printf("-> Error: No data connection available\n");
        return;
    }

    // 送信間隔制御（50ms間隔）
    static uint32_t last_send_time = 0;
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    if (current_time - last_send_time < 50) { // 50ms間隔
        uint32_t wait_time = 50 - (current_time - last_send_time);
        printf("-> Waiting %dms before next send...\n", wait_time);
        sleep_ms(wait_time);
    }

    // 送信キュー監視
    cyw43_arch_lwip_begin();
    if (tcp_sndbuf(g_data_pcb) < 200) {
        printf("-> Send buffer nearly full, flushing existing data...\n");
        tcp_output(g_data_pcb); // 既存データを送信
    }
    cyw43_arch_lwip_end();

    // 送信するペイロードを作成（メッセージ + 改行コード）
    char payload[256];
    int len = snprintf(payload, sizeof(payload), "%s\n", message);
    if (len >= sizeof(payload)) {
        printf("Error: Payload buffer too small for message: %s\n", message);
        return;
    }

    printf("-> Sending data: %s\n", payload);

    // lwipの関数はスレッドセーフティのためにこのブロックで囲む
    cyw43_arch_lwip_begin();
    
    // 送信前の状態チェック（tcp_stateの代わりにNULLチェック）
    if (g_data_pcb == NULL || g_data_pcb->state != ESTABLISHED) {
        printf("-> Error: Connection not established\n");
        cyw43_arch_lwip_end();
        return;
    }

    err_t err = tcp_write(g_data_pcb, payload, len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        err = tcp_output(g_data_pcb);
        if (err != ERR_OK) {
            printf("-> Failed to flush data. Error: %d\n", err);
        } else {
            printf("-> Data sent successfully\n");
        }
    } else {
        printf("-> Failed to write data. Error: %d\n", err);
    }
    cyw43_arch_lwip_end();

    // 送信時間を記録
    last_send_time = to_ms_since_boot(get_absolute_time());
}
// =================================================================
// --- Main Program Logic ---
// =================================================================
int main() {
    stdio_init_all();

    //NOTE: UARTの初期化
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    //NOTE: SPIの初期化
    spi_init(SPI_PORT, 500 * 1000);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);  // 初期状態はHIGH（非アクティブ）

    //NOTE: I2Cの設定
    //NOTE: I2Cの初期化 
    i2c_init(I2C_PORT, 100 * 1000); 
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); 
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); 
    gpio_pull_up(I2C_SDA); 
    gpio_pull_up(I2C_SCL); 


    //NOTE: カメラの初期化
    Gen_clock(); 
    sleep_ms(20); // I2C初期化まで待機 

    //カメラ初期化 
    ov7675_init();//初期化 
    GPIO_set();//初期化 
    cap_pic_init();//初期化 


    //NOTE: pwmの初期化
    initialisePwm();

    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }
    
    cyw43_arch_enable_sta_mode();
    printf("Connecting to Wi-Fi: %s ...\n", WIFI_SSID);
    
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("FAILED to connect to Wi-Fi.\n");
        return 1;
    }
    printf("Connected to Wi-Fi. My IP is: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

    // Establish both persistent connections at the start
    run_command_client();
    sleep_ms(100); // Small delay to allow the network stack to process
    run_data_client();

    // 基本設定：リアルタイムデータ用にNODELAY有効
    configure_tcp_nodelay(true);

    while (true) {
        // This function is essential to keep the network stack running
        cyw43_arch_poll();

        // Check if the flag has been set by the command receiver
        if (g_start_blink_flag) {
            send_data_to_server("LOG: Flag detected! Starting operation.");

            #ifdef USE_SUN_SENSOR
            uint16_t sunSensor_data[4] = {0, 0, 0, 0};
    
            for(int i = 0; i < 4; i++){
                sunSensor_data[i] = sunSensor_read(i);
            }

            //NOTE: 太陽センサのデータを送信
            char sensor_payload[128];
            sprintf(sensor_payload, "DATA:%d,%d,%d,%d", sunSensor_data[0], sunSensor_data[1], sunSensor_data[2], sunSensor_data[3]);
            send_data_to_server(sensor_payload);
            
            //NOTE: 光源と現在位置の角度を取得
            float sun_angle_from_camera = calculate_sun_angle(sunSensor_data[0], sunSensor_data[1], sunSensor_data[2], sunSensor_data[3]);

            //NOTE: 光源と目標物の角度から動作角度を計算
            float move_angle = sun_angle_from_camera + sun_angle_from_target;
            char move_angle_payload[128];
            sprintf(move_angle_payload, "ANGLE:%f", move_angle);
            send_data_to_server(move_angle_payload);

            for(int i = 0; i < 3; i++){//NOTE: ここの回数は決め打ち
                pwm_cycle_by_angle(move_angle, LEFT);//TODO: ここのRIGHT/LEFTは本番直前に決める
                sleep_ms(5000);//NOTE: ここのdelayは必要かと思う、だいたい5秒経過すれば慣性は消える

                for(int i = 0; i < 4; i++){
                    sunSensor_data[i] = sunSensor_read(i);
                }

                char sensor_payload[128];
                sprintf(sensor_payload, "DATA:%d,%d,%d,%d", sunSensor_data[0], sunSensor_data[1], sunSensor_data[2], sunSensor_data[3]);
                send_data_to_server(sensor_payload);
            

                //NOTE: 光源と目標物の角度を取得
                float sun_angle_from_camera = calculate_sun_angle(sunSensor_data[0], sunSensor_data[1], sunSensor_data[2], sunSensor_data[3]);

                //NOTE: 光源と目標物の角度から動作角度を計算
                move_angle = sun_angle_from_camera - sun_angle_from_target;
                char move_angle_payload[128];
                sprintf(move_angle_payload, "ANGLE:%f", move_angle);
                send_data_to_server(move_angle_payload);

                if(move_angle < 30 && move_angle > -30){
                    send_data_to_server("LOG: skip the loop");
                    break;
                }
            }
            send_data_to_server("LOG: End of the loop");
            #endif

            #ifdef NOT_USE_SUN_SENSOR
            send_data_to_server("LOG: not use sun sensor");
            //NOTE: ランダム関数にて角度を生成する
            for(int i = 0; i < 3; i++){
                send_data_to_server("LOG: move around the motor");
                float move_angle = rand() % 360;
                pwm_cycle_by_angle(move_angle, RIGHT);
            }
            sleep_ms(2000);//NOTE: ここのdelayは必要かと思う、だいたい5秒経過すれば慣性は消える
            #endif

            //TODO: カメラを撮影
            send_data_to_server("LOG: take a picture");
            if(g_capture_flag){ 
                // 画像送信用：効率重視でNODELAY無効
                configure_tcp_nodelay(false);
                 printf("MAIN_LOOP: Blinking session finished.\n"); 
                 printf("MAIN_LOOP: Flag detected! capture Start \n"); 
                 //TODO: カメラを撮影 
                 printf("MAIN_LOOP: カメラを撮影\n"); 
                 capture(); 	// capture()関数を呼び出し 
                                
                 //TODO: 撮影した画像をPCに送信 
                 printf("MAIN_LOOP: 撮影した画像をPCに送信\n"); 
                 if (g_data_pcb != NULL) { 
                      printf("MAIN_LOOP: Sending image data in chunks...\n"); 
                      // まず "IMG:" ヘッダを送信 
                      cyw43_arch_lwip_begin(); 
                      tcp_write(g_data_pcb, "IMG:", 4, TCP_WRITE_FLAG_COPY); 
                      tcp_output(g_data_pcb); 
                      cyw43_arch_lwip_end(); 
                      
                      // 少し待機してヘッダが確実に送信されるようにする 
                      sleep_ms(10); 
                           
                      // forループで画像をチャンクに分割して送信 
                      for (size_t offset = 0; offset < FRAME_SIZE; offset += TCP_CHUNK_SIZE) { 
                           size_t current_chunk_size = (FRAME_SIZE - offset > TCP_CHUNK_SIZE) ? TCP_CHUNK_SIZE : (FRAME_SIZE - offset); 
 
                           cyw43_arch_lwip_begin(); 
                           // 送信バッファに空きができるまで待機 
                           while (tcp_sndbuf(g_data_pcb) < current_chunk_size) { 
                                cyw43_arch_lwip_end(); 
                                cyw43_arch_poll(); 
                                sleep_ms(5); 
                                cyw43_arch_lwip_begin(); 
                           } 
                                
                           // imageバッファの該当位置からデータを送信 
                           err_t err = tcp_write(g_data_pcb, image + offset, current_chunk_size, TCP_WRITE_FLAG_COPY); 
                           
                           if (err == ERR_OK) { 
                                tcp_output(g_data_pcb); // 即時送信 
                           } else { 
                                printf("MAIN_LOOP: TCP write error. Aborting send. Error: %d\n", err); 
                                cyw43_arch_lwip_end(); 
                                break; // エラーなら送信ループを抜ける 
                           } 
                           cyw43_arch_lwip_end(); 
                      } 
                      printf("MAIN_LOOP: Image data sent.\n"); 
                 } else { 
                      printf("MAIN_LOOP: Capture complete, but no active TCP connection.\n"); 
                 } 
                 printf("MAIN_LOOP: Blinking session finished.\n"); 
                     g_capture_flag = 0; // Reset flag after the blinking session is complete 
            } 
            // 送信完了後：リアルタイムデータ用にNODELAY有効に戻す
            configure_tcp_nodelay(true);
            }
            g_start_blink_flag = 0; // Reset flag after the blinking session is complete
            printf("MAIN_LOOP: Blinking session finished.\n");
        }
        sleep_ms(1); // Small delay to yield the CPU
    }
