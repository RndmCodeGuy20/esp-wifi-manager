idf_component_register(
    SRCS "src/policy.cpp" "src/wifi_manager.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_common
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer freertos
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
