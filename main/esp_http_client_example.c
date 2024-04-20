#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "protocol_examples_utils.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#include "esp_http_client.h"
#include "freertos/queue.h"
#include "lora.h"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

// LoRa radio operating frequency in europe
#define LORA_FREQ 433e6

// Log tag
static const char *TAG = "HTTP_CLIENT";

// The google trust chain root certificate
// for https

extern const char gtsr1_root_cert_pem_start[] asm("_binary_gtsr1_root_cert_pem_start");
extern const char gtsr1_root_cert_pem_end[] asm("_binary_gtsr1_root_cert_pem_end");

// The lora packet queue
static QueueHandle_t s_lora_queue_handler;

// Lora packet definition struct
typedef struct
{
    uint8_t *payload;
    size_t payload_size;
} lora_packet_t;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        printf("EVENT ON ERROR\n");

        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        printf("EVENT ON CONNECTED\n");

        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        printf("EVENT ON HEADER SENT\n");

        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        printf("EVENT ON HEADER\n");
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        printf("EVENT ON DATA\n");
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        /*
         *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
         *  However, event handler can also be used in case chunked encoding is used.
         */
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            printf("%.*s", evt->data_len, (char *)evt->data);
            // If user_data buffer is configured, copy the response into the buffer
            int copy_len = 0;
            if (evt->user_data)
            {
                copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                if (copy_len)
                {
                    memcpy(evt->user_data + output_len, evt->data, copy_len);
                }
            }
            else
            {
                const int buffer_len = esp_http_client_get_content_length(evt->client);
                if (output_buffer == NULL)
                {
                    output_buffer = (char *)malloc(buffer_len);
                    output_len = 0;
                    if (output_buffer == NULL)
                    {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        return ESP_FAIL;
                    }
                }
                copy_len = MIN(evt->data_len, (buffer_len - output_len));
                if (copy_len)
                {
                    memcpy(output_buffer + output_len, evt->data, copy_len);
                }
            }
            output_len += copy_len;
            printf("%.*s", evt->data_len, (char *)evt->data);
        }

        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL)
        {
            // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
            // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
        if (err != 0)
        {
            ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
        }
        if (output_buffer != NULL)
        {
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        esp_http_client_set_header(evt->client, "From", "user@example.com");
        esp_http_client_set_header(evt->client, "Accept", "text/html");
        esp_http_client_set_redirection(evt->client);
        break;
    }
    return ESP_OK;
}

// Client config
static esp_http_client_config_t s_config = {
    .url = "https://api.one-account.io/v1/auth/login",
    .event_handler = _http_event_handler,
    .cert_pem = gtsr1_root_cert_pem_start,
    .is_async = true,
    .timeout_ms = 15000,
};

static void https_async(esp_http_client_handle_t client, lora_packet_t *packet)
{
    printf("Testing HTTPS async\n");

    const char *payload = (const char *)packet->payload;

    esp_err_t err;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, payload, packet->payload_size);

    while (1)
    {
        err = esp_http_client_perform(client);
        if (err != ESP_ERR_HTTP_EAGAIN)
        {
            break;
        }
    }
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %" PRIu64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
}

static void http_test_task(void *pvParameters)
{

    esp_http_client_handle_t client = esp_http_client_init(&s_config);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");

    lora_packet_t *packet;

    while (1)
    {
        BaseType_t status = xQueueReceive(s_lora_queue_handler, &packet, portMAX_DELAY);

        if (status == pdTRUE)
        {
            https_async(client, packet);
        }
    }

    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

void lora_receive_task(void *pvParameters)
{
    int recv_size;
    uint8_t *recv_buffer = malloc(256);

    lora_packet_t packet = {
        .payload = recv_buffer,
        .payload_size = 256};

    while (true)
    {
        lora_receive();

        while (lora_received())
        {
            packet.payload_size = lora_receive_packet(packet.payload, 256);
            uint32_t node_id = *((uint32_t *)packet.payload);
            uint32_t message_id = *(((uint32_t *)packet.payload) + 1);
            ESP_LOGI("main", "Received packet with size %d, node_id: %lu, message_id: %lu", packet.payload_size, node_id, message_id);
            xQueueSend(s_lora_queue_handler, &packet, portMAX_DELAY);
            lora_receive();
        }
        vTaskDelay(1);
    }
}

void app_main(void)
{
    // Initialize NVS
    // (non-volatile storage)
    // Enables the wifi driver to store the wifi
    // configuration in flash memory

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initializes the lora driver
    // and configures sensible defaults
    // to achieve a balanced ratio betweeen
    // range, speed and power consumption

    lora_init();
    lora_set_frequency(LORA_FREQ);
    lora_enable_crc();
    lora_set_bandwidth(125e6);
    lora_set_spreading_factor(12);
    lora_set_preamble_length(8);
    lora_set_coding_rate(5);

    // Creates a queue for the received lora packets
    s_lora_queue_handler = xQueueCreate(1, sizeof(lora_packet_t *));

    // Connects to the wifi network
    ESP_ERROR_CHECK(wifi_connect());
    ESP_LOGI(TAG, "Connected to AP, begin http");

    // Creates a lora receive task which
    // lora the lora radio for a new lora packet
    // and upon receiving it, adds it to the queue
    // for further processing
    xTaskCreate(&lora_receive_task, "lora_receive_task", 8192, NULL, 5, NULL);

    // The http transmit task which listens for data
    // in the lora packet queue
    // and sends the packets to a defined
    // web server

    xTaskCreate(&http_test_task, "http_test_task", 8192, NULL, 5, NULL);
}
