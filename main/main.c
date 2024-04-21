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

// The backend authentication token
#define BACKEND_AUTH_TOKEN "uO0Ofm2uGvOYG3p67kffMlUBP7uYPM"

// Log tag
static const char *TAG = "HTTP_CLIENT";

// The google trust chain root certificate
// for https
extern const char gtsr1_root_cert_pem_start[] asm("_binary_gtsr1_root_cert_pem_start");
extern const char gtsr1_root_cert_pem_end[] asm("_binary_gtsr1_root_cert_pem_end");

// The lora packet queue
static QueueHandle_t s_lora_queue_handler;

// Packet history used for deduplication of packets if by chance any relay nodes see each other

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    // Buffer to store response of the http request from event handler
    static char *output_buffer;
    // Stores number of bytes read
    static int output_len;

    switch (evt->event_id) {
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
            //printf("EVENT ON HEADER\n");
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            // Check whether the response data is presented
            // as a chunked response and ignore it if it is 
            if (!esp_http_client_is_chunked_response(evt->client)) {
                int copy_len = 0;

                if (evt->user_data) {
                    copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    // Reads the content length header to determine response body size
                    const int buffer_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        // If the buffer has not been already allocated
                        // it allocates it using the received size
                        output_buffer = (char *)malloc(buffer_len);

                        // Clears it before actually using it
                        output_len = 0;

                        // Checks whether we do not have enough memory
                        if (output_buffer == NULL) {
                            return ESP_FAIL;
                        }
                    }

                    // Copy the response data into the previously allocated
                    // buffer
                    copy_len = MIN(evt->data_len, (buffer_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }

                output_len += copy_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            if (output_buffer != NULL)
            {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }

            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        default:
            // Not interested
    }
   
    return ESP_OK;
}

// Client config
// the url of the target server
// the google trust chain root certificate for https
// and a request timeout

static esp_http_client_config_t s_config = {
    .url = "https://dragonhack.ttcloud.io/api/data",
    .event_handler = _http_event_handler,
    .cert_pem = gtsr1_root_cert_pem_start,
    .is_async = true,
    .timeout_ms = 15000,
};

static void https_async(esp_http_client_handle_t client, lora_packet_t *packet) {
    esp_err_t err;
    char *payload = (const char *)(packet->payload);

    // Sets the request method to POST (as we are sending data)
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    // Sets the request Authorization header which provides the auth token
    esp_http_client_set_header(client, "Authorization", "Basic " BACKEND_AUTH_TOKEN);

    // Sets the request body (to the lora packet)
    esp_http_client_set_post_field(client, payload, packet->payload_size);

    // Performs the request 
    while (1) {
        err = esp_http_client_perform(client);
        if (err != ESP_ERR_HTTP_EAGAIN) {
            break;
        }
    }

    // Prints the request status  code
    // and the content length of the response body on success.
    // Prints the error name in case of an error.

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %" PRIu64,
        esp_http_client_get_status_code(client),
        esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
}

void http_transmit_task(void *pvParameters) {
    // Initializes the client (we are sending a request to a server
    // and as such are considered a client)
    // with the url or the server
    esp_http_client_handle_t client = esp_http_client_init(&s_config);

    // Sets the content header to octet-stream as we are sending raw
    // binary data
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");

    // Create a struct for storing the lora packet
    lora_packet_t packet;

    // Enter the task `body`
    // continously runs checks whether there is data
    // in the queue, then reads the packet from it
    // and sends it to the server

    while (1) {
        BaseType_t status = xQueueReceive(s_lora_queue_handler, &packet, portMAX_DELAY);
        
        if (status == pdTRUE) {
            https_async(client, &packet);
        }
    }

    // Cleanup of the client which closes some connections 
    // and frees resources
    esp_http_client_cleanup(client);

    // Removes the task (not necessary)
    vTaskDelete(NULL);
}

void lora_receive_task(void *pvParameters) {
    // Stores the payload of the recevied packet
    uint8_t *recv_buffer = malloc(256);

    // Creates a lora packet
    lora_packet_t packet = {
        .payload = recv_buffer,
        .payload_size = 256};

    while (true) {
        // Continously puts the LoRa radio into receive mode
        lora_receive();

        // Checks if some data is available (was recevied)
        // and the radio is ready to send it to us

        while (lora_received()) {
            // Populate the packet struct with actual received packet data
            packet.payload_size = lora_receive_packet(packet.payload, 256);

            // Create the lora packet header 
            lora_header_t header = {
                .node_id = *((uint32_t *)packet.payload),
                .message_id = *(((uint32_t *)packet.payload) + 1)
            };

            // Check if the packet is already contained in the
            // history buffer
            // Send it if it is not duplicate otherwise ignore

            if(lora_packet_is_duplicate(header) == 0) {
                // Adds the packet the the queue
                xQueueSend(s_lora_queue_handler, &packet, portMAX_DELAY);
            }

            // Adds the header to the deduplication queue
            // AKA history queue
            lora_add_to_history(header);
            lora_receive();
        }
        vTaskDelay(1);
    }
}

void app_main(void) {
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

    // Initializes the underlying
    // lwip driver code which
    // implements a TCP/IP 
    // stack optimized for 
    // embedded users

    ESP_ERROR_CHECK(esp_netif_init());

    // Create the default system event loop
    // which the wifi driver code uses to communicate
    // with us

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initializes the lora driver
    // and configures sensible defaults
    // to achieve a balanced ratio betweeen
    // range, speed and power consumption
    ESP_ERROR_CHECK(lora_initialize_radio());

    // Creates a queue for the received lora packets
    s_lora_queue_handler = xQueueCreate(1, sizeof(lora_packet_t));

    // Connects to the wifi network
    // using an ESP IDF provided example
    ESP_ERROR_CHECK(example_connect());
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
    xTaskCreate(&http_transmit_task, "http_transmit_task", 8192, NULL, 5, NULL);

    // NOTE: The tasks have the same priority
}
