#include "SunSensor.h"
#include <math.h>


//NOTE: 太陽センサの初期化
void sunSensor_init() {

}

//NOTE: 太陽センサの読み取り
uint16_t sunSensor_read(int ch) {
    return read_mcp3008(ch);
}

//NOTE: MCP3008の読み取り
uint16_t read_mcp3008(int ch) {
    // チャンネル範囲外は0を返す
    if (ch > 7) {
        return 0;
    }

    uint8_t tx_buf[3];
    uint8_t rx_buf[3];

    // MCP3008へのコマンド
    tx_buf[0] = 0x01;                       // スタートビット
    tx_buf[1] = 0x80 | (ch << 4);      // SGL=1 + チャンネル指定
    tx_buf[2] = 0x00;                       // ダミー

    // CSをLOWにして通信開始
    gpio_put(PIN_CS, 0);

    // SPI通信で3バイト送受信
    spi_write_read_blocking(SPI_PORT, tx_buf, rx_buf, 3);

    // CSをHIGHにして通信終了
    gpio_put(PIN_CS, 1);

    // 受信データから10ビットのADC値を組み立てる
    uint16_t result = ((rx_buf[1] & 0x03) << 8) | rx_buf[2];

    return result;
}

//NOTE: 太陽センサより太陽の有無を検出
bool isSunValid() {
    int sum_sunSensor_data = 0;
    uint16_t sunSensor_data[4] = {0, 0, 0, 0};

    //NOTE: 光量の割合用の変数
    float per45 = 0.45;


    for(int i = 0; i < 4; i++){
        uint16_t sensor_value = sunSensor_read(i);
        sum_sunSensor_data += (int)sensor_value;
        sunSensor_data[i] = sensor_value;
    }

    //NOTE: カメラがついている方向の太陽センサはCH2,CH3
    float perCH2_sunSensor_data = (float)sunSensor_data[2] / sum_sunSensor_data;
    float perCH3_sunSensor_data = (float)sunSensor_data[3] / sum_sunSensor_data;

    //NOTE: CH2,CH3の光量の割合が45%以上の場合は太陽が検出されたと判断する
    if(perCH2_sunSensor_data > per45 && perCH3_sunSensor_data > per45){
        return true;
    }else{
        return false;
    }
}


/*
引数：ch0, ch1, ch2, ch3: 太陽センサのデータ
返り値：カメラを0°としたときの太陽の角度
4つのセンサー値から太陽の角度を計算する
*/
float calculate_sun_angle(uint16_t ch0, uint16_t ch1, uint16_t ch2, uint16_t ch3) {
        // 合成ベクトルを計算 (センサー配置に基づく)
        float vx = (float)(ch1 + ch2) - (float)(ch0 + ch3);
        float vy = (float)(ch0 + ch1) - (float)(ch2 + ch3);
    
        // 光が全くない場合は0°を返す
        if (vx == 0 && vy == 0) {
            return 0.0f;
        }
    
        // atan2fで反時計回りの角度[-180, 180]を計算
        float angle_rad = atan2f(vy, vx);
        float angle_deg = angle_rad * 180.0f / M_PI; // ラジアンから度に変換
    
        // 角度を[0, 360]の範囲に正規化
        if (angle_deg < 0) {
            angle_deg += 360.0f;
        }
    
        // カメラの位置(-90° or 270°)を0°とするためのオフセット補正
        angle_deg -= 270.0f;
        if (angle_deg < 0) {
            angle_deg += 360.0f;
        }
    
        // 反時計回りから時計回りに変換
        float clockwise_angle = 360.0f - angle_deg;
        if (clockwise_angle >= 360.0f) {
            clockwise_angle = 0.0f;
        }

        printf("calculate_sun_angle: %f\n", clockwise_angle);
    
        return clockwise_angle;
}
