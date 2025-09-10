#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <stdint.h> 
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR   -1
    typedef int SOCKET;
#endif

// =================================================================
// --- 設定 (Settings) ---
// =================================================================
#define HTTP_PORT    8080
#define DATA_PORT    12345
#define COMMAND_PORT 12346
#define BUFFER_SIZE  4096

// =================================================================
// --- グローバル変数 (Global Variables) ---
// =================================================================
SOCKET g_pico_command_socket = INVALID_SOCKET;
volatile int g_last_blink_count = 0;
pthread_mutex_t g_socket_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_count_mutex = PTHREAD_MUTEX_INITIALIZER;

// =================================================================
// --- ユーティリティ関数 (Utility Functions) ---
// =================================================================

/**
 * @brief エラーメッセージを表示してプログラムを終了する
 */
void die_with_error(const char *msg) {
    #ifdef _WIN32
        fprintf(stderr, "%s: %d\n", msg, WSAGetLastError());
    #else
        perror(msg);
    #endif
    exit(EXIT_FAILURE);
}

/**
 * @brief プラットフォーム互換のソケットクローズ関数
 */
void close_socket(SOCKET sock) {
    #ifdef _WIN32
        closesocket(sock);
    #else
        close(sock);
    #endif
}

/**
 * @brief 指定されたポートで待ち受けるTCPソケットを作成・設定する
 * @return 成功した場合はリスニングソケット、失敗した場合はプログラムが終了する
 */
SOCKET create_listening_socket(uint16_t port, int backlog) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        die_with_error("socket() failed");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        close_socket(sock);
        die_with_error("bind() failed");
    }

    if (listen(sock, backlog) == SOCKET_ERROR) {
        close_socket(sock);
        die_with_error("listen() failed");
    }
    
    printf("[*] Server listening on port %u\n", port);
    return sock;
}

// =================================================================
// --- HTTPサーバー用ヘルパー関数 (HTTP Server Helpers) ---
// =================================================================

const char* get_content_type(const char* path) {
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".css"))  return "text/css";
    if (strstr(path, ".js"))   return "application/javascript";
    return "text/plain";
}

void send_http_response(SOCKET sock, const char* status, const char* content_type, const char* body, size_t body_len) {
    char header[BUFFER_SIZE];
    sprintf(header, "%s\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n"
                    "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                    "Pragma: no-cache\r\n"                                  
                    "Expires: 0\r\n\r\n",                                    
                    status, content_type, body_len);
    send(sock, header, strlen(header), 0);
    if (body && body_len > 0) {
        send(sock, body, body_len, 0);
    }
}

void handle_api_start_command(SOCKET client_sock) {
    const char* message;
    pthread_mutex_lock(&g_socket_mutex);
    if (g_pico_command_socket != INVALID_SOCKET) {
        send(g_pico_command_socket, "START", 5, 0);
        printf("[HTTP] Sent 'START' command to PicoW.\n");
        message = "{\"message\":\"Command sent to PicoW.\"}";
    } else {
        message = "{\"message\":\"PicoW is not connected.\"}";
    }
    pthread_mutex_unlock(&g_socket_mutex);
    send_http_response(client_sock, "HTTP/1.1 200 OK", "application/json", message, strlen(message));
}

void handle_api_get_count(SOCKET client_sock) {
    pthread_mutex_lock(&g_count_mutex);
    int current_count = g_last_blink_count;
    pthread_mutex_unlock(&g_count_mutex);
    
    char json_body[64];
    sprintf(json_body, "{\"count\":%d}", current_count);
    send_http_response(client_sock, "HTTP/1.1 200 OK", "application/json", json_body, strlen(json_body));
}

void handle_static_file(SOCKET client_sock, const char* path) {
    char filepath[260];
    if (strcmp(path, "/") == 0) {
        strcpy(filepath, "index.html");
    } else {
        // Path Traversal攻撃を防ぐ簡易的な対策
        if (strstr(path, "..")) {
             send_http_response(client_sock, "HTTP/1.1 400 Bad Request", "text/plain", "Bad Request", 11);
             return;
        }
        strcpy(filepath, path + 1);
    }

    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        send_http_response(client_sock, "HTTP/1.1 404 Not Found", "text/plain", "Not Found", 9);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *file_content = (char*)malloc(filesize);
    if (file_content) {
        fread(file_content, 1, filesize, fp);
        const char* content_type = get_content_type(filepath);
        send_http_response(client_sock, "HTTP/1.1 200 OK", content_type, file_content, filesize);
        free(file_content);
    }
    fclose(fp);
}


// =================================================================
// --- スレッド関数 (Thread Functions) ---
// =================================================================

void* data_server_thread(void *arg) {
    SOCKET server_sock = create_listening_socket(DATA_PORT, 1);

    while(1) {
        SOCKET client_sock = accept(server_sock, NULL, NULL);
        if (client_sock == INVALID_SOCKET) continue;

        printf("[DATA] Persistent data connection established with PicoW.\n");
        
        char buffer[BUFFER_SIZE];
        int bytes_received;

        while ((bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
            buffer[bytes_received] = '\0';
            
            // \nで区切られた最後の数値を効率的に探す
            char* last_num_start = buffer;
            char* last_newline = strrchr(buffer, '\n');
            if(last_newline && *(last_newline + 1) != '\0') {
                last_num_start = last_newline + 1;
            }

            int received_count = atoi(last_num_start);
            
            pthread_mutex_lock(&g_count_mutex);
            g_last_blink_count = received_count;
            pthread_mutex_unlock(&g_count_mutex);

            printf("[DATA] Buffer received: '%s', Updated blink count to: %d\n", buffer, received_count);
        }

        printf("[DATA] Data connection with PicoW closed.\n");
        close_socket(client_sock);
    }
    close_socket(server_sock);
    return NULL;
}

void* command_server_thread(void *arg) {
    SOCKET server_sock = create_listening_socket(COMMAND_PORT, 1);
    
    // このサーバーは一度PicoWと接続したら、その接続を保持し続ける
    SOCKET client_sock = accept(server_sock, NULL, NULL);
    if(client_sock != INVALID_SOCKET) {
        pthread_mutex_lock(&g_socket_mutex);
        g_pico_command_socket = client_sock;
        pthread_mutex_unlock(&g_socket_mutex);
        printf("[COMMAND] PicoW connected.\n");
    }
    
    // リスニングソケットは不要になったので閉じる
    close_socket(server_sock);
    return NULL;
}

void* http_server_thread(void *arg) {
    SOCKET server_sock = create_listening_socket(HTTP_PORT, 5);
    
    while (1) {
        SOCKET client_sock = accept(server_sock, NULL, NULL);
        if (client_sock == INVALID_SOCKET) continue;

        char buffer[BUFFER_SIZE];
        int recv_size = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);

        if (recv_size > 0) {
            buffer[recv_size] = '\0';
            char path[256];
            sscanf(buffer, "GET %s HTTP/1.1", path);

            if (strcmp(path, "/start-command") == 0) {
                handle_api_start_command(client_sock);
            } else if (strcmp(path, "/get-count") == 0) {
                handle_api_get_count(client_sock);
            } else {
                handle_static_file(client_sock, path);
            }
        }
        
        close_socket(client_sock);
    }
    close_socket(server_sock);
    return NULL;
}

// =================================================================
// --- メイン関数 (Main) ---
// =================================================================

int main() {
    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            die_with_error("WSAStartup failed");
        }
    #endif

    pthread_t http_tid, data_tid, command_tid;
    
    printf("[MAIN] Starting all servers...\n");
    pthread_create(&data_tid, NULL, data_server_thread, NULL);
    pthread_create(&command_tid, NULL, command_server_thread, NULL);
    pthread_create(&http_tid, NULL, http_server_thread, NULL);
    
    pthread_join(data_tid, NULL);
    pthread_join(command_tid, NULL);
    pthread_join(http_tid, NULL);
    
    #ifdef _WIN32
        WSACleanup();
    #endif
    return 0;
}