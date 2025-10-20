idf_component_register(
        SRCS "main.c" "lcd_driver.c" "WiFi_manager.c" "MQTT_callbacks.c" "WiFi_callbacks.c" "nvs_memory.c" "web_application.c" "http_server.c" "mqtt_manager.c" "util.c" "hardware_layer.c" "interrupts.c" "leds_driver.c"
        INCLUDE_DIRS "."
        EMBED_TXTFILES "certs/root_ca.pem"
)
idf_build_set_property(PARTITION_TABLE_FILENAME partitions.csv)