#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

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
// Cumulative counter for total blinks
int g_total_blink_count = 0;
// Persistent connection handle (PCB) for the data channel
struct tcp_pcb *g_data_pcb = NULL;


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


// =================================================================
// --- Main Program Logic ---
// =================================================================
int main() {
    stdio_init_all();

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

    while (true) {
        // This function is essential to keep the network stack running
        cyw43_arch_poll();

        // Check if the flag has been set by the command receiver
        if (g_start_blink_flag) {
            printf("MAIN_LOOP: Flag detected! Starting LED blink session.\n");

            // Blink the onboard LED 5 times
            for (int i = 0; i < 5; i++) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                sleep_ms(200);
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
                sleep_ms(200);

                // Increment total count
                g_total_blink_count++;

                // Send the new total count in real-time if data connection is active
                if (g_data_pcb != NULL) {
                    char payload[16];
                    sprintf(payload, "%d\n", g_total_blink_count);
                    
                    // lwip functions must be wrapped in this block for thread safety
                    cyw43_arch_lwip_begin();
                    err_t err = tcp_write(g_data_pcb, payload, strlen(payload), TCP_WRITE_FLAG_COPY);
                    
                    if (err == ERR_OK) {
                        tcp_output(g_data_pcb); // Send the data immediately
                        cyw43_arch_lwip_end();
                        printf("-> Sent count: %d\n", g_total_blink_count);
                    } else {
                        cyw43_arch_lwip_end();
                        printf("-> Failed to send count. Error: %d\n", err);
                    }
                }
            }
            g_start_blink_flag = 0; // Reset flag after the blinking session is complete
            printf("MAIN_LOOP: Blinking session finished.\n");
        }
        
        sleep_ms(1); // Small delay to yield the CPU
    }
    return 0;
}