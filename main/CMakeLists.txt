# Embed the server root certificate into the final binary
#
# (If this was a component, we would set COMPONENT_EMBED_TXTFILES here.)
set(requires "")
idf_build_get_property(target IDF_TARGET)

if(${target} STREQUAL "linux")
    list(APPEND requires esp_stubs esp-tls esp_http_client protocol_examples_common nvs_flash)
endif()
idf_component_register(SRCS "main.c" "lora.c"
                    INCLUDE_DIRS "include"
                    REQUIRES ${requires}
                    EMBED_TXTFILES gtsr1_root_cert.pem)
