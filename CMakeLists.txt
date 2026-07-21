file(GLOB SOURCES_LIST "src/*.cpp" "src/*.c")

idf_component_register(
    SRCS "${SOURCES_LIST}"
    INCLUDE_DIRS "include"
    REQUIRES EmCore esp_wifi esp_event esp_netif esp_http_client app_update espressif__mqtt
)