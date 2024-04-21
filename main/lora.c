#include "lora.h"

uint8_t lora_packet_is_duplicate(lora_header_t header) {
  // Iterates the deduplication queue
  // and checks whether a packet
  // with the given message id and node id
  // already exists

    for(int i = 0; i < LORA_DUPLICATE_HISTORY_SIZE; i++) {
        if (s_lora_packet_history[i].message_id == header.message_id && s_lora_packet_history[i].node_id == header.node_id) {
            return 1;
        }
    }
    
    return 0;
}

void lora_add_to_history(lora_header_t header) {
  // Resets the pointer to the first one when we
  // fill the queue
  if(s_lora_next_history_insert >= LORA_DUPLICATE_HISTORY_SIZE) {
      s_lora_next_history_insert = 0;
  };

  // Saves the packet header
  // and advances the pointer
  s_lora_packet_history[s_lora_next_history_insert++] = header;
}

esp_err_t lora_initialize_radio() {
  // The "dh-sender" repository
  // has comments for the radio
  // initialization code

  lora_init();
  lora_set_frequency(LORA_FREQ);
  lora_enable_crc();
  lora_set_bandwidth(125e6);
  lora_set_spreading_factor(12);
  lora_set_preamble_length(8);
  lora_set_coding_rate(5);

  return ESP_OK;
}