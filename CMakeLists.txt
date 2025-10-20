cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_MQTT_project)

idf_build_set_property(PARTITION_TABLE_FILENAME partitions.csv)
spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)
