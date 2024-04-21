#ifndef _LORA_H_
#define _LORA_H_

#include "esp_err.h"
#include <stdlib.h>
#include "sx1278.h"

// Size of lora duplication queue. Must not be larger than 256
#define LORA_DUPLICATE_HISTORY_SIZE 100

// Lora radio operating frequency in europe
#define LORA_FREQ 433e6

// Lora packet definition struct
typedef struct
{
    uint8_t *payload;
    size_t payload_size;
} lora_packet_t;

// Lora header definition struct
typedef struct
{
    uint32_t node_id;
    uint32_t message_id;
} lora_header_t;

// Packet history used for deduplication of packets
// if by change any relay nodes see each other
static lora_header_t s_lora_packet_history[LORA_DUPLICATE_HISTORY_SIZE];
static uint8_t s_lora_next_history_insert = 0;

esp_err_t lora_initialize_radio();
uint8_t lora_packet_is_duplicate(lora_header_t header);
void lora_add_to_history(lora_header_t header);

#endif