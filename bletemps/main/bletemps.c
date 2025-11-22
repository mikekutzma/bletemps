#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
// #include "freertos/event_groups.h"
// #include "freertos/queue.h"
// #include "freertos/task.h"
// #include "host/ble_gap.h"
#include "esp_event_base.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
// #include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <bmp280.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define I2C_SDA_PIN 5
#define I2C_SCL_PIN 4

#define BLE_UUID_SERVICE                                                       \
  0x4f, 0xaf, 0xc2, 0x01, 0x1f, 0xb5, 0x45, 0x9e, 0x8f, 0xcc, 0xc5, 0xc9,      \
      0x00, 0x00, 0x91, 0x4b

#define BLE_UUID_CHAR_CLICK                                                    \
  0x4f, 0xaf, 0xc2, 0x01, 0x1f, 0xb5, 0x45, 0x9e, 0x8f, 0xcc, 0xc5, 0xc9,      \
      0x04, 0x00, 0x91, 0x4b

#define BLE_GAP_APPEARANCE_GENERIC_TAG 0x0200
#define BLE_GAP_URI_PREFIX_HTTPS 0x17
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

void ble_app_advertise(void);
void ble_gap_conn_foreach_handle(ble_gap_conn_foreach_handle_fn *cb, void *arg);
static int click_notify_callback(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);

static const char *TAG = "BLETemps";

static float pressure, temperature, humidity;
typedef enum {
  NO_ERROR,
  NO_INITIAL_READING,
  READING_ERROR,
} bmp_error_t;

static bmp_error_t bmp_error = NO_INITIAL_READING;
uint8_t ble_addr_type;

static uint16_t notify_click_handle;

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(BLE_UUID_SERVICE),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                // {.uuid = BLE_UUID16_DECLARE(0xFEF4), // Define UUID for
                // reading
                //  .flags = BLE_GATT_CHR_F_READ,
                //  .access_cb = device_read},
                // {
                //     .uuid = BLE_UUID128_DECLARE(BLE_UUID_CHAR_WIFI),
                //     .flags = BLE_GATT_CHR_F_WRITE,
                //     .access_cb = write_cb,
                // },
                // {
                //     .uuid = BLE_UUID128_DECLARE(BLE_UUID_CHAR_NTP),
                //     .flags = BLE_GATT_CHR_F_WRITE,
                //     .access_cb = write_cb_log_data,
                // },
                // {
                //     .uuid = BLE_UUID128_DECLARE(BLE_UUID_CHAR_SERVER_URL),
                //     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                //     .access_cb = server_url_callback,
                // },
                {
                    .uuid = BLE_UUID128_DECLARE(BLE_UUID_CHAR_CLICK),
                    .access_cb = click_notify_callback,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                             BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &notify_click_handle,
                    .descriptors =
                        (struct ble_gatt_dsc_def[]){
                            // {
                            //     .uuid = BLE_UUID16_DECLARE(0x2902),
                            //     .att_flags = BLE_ATT_F_READ |
                            //     BLE_ATT_F_WRITE,
                            // },
                            {0}, // end of descriptors
                        },
                },
                {0},
            },
    },
    {0},
};

// BLE event handling
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  // Advertise if connected
  case BLE_GAP_EVENT_CONNECT:
    ESP_LOGI("GAP", "BLE GAP EVENT CONNECT %s",
             event->connect.status == 0 ? "OK!" : "FAILED!");
    if (event->connect.status != 0) {
      ble_app_advertise();
    }
    break;
  // Advertise again after completion of the event
  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI("GAP", "BLE GAP EVENT DISCONNECTED");
    ble_app_advertise();
    break;
  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI("GAP", "BLE GAP EVENT");
    ble_app_advertise();
    break;
  default:
    break;
  }
  return 0;
}

// Define the BLE connection
void ble_app_advertise(void) {
  // GAP - device name definition
  struct ble_hs_adv_fields fields;
  const char *device_name;
  memset(&fields, 0, sizeof(fields));
  device_name = ble_svc_gap_device_name(); // Read the BLE device name
  fields.name = (uint8_t *)device_name;
  fields.name_len = strlen(device_name);
  fields.name_is_complete = 1;
  fields.le_role = BLE_GAP_LE_ROLE_PERIPHERAL;
  fields.le_role_is_present = 1;
  fields.appearance = BLE_GAP_APPEARANCE_GENERIC_TAG;
  fields.appearance_is_present = 1;
  ble_gap_adv_set_fields(&fields);

  // GAP - device connectivity definition
  struct ble_gap_adv_params adv_params;
  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode =
      BLE_GAP_CONN_MODE_UND; // connectable or non-connectable
  adv_params.disc_mode =
      BLE_GAP_DISC_MODE_GEN; // discoverable or non-discoverable
  ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                    ble_gap_event, NULL);
}

// The application
void ble_app_on_sync(void) {
  ble_hs_id_infer_auto(
      0, &ble_addr_type); // Determines the best address type automatically
  ble_app_advertise();    // Define the BLE connection
}

// The infinite task
void host_task(void *param) {
  nimble_port_run(); // This function will return only when nimble_port_stop()
                     // is executed
}

// int64_t xx_time_get_time() {
//   struct timeval tv;
//   gettimeofday(&tv, NULL);
//   return (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
// }

static int click_notify_callback(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
  return 0; // nothing to do here for pure notifications
}

int send_click_ble_callback(uint16_t conn_handle, void *data) {
  ESP_LOGI(TAG, "Sending click to %d", conn_handle);
  char const *json_data = (char const *)data;
  struct os_mbuf *om;
  om = ble_hs_mbuf_from_flat(json_data, strlen(json_data));
  if (!om) {
    ESP_LOGE(TAG, "No data to send");
    return BLE_HS_ENOMEM;
  }
  ble_gatts_notify_custom(conn_handle, notify_click_handle, om);
  return 0;
}

void send_reading_ble() {
  ESP_LOGI(TAG, "Starting BLE click notification");
  // int64_t timestamp = xx_time_get_time();

  cJSON *root = cJSON_CreateObject();
  if (bmp_error == NO_INITIAL_READING || bmp_error == READING_ERROR) {
    cJSON_AddStringToObject(root, "error",
                            (bmp_error == NO_INITIAL_READING)
                                ? "Waiting for first reading.."
                                : "BMP reading error");
    cJSON_AddNullToObject(root, "temperature");
    cJSON_AddNullToObject(root, "pressure");
    cJSON_AddNullToObject(root, "humidity");
  } else {
    cJSON_AddNullToObject(root, "error");
    cJSON_AddNumberToObject(root, "temperature", temperature);
    cJSON_AddNumberToObject(root, "pressure", pressure);
    cJSON_AddNumberToObject(root, "humidity", humidity);
  }

  // cJSON_AddNumberToObject(root, "timestamp", timestamp);
  char *json_data = cJSON_PrintUnformatted(root);

  ble_gap_conn_foreach_handle(send_click_ble_callback, (void *)json_data);
}

void update_readings(void *pvParameters) {
  bmp280_params_t params;
  bmp280_init_default_params(&params);
  bmp280_t dev;
  memset(&dev, 0, sizeof(bmp280_t));

  ESP_ERROR_CHECK(bmp280_init_desc(&dev, BMP280_I2C_ADDRESS_0, 0, I2C_SDA_PIN,
                                   I2C_SCL_PIN));
  ESP_ERROR_CHECK(bmp280_init(&dev, &params));

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (bmp280_read_float(&dev, &temperature, &pressure, &humidity) != ESP_OK) {
      bmp_error = READING_ERROR;
      printf("Temperature/pressure reading failed\n");
      continue;
    }
    bmp_error = NO_ERROR;
    send_reading_ble();

    printf("Pressure: %.2f Pa, Temperature: %.2f C, Humidity: %.2f\n", pressure,
           temperature, humidity);
  }
}

void app_main() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    /* NVS partition was truncated
     * and needs to be erased */
    ESP_ERROR_CHECK(nvs_flash_erase());

    /* Retry nvs_flash_init */
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  ESP_ERROR_CHECK(esp_netif_init());

  esp_event_loop_create_default();
  ESP_ERROR_CHECK(i2cdev_init());
  xTaskCreate(update_readings, "gather_readings", configMINIMAL_STACK_SIZE * 8,
              NULL, 5, NULL);
  nimble_port_init(); // Initialize the host stack
  ble_svc_gap_device_name_set(
      "StationTemps"); // Initialize NimBLE configuration - server name
  ble_svc_gap_init();  // Initialize NimBLE configuration - gap service
  ble_svc_gatt_init(); // Initialize NimBLE configuration - gatt service
  ble_gatts_count_cfg(
      gatt_svcs); // Initialize NimBLE configuration - config gatt services
  ble_gatts_add_svcs(
      gatt_svcs); // Initialize NimBLE configuration - queues gatt services.
  ble_gatts_find_chr(BLE_UUID128_DECLARE(BLE_UUID_SERVICE),
                     BLE_UUID128_DECLARE(BLE_UUID_CHAR_CLICK), NULL,
                     &notify_click_handle);
  ble_hs_cfg.sync_cb = ble_app_on_sync; // Initialize application
  nimble_port_freertos_init(host_task); // Run the thread
}
