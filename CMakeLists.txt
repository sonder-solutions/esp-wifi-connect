set(public_requirements esp_timer esp_http_server esp_wifi nvs_flash)
if(IDF_VERSION_MAJOR LESS 6)
    list(APPEND public_requirements json)
endif()

idf_component_register(
    SRCS "wifi_manager.cc" "wifi_configuration_ap.cc" "wifi_station.cc"
         "ssid_manager.cc" "dns_server.cc" "wifi_credentials.cc"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES "assets/wifi_configuration.html"
    REQUIRES ${public_requirements}
    PRIV_REQUIRES esp_hw_support wpa_supplicant mbedtls
)
