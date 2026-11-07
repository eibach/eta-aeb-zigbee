idf_component_register(SRCS "main.c" "eta_link.c" "status_led.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_timer driver eta_proto)
