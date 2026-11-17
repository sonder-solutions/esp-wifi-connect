#pragma once

#include <esp_wifi.h>
#include <cstddef>
#include <memory>
#include <string>

struct cJSON;

struct PeapCredentials {
    std::string username;
    std::string password;
    std::string domain;
    std::string ca_pem;
};

constexpr size_t kMaxPeapCaSize = 4096;
using PeapProfile = std::shared_ptr<const PeapCredentials>;

bool IsEnterpriseAuth(wifi_auth_mode_t auth);
bool IsPeapAuth(wifi_auth_mode_t auth);
PeapProfile ReadPeapProfile(const cJSON* json);
esp_err_t ConfigureWifiCredentials(wifi_config_t& config, const std::string& password,
                                   const PeapProfile& peap);
esp_err_t ResetWifiEnterprise();
