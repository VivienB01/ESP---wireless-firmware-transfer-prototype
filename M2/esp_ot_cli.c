/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * OpenThread Command Line Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "nvs_flash.h"
#include "ot_examples_common.h"
#include "openthread/udp.h"
#include "openthread/ip6.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
#include "ot_led_strip.h"
#endif

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_cli_extension.h"
#endif // CONFIG_OPENTHREAD_CLI_ESP_EXTENSION

#define TAG "ot_esp_cli"

#define UDP_PORT 12345

static void sender_task(void *arg);
static void send_message(otInstance *instance);

static void sender_task(void *arg)
{
    otInstance *instance = esp_openthread_get_instance();

    vTaskDelay(pdMS_TO_TICKS(10000));

    esp_openthread_lock_acquire(portMAX_DELAY);

    if (otThreadGetDeviceRole(instance) >= OT_DEVICE_ROLE_CHILD)
    {
        send_message(instance);
    }

    esp_openthread_lock_release();


    vTaskDelete(NULL);   // kill the task after sending
}

static void send_message(otInstance *instance)
{
    otUdpSocket socket;
    otMessageInfo info;

    memset(&socket, 0, sizeof(socket));
    memset(&info, 0, sizeof(info));

    otIp6AddressFromString(
        "fd24:76cc:c5e2:a6e6:0:ff:fe00:5c00",
        &info.mPeerAddr);

    info.mPeerPort = UDP_PORT;

    otError error = otUdpOpen(instance, &socket, NULL, NULL);

    if (error != OT_ERROR_NONE)
    {
        ESP_LOGE(TAG, "UDP open failed");
        return;
    }

    otMessage *msg = otUdpNewMessage(instance, NULL);

    if (msg == NULL)
    {
        ESP_LOGE(TAG, "Message allocation failed");
        return;
    }

    const char *text = "HELLO_FROM_C6";

    otMessageAppend(msg, text, strlen(text));

    error = otUdpSend(instance, &socket, msg, &info);

    if (error == OT_ERROR_NONE)
    {
        ESP_LOGI(TAG, "Sent: %s", text);
    }
    else
    {
        ESP_LOGE(TAG, "Send failed (%d)", error);
    }

    otUdpClose(instance, &socket);
}

void app_main(void)
{
    // Used eventfds:
    // * netif
    // * ot task queue
    // * radio driver
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

#if CONFIG_OPENTHREAD_CLI
    ot_console_start();
    ot_register_external_commands();
#endif

    static esp_openthread_config_t config = {
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };

    ESP_ERROR_CHECK(esp_openthread_start(&config));

    xTaskCreate(
    sender_task,
    "sender_task",
    4096,
    NULL,
    5,
    NULL
);

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
    esp_cli_custom_command_init();
#endif
#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
    ESP_ERROR_CHECK(esp_openthread_state_indicator_init(esp_openthread_get_instance()));
#endif
#if CONFIG_OPENTHREAD_NETWORK_AUTO_START
    ot_network_auto_start();
#endif
}
