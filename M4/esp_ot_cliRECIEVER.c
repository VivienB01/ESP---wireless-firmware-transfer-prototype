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
#define MAX_RETRIES 3



typedef enum
{
    RESP_ACK   = 0x81,
    RESP_NACK  = 0x82,
    RESP_BUSY  = 0x83,
    RESP_DONE  = 0x84,
    RESP_ERROR = 0x85
} response_code_t;

typedef struct
{
    uint32_t transfer_id;
    uint32_t packet_number;
    response_code_t response;
} fw_response_t;


typedef struct __attribute__((packed))
{
    uint32_t transfer_id;
    uint32_t packet_number;
    uint32_t total_packets;
    uint32_t total_size;
    uint16_t payload_length;
    uint8_t  payload[128];
    uint32_t crc32;
} fw_packet_t;

static uint32_t expected_packet = 1;
static uint32_t packets_received = 0;
static uint32_t packets_missing = 0;
static otUdpSocket s_udp_socket;



static void udp_receiver_init(void);

static uint32_t crc32_compute(const void *data, size_t len)
{
    const uint8_t *bytes = data;
    uint32_t crc = ~0U;

    while (len-- > 0)
    {
        crc ^= *bytes++;
        for (int i = 0; i < 8; i++)
        {
            crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1U));
        }
    }

    return ~crc;
}

static void send_response(
    otInstance *instance,
    const otMessageInfo *rx_info,
    uint32_t transfer_id,
    uint32_t packet_number,
    response_code_t response)
{
    fw_response_t resp;

    resp.transfer_id = transfer_id;
    resp.packet_number = packet_number;
    resp.response = response;

    otMessage *msg = otUdpNewMessage(instance, NULL);

    if (msg == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate response message");
        return;
    }

    otError error = otMessageAppend(
        msg,
        &resp,
        sizeof(resp));

    if (error != OT_ERROR_NONE)
    {
        otMessageFree(msg);
        ESP_LOGE(TAG, "Failed to append response");
        return;
    }

    otMessageInfo tx_info;
    memset(&tx_info, 0, sizeof(tx_info));

    tx_info.mPeerAddr = rx_info->mPeerAddr;
    tx_info.mPeerPort = rx_info->mPeerPort;

    error = otUdpSend(
        instance,
        &s_udp_socket,
        msg,
        &tx_info);

    if (error != OT_ERROR_NONE)
    {
        otMessageFree(msg);
        ESP_LOGE(TAG, "Failed to send response (%d)", error);
        return;
    }

    ESP_LOGI(TAG,
             "Sent response %02X for packet %lu",
             response,
             (unsigned long)packet_number);
}



static void udp_receive(
    
    void *aContext,
    otMessage *aMessage,
    const otMessageInfo *aMessageInfo)
{
    (void)aContext;
    (void)aMessageInfo;

    fw_packet_t packet;
    int message_len = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);

    if (message_len < (int)(offsetof(fw_packet_t, payload) + sizeof(packet.crc32)))
    {
        ESP_LOGE(TAG, "Invalid packet size %d (too small)", message_len);
        return;
    }



    int len = otMessageRead(
        aMessage,
        otMessageGetOffset(aMessage),
        &packet,
        sizeof(packet));

    if (len < 0)
    {
        ESP_LOGE(TAG, "otMessageRead failed (%d)", len);
        return;
    }

    // Reset counters on new transfer
    if (packet.packet_number == 1 && expected_packet == 1)
    {
        packets_received = 0;
        packets_missing = 0;
    }

    if (packet.payload_length > sizeof(packet.payload))
    {
        ESP_LOGE(TAG, "Invalid payload length %u", packet.payload_length);
        return;
    }

    size_t expected_len = offsetof(fw_packet_t, payload) + packet.payload_length + sizeof(packet.crc32);
    if (len != (int)expected_len && len != (int)sizeof(fw_packet_t))
    {
        ESP_LOGE(TAG, "Invalid packet size %d expected %u or %u", len, (unsigned)expected_len, (unsigned)sizeof(fw_packet_t));
        return;
    }

    uint32_t computed_crc = crc32_compute(&packet, len - sizeof(packet.crc32));
    if (computed_crc != packet.crc32)
    {
        ESP_LOGW(TAG,
                 "CRC error: packet %lu expected 0x%08lX got 0x%08lX",
                 (unsigned long)packet.packet_number,
                 (unsigned long)packet.crc32,
                 (unsigned long)computed_crc);

        send_response(
            esp_openthread_get_instance(),
            aMessageInfo,
            packet.transfer_id,
            packet.packet_number,
            RESP_NACK);

        return;
    }

    send_response(
        esp_openthread_get_instance(),
        aMessageInfo,
        packet.transfer_id,
        packet.packet_number,
        RESP_ACK);

    if (packet.packet_number == expected_packet)
    {
        ESP_LOGI(
            TAG,
            "Packet %lu/%lu OK",
            (unsigned long)packet.packet_number,
            (unsigned long)packet.total_packets);

        packets_received++;
        expected_packet++;
        
        if (packet.packet_number == packet.total_packets)
        {
            ESP_LOGI(TAG, "Transfer complete! Received: %lu, Missing: %lu",
                    (unsigned long)packets_received, (unsigned long)packets_missing);
        }
    }
    else if (packet.packet_number > expected_packet)
    {
        uint32_t missing_count = packet.packet_number - expected_packet;
        packets_missing += missing_count;
        
        ESP_LOGW(
            TAG,
            "Missing packet(s)! Expected %lu but received %lu (%lu missing)",
            (unsigned long)expected_packet,
            (unsigned long)packet.packet_number,
            (unsigned long)missing_count);

        packets_received++;
        expected_packet = packet.packet_number + 1;
        
        if (packet.packet_number == packet.total_packets)
        {
            ESP_LOGI(TAG, "Transfer complete! Received: %lu, Missing: %lu",
                    (unsigned long)packets_received, (unsigned long)packets_missing);
        }
    }
    else
    {
        ESP_LOGW(
            TAG,
            "Out of order! Expected %lu but received %lu",
            (unsigned long)expected_packet,
            (unsigned long)packet.packet_number);
    }

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

