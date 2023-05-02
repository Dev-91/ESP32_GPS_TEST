#include <stdio.h>

// file_sys_d9_lib
#include <sd_d9.h>

// serial_d9_lib
#include <uart_d9.h>

// timer_d9_lib
#include <gptimer_d9.h>

// utility_d9_lib
#include <utility_d9.h>

typedef struct
{
    char *utc_time;           // UTC time
    float latitude;           // 위도
    float longitude;          // 경도
    char *e_w;                // E/W
    float speed_over_ground;  // km/h = knots * 1.852
    float course_over_ground; // degrees. 진행방향 진북을 중심으로 시계방향
    char *utc_date;           // UTC date
} gps_t;

gps_t gps_data;

/* ESP32-S3 SD Setting */
#define USER_SPI_MOSI_GPIO 11
#define USER_SPI_MISO_GPIO 13
#define USER_SPI_SCLK_GPIO 12

#define USER_SD_SPI_CS_GPIO 10

/* UART Setting */
#define USER_RX_GPIO 18
#define USER_TX_GPIO 17
#define USER_RTS -1
#define USER_CTS -1
#define USER_BAUD_RATE 38400
#define UART_PORT_NUM 1
#define READ_TIME_OUT 3

#define USER_LED_GPIO 42

static const char *TAG = "GPS_TEST";

static void spi_bus_init()
{
    spi_bus_config_t buscfg = {
        .miso_io_num = USER_SPI_MISO_GPIO,
        .mosi_io_num = USER_SPI_MOSI_GPIO,
        .sclk_io_num = USER_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
}

static void sd_setup()
{
    spi_sd_module_config_t sdcfg = {
        .spi_cs_gpio = USER_SD_SPI_CS_GPIO,
        .host = SDSPI_HOST_DEFAULT(),
    };
    sd_card_init(&sdcfg);

    sd_card_mount();
}

static void sd_test()
{
    if (sd_card_mount() == 0)
    {
        if (file_exists("Hello.txt") == 0)
        {
            sd_card_delete("Hello.txt");
        }
        if (file_exists("Hello.txt") == -1)
        {
            sd_card_write("Hello.txt", "Hello SD :D\n");
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        size_t read_buf_size = 100 * sizeof(char);
        char *read_buf = (char *)calloc(read_buf_size, sizeof(char));
        ESP_LOGI(TAG, "sizeof(read_buf) : %d\n", read_buf_size);

        ESP_LOGI(TAG, "sd_mount_check : %d\n", sd_mount_check());

        sd_card_read("Hello.txt", read_buf, read_buf_size);

        ESP_LOGI(TAG, "%s", read_buf);
        free(read_buf);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        sd_card_unmount();
        ESP_LOGI(TAG, "sd_mount_check : %d\n", sd_mount_check());

        char *read_buf2 = (char *)calloc(read_buf_size, sizeof(char));
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        sd_card_read("Hello.txt", read_buf2, read_buf_size);

        ESP_LOGI(TAG, "%s", read_buf2);

        if (sd_card_mount() == 0)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            sd_card_read("Hello.txt", read_buf2, read_buf_size);

            ESP_LOGI(TAG, "sd_mount_check : %d\n", sd_mount_check());

            ESP_LOGI(TAG, "%s", read_buf2);
            free(read_buf2);
        }
    }
}

static void uart_setup()
{
    uart_gpio_config_t uart_gpio_cfg = {
        .rx_gpio = USER_RX_GPIO,
        .tx_gpio = USER_TX_GPIO,
        .rts_gpio = USER_RTS,
        .cts_gpio = USER_CTS,
    };

    uart_config_t uart_cfg = {
        .baud_rate = USER_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    uart_init(UART_PORT_NUM, &uart_gpio_cfg, &uart_cfg, BUF_SIZE_4096, READ_TIME_OUT);
}

static float parse_lat_long(float ll)
{
    int deg = ((int)ll) / 100;
    float min = ll - (deg * 100);
    ll = deg + min / 60.0f;
    return ll;
}

static void gps_decode(char *gps_token)
{
    memset(&gps_data, 0, sizeof(gps_t));

    int token_cnt = 0;

    char *token = strsep(&gps_token, ",");
    while (token != NULL)
    {
        // printf("token %d: %s\n", token_cnt, token);
        switch (token_cnt)
        {
        case 1:
            gps_data.utc_time = token;
            break;
        case 3:
            gps_data.latitude = parse_lat_long(atof(token));
            break;
        case 5:
            gps_data.longitude = parse_lat_long(atof(token));
            break;
        case 6:
            gps_data.e_w = token;
            break;
        case 7:
            gps_data.speed_over_ground = atof(token);
            break;
        case 8:
            gps_data.course_over_ground = atof(token);
            break;
        case 9:
            gps_data.utc_date = token;
            break;
        }
        token = strsep(&gps_token, ",");
        token_cnt++;
    }

    if (gps_data.latitude != 0 && gps_data.longitude != 0)
        blink(USER_LED_GPIO, 100, 1);

    ESP_LOGI(TAG, "UTC TIME: %s", gps_data.utc_time);
    ESP_LOGI(TAG, "Latitude: %.5f", gps_data.latitude);
    ESP_LOGI(TAG, "Longitude: %.5f", gps_data.longitude);
    ESP_LOGI(TAG, "E/W: %s", gps_data.e_w);
    ESP_LOGI(TAG, "Speed over ground: %.5f", gps_data.speed_over_ground);
    ESP_LOGI(TAG, "Course over ground: %.5f", gps_data.course_over_ground);
    ESP_LOGI(TAG, "UTC Date: %s", gps_data.utc_date);
    printf("\n");

    char *sd_gps_data = calloc(200, sizeof(char));
    sprintf(sd_gps_data, "%s,%s,%.5f,%.5f,%s,%.5f,%.5f",
            gps_data.utc_date, gps_data.utc_time, gps_data.latitude, gps_data.longitude,
            gps_data.e_w, gps_data.speed_over_ground, gps_data.course_over_ground);

    sd_card_write("gps_data.csv", sd_gps_data);
    free(sd_gps_data);
}
// $GNRMC,082943.00,A,3328.56838,N,12632.88343,E,0.050,,280423,,,A,V*10
static void gps_task()
{
    // Configure a temporary buffer for the incoming data
    uint8_t *buffer = (uint8_t *)malloc(BUF_SIZE_4096);

    while (1)
    {
        // Read data from the UART
        int len = uart_read_bytes(UART_PORT_NUM, buffer, (BUF_SIZE_4096 - 1), 200 / portTICK_PERIOD_MS);
        buffer[len] = '\0';
        // ESP_LOGI(TAG, "Received Data : \n%s", buffer);

        char *token = strtok((const char *)buffer, "\n");
        while (token != NULL)
        {
            if (strstr(token, "RMC") != NULL)
            {
                gps_decode(token);
                token = strtok(NULL, "\n");
                break;
                // continue;
            }
            // printf("%s\n", token);
            token = strtok(NULL, "\n");
        }
    }
    free(buffer);
    vTaskDelay(NULL);
}

static void check_processing()
{
    int heap_size = esp_get_free_heap_size();
    // ESP_LOGW(TAG_MEMORY, "Current heap memory size : %d\n", heap_size);

    if (heap_size <= 10000)
    {
        char save_buf[100] = {0};
        sprintf(save_buf, "memory leak rebooting heap memory size : %d", heap_size);
        ESP_LOGE(TAG, "%s\n", save_buf);

        vTaskDelay(10 / portTICK_PERIOD_MS);
        sd_card_write("err_log.txt", save_buf);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart(); // memory가 10000이하로 내려가면 디바이스 리셋
    }
}

static void timer_setup()
{
    gptimer_d9_init(D9_GPTIMER_0, true);
    gptimer_d9_set_callback_function(D9_GPTIMER_0, check_processing);
    gptimer_d9_start(D9_GPTIMER_0, true, 5000, "check_processing", 2048, 1, PRO_CPU_NUM);
}

void app_main(void)
{
    blink_gpio_init(USER_LED_GPIO);
    blink(USER_LED_GPIO, 200, 5);

    spi_bus_init();
    sd_setup();

    uart_setup();

    timer_setup();

    xTaskCreate(gps_task, "gps_task", 8192, NULL, 10, NULL);
}
