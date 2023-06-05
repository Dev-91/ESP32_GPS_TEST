#include <stdio.h>

// file_sys_d9_lib
#include <spiffs_d9.h>
#include <sd_d9.h>

// serial_d9_lib
#include <uart_d9.h>

// timer_d9_lib
#include <gptimer_d9.h>

// utility_d9_lib
#include <utility_d9.h>

// network_d9_lib
#include <network_d9.h>
#include <ethernet_d9.h>
#include <wifi_d9.h>
#include <wifi_ap_d9.h>

// packet_d9_lib
#include <packet_d9.h>

/* Log Tag */
static const char *TAG = "MAIN";
static const char *TAG_ETH = "ETHERNET";
static const char *TAG_WIFI = "WIFI";
static const char *TAG_SD = "SD_CARD";
static const char *TAG_SPIFFS = "SPIFFS";
static const char *TAG_HTTP = "HTTP_CLIENT";
static const char *TAG_TIME = "TIME";
static const char *TAG_MEMORY = "MEMORY";
static const char *TAG_PACKET = "PACKET";

typedef struct
{
    char utc_time[7]; // UTC time
    char utc_date[7]; // UTC date
    float latitude;   // 위도'
    char n_s[2];
    float longitude; // 경도
    char e_w[2];     // E/W

    int position_fix;    // Position Fix
    int satellites_used; // Satellites used 현재 수신되는 위성 개수

    float hdop;     // Horizontal Dilution of Precision
    float altitude; // WGS-84 타원체에서 평균 해수면(MSL)을 기준으로한 고도
    char altitude_unit[2];
    float geoid_seperation; // MSL과 Geoid의 고도차. 마이너스 값이 나올 수 있음
    char seperation_unit[2];

    float speed_over_ground;  // km/h = knots * 1.852
    float course_over_ground; // degrees. 진행방향 진북을 중심으로 시계방향
} gps_t;

gps_t gps_data;

typedef struct
{
    int gps_interval;
} set_config_t;

static set_config_t set_cfg;

/* ESP32-S3 SD Setting */
#define USER_SPI_MOSI_GPIO 26
#define USER_SPI_MISO_GPIO 25
#define USER_SPI_SCLK_GPIO 27

#define USER_SD_SPI_CS_GPIO 32
#define USER_SD_CD_GPIO 35

#define USER_ETH_SPI_CS_GPIO 33
#define USER_ETH_SPI_INT_GPIO 4
#define USER_ETH_SPI_PHY_RST_GPIO -1
#define USER_ETH_SPI_PHY_ADDR_GPIO 1

/* UART Setting */
#define USER_RX_GPIO 16
#define USER_TX_GPIO 17
#define USER_RTS -1
#define USER_CTS -1
#define USER_BAUD_RATE 38400
#define UART_PORT_NUM 1
#define READ_TIME_OUT 3

#define USER_BLINK_GPIO 5

#define DEVICE_NUM "0"

static bool gps_receive_flag = false;
int network_ret = -1;

static void config_init()
{
    memset(&set_cfg, 0, sizeof(set_config_t));
}

static void default_config_read()
{
    set_cfg.gps_interval = CONFIG_DEFAULT_GPS_INTERVAL;
}

static void spiffs_setup()
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    spiffs_init(&conf);
}

static void spiffs_set_cfg_read()
{
    int read_buf_size = 100;
    char *read_buf = calloc(read_buf_size, sizeof(char));
    int ret = spiffs_file_read("/spiffs/set_cfg.csv", read_buf, read_buf_size);
    // esp_vfs_spiffs_unregister(conf.partition_label);
    if (ret == 0)
    {
        char split_buf[50][5] = {0};
        for (int i = 0;; i++)
        {
            int rc = split_data(read_buf, strlen(read_buf), ",", split_buf[i], i);
            if (rc == -1)
                break;
        }

        set_config_t set_cfg_buf;

        memset(&set_cfg_buf, 0, sizeof(set_config_t));

        set_cfg.gps_interval = atoi(split_buf[0]);
        // set_cfg.gps_interval = atoi(split_buf[0]);

        ESP_LOGI(TAG, "SPIFFS GPS Interval : %d", set_cfg.gps_interval);

        ESP_LOGI(TAG, "Use the spiffs set config");
    }
    else
    {
        char *write_data = calloc(100, sizeof(char));
        sprintf(write_data, "%d\n", CONFIG_DEFAULT_GPS_INTERVAL);

        spiffs_file_write("/spiffs/set_cfg.csv", "w", write_data);
        free(write_data);
    }
    free(read_buf);
}

static void spi_bus_init()
{
    spi_device_handle_t spi;

    spi_bus_config_t buscfg = {
        .miso_io_num = USER_SPI_MISO_GPIO,
        .mosi_io_num = USER_SPI_MOSI_GPIO,
        .sclk_io_num = USER_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_bus_initialize(HSPI_HOST, &buscfg, SPI_DMA_CH_AUTO); // S3: SPI2_HOST

    // SPI Bus Add device
    spi_device_interface_config_t sd_devcfg = {
        .clock_speed_hz = 2000000,
        .mode = 0,
        .spics_io_num = USER_SD_SPI_CS_GPIO,
        .queue_size = 1,
    };
    spi_bus_add_device(HSPI_HOST, &sd_devcfg, &spi);

    spi_device_interface_config_t eth_devcfg = {
        .clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .mode = 0,
        .spics_io_num = USER_ETH_SPI_CS_GPIO,
        .queue_size = 20,
    };
    spi_bus_add_device(HSPI_HOST, &eth_devcfg, &spi);
}

static int network_setup()
{
    // euthernet & wifi common init
    net_init();

#if CONFIG_USER_ETH_W5500_ENABLE == 0
    // ethernet init &connection
    ESP_LOGI(TAG, "CONFIG_USER_ETH_W5500_ENABLE: %d", CONFIG_USER_ETH_W5500_ENABLE);
    spi_eth_module_config_t ethcfg = {
        .spi_cs_gpio = USER_ETH_SPI_CS_GPIO,
        .int_gpio = USER_ETH_SPI_INT_GPIO,
        .phy_reset_gpio = USER_ETH_SPI_PHY_RST_GPIO,
        .phy_addr = USER_ETH_SPI_PHY_ADDR_GPIO,
    };

    ethernet_init(&ethcfg);
    network_ret = ethernet_connection_check();

    if (network_ret == 0)
        blink(USER_BLINK_GPIO, 100, 10);
    else
        blink(USER_BLINK_GPIO, 1000, 2);

#endif

    return -1;
}

static void sd_setup()
{
    spi_sd_module_config_t sdcfg = {
        .spi_cs_gpio = USER_SD_SPI_CS_GPIO,
        .sd_cd_gpio = USER_SD_CD_GPIO,
        .host = SDSPI_HOST_DEFAULT(),
    };
    sd_card_init(&sdcfg);

    if (sd_card_detect() != 0)
    {
        ESP_LOGW(TAG, "No SD card detected.");
        return;
    }

    if (sd_card_mount() != 0)
    {
        ESP_LOGW(TAG, "Failed to mount SD card.");
        return;
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

static float parse_lati_longi(float ll)
{
    int deg = ((int)ll) / 100;
    float min = ll - (deg * 100);
    ll = deg + min / 60.0f;
    return ll;
}

static int gps_chksum_check(const char *data)
{
    char *ori_chksum_str = strchr(data, '*') + 1;
    int ori_chksum = strtol(ori_chksum_str, NULL, 16);

    char *cal_data = strchr(data, '$') + 1;

    int n = 0;
    char cal_chksum = NULL;

    while (1)
    {
        if (cal_data[n] == '*')
            break;

        cal_chksum ^= (char)cal_data[n];
        n++;
    }

    if (cal_chksum == ori_chksum)
        return 0;

    return -1;
}

static void gps_rmc_decode(char *gps_token)
{
    memset(&gps_data, 0, sizeof(gps_t));

    int token_cnt = 0;

    int ret = gps_chksum_check(gps_token);

    if (ret != 0)
        return;

    char *token = strsep(&gps_token, ",");
    while (token != NULL)
    {
        // printf("token %d: %s\n", token_cnt, token);
        switch (token_cnt)
        {
        case 1:
            strncpy(gps_data.utc_time, token, 6);
            break;
        case 3:
            gps_data.latitude = parse_lati_longi(atof(token));
            break;
        case 4:
            strncpy(gps_data.n_s, token, 1);
            break;
        case 5:
            gps_data.longitude = parse_lati_longi(atof(token));
            break;
        case 6:
            strncpy(gps_data.e_w, token, 1);
            break;
        case 7:
            gps_data.speed_over_ground = atof(token);
            break;
        case 8:
            gps_data.course_over_ground = atof(token);
            break;
        case 9:
            strncpy(gps_data.utc_date, token, 6);
            break;
        }
        token = strsep(&gps_token, ",");
        token_cnt++;
    }
}

static void gps_gga_decode(char *gps_token)
{
    int token_cnt = 0;

    int ret = gps_chksum_check(gps_token);

    if (ret != 0)
        return;

    char *token = strsep(&gps_token, ",");
    while (token != NULL)
    {
        // printf("token %d: %s\n", token_cnt, token);
        switch (token_cnt)
        {
        case 6:
            gps_data.position_fix = atoi(token);
            break;
        case 7:
            gps_data.satellites_used = atof(token);
            break;
        case 8:
            gps_data.hdop = atof(token);
            break;
        case 9:
            gps_data.altitude = atof(token);
            break;
        case 10:
            strncpy(gps_data.altitude_unit, token, 1);
            break;
        case 11:
            gps_data.geoid_seperation = atof(token);
            break;
        case 12:
            strncpy(gps_data.seperation_unit, token, 1);
            break;
        }
        token = strsep(&gps_token, ",");
        token_cnt++;
    }

    if (gps_data.position_fix != 0)
    {
        blink(USER_BLINK_GPIO, 100, 1);
        gps_receive_flag = true;
    }
    else
    {
        gps_receive_flag = false;
    }
}

static void gps_data_sd_save()
{
    char *file_name = calloc(20, sizeof(char));
    char *sd_gps_data = calloc(200, sizeof(char));
    // SD Card Data Save
    sprintf(sd_gps_data, "%s,%s,%.5f,%.5f,%s,%.5f,%.5f",
            gps_data.utc_date, gps_data.utc_time, gps_data.latitude, gps_data.longitude,
            gps_data.e_w, gps_data.speed_over_ground, gps_data.course_over_ground);

    sprintf(file_name, "GPS_%s.csv", gps_data.utc_date);

    sd_card_write(file_name, sd_gps_data);
    free(file_name);
    free(sd_gps_data);
}

static void gps_task()
{
    // Configure a temporary buffer for the incoming data
    uint8_t *buffer = (uint8_t *)malloc(BUF_SIZE_4096);

    int gps_cnt = 0;

    while (1)
    {
        // Read data from the UART
        int len = uart_read_bytes(UART_PORT_NUM, buffer, (BUF_SIZE_4096 - 1), 200 / portTICK_PERIOD_MS);
        buffer[len] = '\0';
        // ESP_LOGI(TAG, "Received Data : \n%s", buffer);
        bool rmc_flag = false;
        bool gga_flag = false;

        char *rmc_token = calloc(120, sizeof(char));
        char *gga_token = calloc(120, sizeof(char));

        char *token = strtok((const char *)buffer, "\n");

        while (token != NULL)
        {
            // printf("token: %s\n", token);
            if (strstr(token, "RMC") != NULL)
            {
                strcpy(rmc_token, token);
                ESP_LOGI(TAG, "%s", rmc_token);
                rmc_flag = true;
            }
            else if (strstr(token, "GGA") != NULL)
            {
                strcpy(gga_token, token);
                ESP_LOGI(TAG, "%s", gga_token);
                gga_flag = true;
            }

            token = strtok(NULL, "\n");
        }

        if (rmc_flag && gga_flag)
        {
            gps_cnt++;
            if (gps_cnt >= set_cfg.gps_interval)
            {
                // $GNRMC,070000.00,A,3333.27906,N,12644.81225,E,0.049,,050623,,,A,V*1F
                gps_rmc_decode(rmc_token);
                // $GNGGA,070000.00,3333.27906,N,12644.81225,E,1,12,0.73,5.9,M,20.4,M,,*4B
                gps_gga_decode(gga_token);
                delay_ms(100);
                gps_data_sd_save();
                gps_cnt = 0;
            }
        }
        free(rmc_token);
        free(gga_token);
    }
    free(buffer);
    vTaskDelete(NULL);
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

        delay_ms(10);
        sd_card_write("err_log.txt", save_buf);
        delay_ms(1000);
        esp_restart(); // memory가 10000이하로 내려가면 디바이스 리셋
    }
}

static void timer_setup()
{
    gptimer_d9_init(D9_GPTIMER_0, true);
    gptimer_d9_set_callback_function(D9_GPTIMER_0, check_processing);
    gptimer_d9_start(D9_GPTIMER_0, true, 5000, "check_processing", 2048, 1, PRO_CPU_NUM);
}

static void gpio_setup()
{
    blink_gpio_init(USER_BLINK_GPIO);

    gpio_reset_pin(USER_SD_CD_GPIO);
    gpio_set_direction(USER_SD_CD_GPIO, GPIO_MODE_INPUT);
}

void app_main(void)
{
    ESP_LOGI(TAG, "app_main function start");
    delay_ms(get_random_int(1000, 2000));

    gpio_setup();
    blink(USER_BLINK_GPIO, 200, 5);

    config_init();
    default_config_read();

    spiffs_setup();
    spiffs_set_cfg_read();

    spi_bus_init();

    sd_setup();

    network_setup();

    uart_setup();

    timer_setup();

    delay_ms(2000);

    xTaskCreate(gps_task, "gps_task", 8192, NULL, 5, NULL);
}
