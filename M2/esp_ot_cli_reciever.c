/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * OpenThread UDP Receiver Example
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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "openthread/udp.h"
#include "openthread/ip6.h"

#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
#include "ot_led_strip.h"
#endif

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_cli_extension.h"
#endif

#define TAG "RX"
#define UDP_PORT 12345

static otUdpSocket s_udp_socket;

static void udp_receive(
    void *aContext,
    otMessage *aMessage,
    const otMessageInfo *aMessageInfo);

static void udp_receiver_init(void);

static void udp_receive(
    void *aContext,
    otMessage *aMessage,
    const otMessageInfo *aMessageInfo)
{
    (void)aContext;
    (void)aMessageInfo;

    char buf[128];

    int len = otMessageRead(
        aMessage,
        otMessageGetOffset(aMessage),
        buf,
        sizeof(buf) - 1);

    if (len < 0)
    {
        ESP_LOGE(TAG, "Failed to read message");
        return;
    }

    buf[len] = '\0';

    ESP_LOGI(TAG, "Received: %s", buf);
}

static void udp_receiver_init(void)
{
    otInstance *instance = esp_openthread_get_instance();

    esp_openthread_lock_acquire(portMAX_DELAY);

    otSockAddr listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));

    listen_addr.mPort = UDP_PORT;

    otError error;

    error = otUdpOpen(
        instance,
        &s_udp_socket,
        udp_receive,
        NULL);

    if (error != OT_ERROR_NONE)
    {
        ESP_LOGE(TAG, "otUdpOpen failed (%d)", error);
        esp_openthread_lock_release();
        return;
    }

    error = otUdpBind(
        instance,
        &s_udp_socket,
        &listen_addr,
        OT_NETIF_THREAD_HOST);

    if (error != OT_ERROR_NONE)
    {
        ESP_LOGE(TAG, "otUdpBind failed (%d)", error);
        esp_openthread_lock_release();
        return;
    }

    esp_openthread_lock_release();

    ESP_LOGI(TAG, "Listening on UDP port %d", UDP_PORT);
}

void app_main(void)
{
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

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
    esp_cli_custom_command_init();
#endif

#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
    ESP_ERROR_CHECK(
        esp_openthread_state_indicator_init(
            esp_openthread_get_instance()));
#endif

    udp_receiver_init();

#if CONFIG_OPENTHREAD_NETWORK_AUTO_START
    ot_network_auto_start();
#endif
}