#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"

/* NimBLE / Bluetooth headers */
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

static const char* TAG = "[NIMBLE_TEST]";
static const char* DEVICE_NAME = "ESP32S3_NimBLE";

static void ble_app_advertise(void);

static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "BLE host synchronized with controller.");
    ble_app_advertise();
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset; reason = %d", reason);
}

static void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t*) DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error setting advertisement fields; rc = %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error starting advertisement; rc = %d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE Advertising started successfully as '%s'!", DEVICE_NAME);
}

static void host_task(void* param)
{
    ESP_LOGI(TAG, "NimBLE host task running...");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 NimBLE Stack Test ===");

    /* 1. Initialize NVS (Non-Volatile Storage) - Required for Bluetooth */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "1. NVS Flash initialized.");

    /* 2. Initialize NimBLE Port (Host & Controller) */
    ESP_LOGI(TAG, "2. Initializing NimBLE host stack via nimble_port_init()...");
    ESP_ERROR_CHECK(nimble_port_init());

    /* 3. Configure Host callbacks */
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    /* 4. Set GAP Device Name */
    ble_svc_gap_device_name_set(DEVICE_NAME);

    /* 5. Start NimBLE Host Task in FreeRTOS */
    ESP_LOGI(TAG, "3. Starting NimBLE host FreeRTOS task...");
    nimble_port_freertos_init(host_task);

    ESP_LOGI(TAG, "=== NimBLE Initialization Complete ===");
}
