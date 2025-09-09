// 標準入出力（printfなど）
#include <stdio.h>
// 文字列操作（memcpy, strstrなど）
#include <string.h> 
// Picoの標準ライブラリ
#include "pico/stdlib.h"
// Pico WのWi-Fiチップ(CYW43)制御用アーキテクチャ層
#include "pico/cyw43_arch.h"

// lwIP (軽量TCP/IPスタック) のためのヘッダー
#include "lwip/pbuf.h" // パケットバッファ管理
#include "lwip/tcp.h"  // TCP制御

// HTTP (Web) サーバーが待ち受けるポート番号
#define TCP_PORT 80

// 接続するWi-FiアクセスポイントのSSID (ID)
char ssid[] = "SPWH_L12_af31f5";
// Wi-Fiのパスワード
char pass[] = "1a73892d66094";

/**
 * [コールバック関数 1] クライアントからデータを受信した時にlwIPによって呼び出される
 * HTTPリクエストを解析し、レスポンスを返す役割を持つ
 */
static err_t tcp_server_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {

    // HTTPリクエストの先頭部分を格納するための一時バッファ
    char request_buffer[128];
    size_t request_len = 0;

    // p (pbuf) が NULL でない場合、クライアントからデータが届いたことを意味する
    if (p != NULL) {
        
        // --- リクエスト内容の解析 ---
        // pbuf内の受信データをローカルバッファにコピーする
        // (C言語の文字列関数(strstr)で扱えるよう、末尾にヌル文字'\0'を追加するため)
        if (p->len < sizeof(request_buffer) - 1) {
            memcpy(request_buffer, p->payload, p->len);
            request_buffer[p->len] = '\0'; // 文字列の終端
        } else {
            // バッファサイズを超えるリクエストは、バッファ分だけコピーして強制的に終端する
            memcpy(request_buffer, p->payload, sizeof(request_buffer) - 1);
            request_buffer[sizeof(request_buffer) - 1] = '\0';
        }

        // 受信したリクエスト（の先頭）をデバッグ用にシリアル出力
        printf("--- HTTP Request Received ---\n%s\n-----------------------------\n", request_buffer);

        // ★リクエスト解析の核心部★
        // 受信した文字列(request_buffer)内に "GET /a" という部分文字列が含まれているか検索
        if (strstr(request_buffer, "GET /a")) {
            // 含まれていた場合（＝ブラウザのボタンが押された場合）
            // シリアルモニタに 'a' を出力する
            printf("a\n"); 
        
        } else {
            // 含まれていない場合（通常の "/" アクセスや "favicon.ico" など）
            printf("Root access or unknown request.\n");
        }
        
        // --- レスポンスの送信準備 ---
        
        // 受信したことをTCPスタックに通知（TCPウィンドウ制御のため）
        tcp_recved(tpcb, p->tot_len);
        // 受信に使用したpbuf（メモリ）を解放する
        pbuf_free(p);

        // ブラウザに返すHTTPレスポンス（ヘッダー＋HTML本体）を定義
        const char http_response[] = 
            "HTTP/1.1 200 OK\r\n"                 // ステータスコード: 成功
            "Content-Type: text/html\r\n"          // コンテンツタイプ: HTML
            "Connection: close\r\n"                // 通信後、接続を切断する
            "\r\n"                                 // ヘッダーとボディの分離
            "<html><body>"
            "<h1>Pico W Controller</h1>"
            "<p>Click the button to send signal 'a' to the serial monitor.</p>"
            "<br>"
            // このボタンが押されると、ブラウザは "/a" へ再度アクセスする
            "<a href=\"/a\"><button style=\"font-size: 20px; padding: 10px;\">Send 'a'</button></a>"
            "</body></html>";

        // ネットワークインターフェースの状態をチェック
        cyw43_arch_lwip_check();
        // TCP書き込み（レスポンスを送信バッファにコピー）
        // (sizeof-1 はC言語の文字列末尾のヌル文字を除外するため)
        tcp_write(tpcb, http_response, sizeof(http_response) - 1, TCP_WRITE_FLAG_COPY); 
        
        // レスポンス送信後、サーバー側から接続を閉じる ("Connection: close" のため)
        tcp_close(tpcb);

    } else {
        // p が NULL の場合、クライアント（ブラウザ）が接続を切断したことを意味する
        printf("Client closed connection.\n");
        tcp_close(tpcb); // 接続リソースを解放する
    }
    
    return ERR_OK; // 正常終了
}

/**
 * [コールバック関数 2] 新しいクライアントが接続してきた時（TCP Listen後）にlwIPによって呼び出される
 */
static err_t tcp_server_accept_callback(void *arg, struct tcp_pcb *new_pcb, err_t err) {
    
    // 接続に問題がある場合はエラーを返す
    if (new_pcb == NULL) {
        printf("Failed to accept connection\n");
        return ERR_VAL;
    }
    printf("Client connected from browser.\n");

    // この新しい接続(new_pcb)で「データを受信」したら、
    // 先ほど定義した 'tcp_server_recv_callback' 関数を呼び出すようlwIPに登録する
    tcp_recv(new_pcb, tcp_server_recv_callback);
    
    return ERR_OK; // 受諾成功
}

/**
 * [セットアップ関数] TCPサーバー（Webサーバー）を初期化し、待ち受け状態にする
 */
static void setup_tcp_server(void) {
    printf("Setting up TCP Server...\n");
    
    // 1. 新しいTCP「PCB (Protocol Control Block)」を作成 (IPタイプはANY=IPv4/v6問わず)
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        printf("Failed to create TCP PCB\n");
        return;
    }

    // 2. サーバーを指定ポートに「バインド (紐付け)」する
    // (IP_ADDR_ANY = どのネットワークインターフェース(IPアドレス)に来た通信も受け付ける)
    err_t err = tcp_bind(pcb, IP_ADDR_ANY, TCP_PORT);
    if (err != ERR_OK) {
        printf("Failed to bind to port %d\n", TCP_PORT);
        return;
    }

    // 3. 「リッスン (待ち受け)」状態に移行する
    pcb = tcp_listen(pcb);
    if (!pcb) {
        printf("Failed to listen\n");
        return;
    }

    printf("TCP Server is Listening on port %d\n", TCP_PORT);

    // 4. 新しい接続要求(accept)が来たら、
    // 'tcp_server_accept_callback' 関数を呼び出すようlwIPに登録する
    tcp_accept(pcb, tcp_server_accept_callback);
}


/**
 * --- メイン関数 (プログラムのエントリポイント) ---
 */
int main() {
    // シリアルモニタ(USB経由)を使えるように初期化
    stdio_init_all();

    // 1. Wi-Fiチップ (CYW43) を初期化
    if (cyw43_arch_init()) {
        printf("FAILED to initialise Wi-Fi chip\n");
        return 1; // 異常終了
    }
    printf("Initialised.\n");

    // 2. Wi-Fiの「ステーションモード (STAモード)」を有効化 (アクセスポイントに接続する子機モード)
    cyw43_arch_enable_sta_mode();
    printf("Connecting to Wi-Fi: %s ...\n", ssid);

    // 3. Wi-Fiへの接続を試行 (タイムアウト30秒)
    if (cyw43_arch_wifi_connect_timeout_ms(ssid, pass, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        // 接続失敗
        printf("FAILED to connect Wifi.\n");
    } else {
        // 接続成功
        printf("Connected successfully Wifi.\n");
        // DHCPで取得した自分のIPアドレスをシリアルモニタに表示 (ブラウザアクセスに必要)
        printf("My IP address is: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list))); 

        // TCPサーバー（Webサーバー）をセットアップして起動する
        setup_tcp_server();
    }

    // 4. メインの無限ループ
    // ネットワーク処理はコールバック関数で非同期に実行されるため、
    // mainループはネットワークスタックが処理を行うための「ポーリング」を続ける。
    while (true) {
        // CYW43アーキテクチャ層とlwIPスタックが保留中の処理を実行する機会を与える
        // (これがないとネットワークが機能しない)
        cyw43_arch_poll();
        // CPUを少し休ませる（他のタスクに処理を譲る）
        sleep_ms(1);
    }
}