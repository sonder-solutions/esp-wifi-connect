#include "wifi_credentials.h"

#include <algorithm>
#include <cstring>

#include <esp_idf_version.h>
#include <cJSON.h>
#include <sdkconfig.h>

#ifdef CONFIG_ESP_WIFI_CONNECT_PEAP
#include <esp_eap_client.h>
#include <mbedtls/x509_crt.h>
#endif

bool IsEnterpriseAuth(wifi_auth_mode_t auth) {
    switch (auth) {
        case WIFI_AUTH_WPA3_ENT_192:
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
        case WIFI_AUTH_WPA3_ENTERPRISE:
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
        case WIFI_AUTH_WPA_ENTERPRISE:
#endif
            return true;
        default:
            return IsPeapAuth(auth);
    }
}

bool IsPeapAuth(wifi_auth_mode_t auth) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
    if (auth == WIFI_AUTH_WPA2_WPA3_ENTERPRISE) {
        return true;
    }
#endif
    return auth == WIFI_AUTH_WPA2_ENTERPRISE;
}

#ifdef CONFIG_ESP_WIFI_CONNECT_PEAP
namespace {

// ESP-IDF borrows the CA bytes for the lifetime of the EAP session.
PeapProfile active_peap;

bool ReadString(const cJSON* json, const char* key, size_t max_size, std::string& value) {
    const char* field = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, key));
    if (!field || std::strlen(field) > max_size) {
        return false;
    }
    value = field;
    return true;
}

bool IsDnsName(const std::string& name) {
    size_t start = 0;
    while (start < name.size()) {
        const size_t dot = name.find('.', start);
        const size_t end = dot == std::string::npos ? name.size() : dot;
        if (end == start || end - start > 63 || name[start] == '-' || name[end - 1] == '-') {
            return false;
        }
        if (!std::all_of(name.begin() + start, name.begin() + end, [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                       c == '-';
            })) {
            return false;
        }
        if (dot == std::string::npos) {
            return true;
        }
        start = dot + 1;
    }
    return false;
}

esp_err_t EnablePeap(const PeapCredentials& peap) {
    esp_err_t err;
    if (peap.ca_pem.empty()) {
        err = esp_eap_client_use_default_cert_bundle(true);
    } else {
        err = esp_eap_client_set_ca_cert(reinterpret_cast<const uint8_t*>(peap.ca_pem.c_str()),
                                         peap.ca_pem.size() + 1);
    }
    if (err != ESP_OK) {
        return err;
    }
    const auto* username = reinterpret_cast<const uint8_t*>(peap.username.data());
    const auto* password = reinterpret_cast<const uint8_t*>(peap.password.data());
    if ((err = esp_eap_client_set_domain_name(peap.domain.c_str())) != ESP_OK ||
        (err = esp_eap_client_set_identity(username, peap.username.size())) != ESP_OK ||
        (err = esp_eap_client_set_username(username, peap.username.size())) != ESP_OK ||
        (err = esp_eap_client_set_password(password, peap.password.size())) != ESP_OK ||
        (err = esp_eap_client_set_eap_methods(ESP_EAP_TYPE_PEAP)) != ESP_OK) {
        return err;
    }
    return esp_wifi_sta_enterprise_enable();
}

}  // namespace
#endif

PeapProfile ReadPeapProfile(const cJSON* json) {
#ifdef CONFIG_ESP_WIFI_CONNECT_PEAP
    auto peap = std::make_shared<PeapCredentials>();
    if (!cJSON_IsObject(json) || !ReadString(json, "username", 128, peap->username) ||
        peap->username.empty() || !ReadString(json, "password", 128, peap->password) ||
        peap->password.empty() || !ReadString(json, "domain", 253, peap->domain) ||
        !IsDnsName(peap->domain) || !ReadString(json, "ca_pem", kMaxPeapCaSize, peap->ca_pem)) {
        return {};
    }
    if (!peap->ca_pem.empty()) {
        mbedtls_x509_crt ca;
        mbedtls_x509_crt_init(&ca);
        const int result = mbedtls_x509_crt_parse(
            &ca, reinterpret_cast<const uint8_t*>(peap->ca_pem.c_str()), peap->ca_pem.size() + 1);
        mbedtls_x509_crt_free(&ca);
        if (result != 0) {
            return {};
        }
    }
    return peap;
#else
    return {};
#endif
}

esp_err_t ResetWifiEnterprise() {
#ifdef CONFIG_ESP_WIFI_CONNECT_PEAP
    if (active_peap) {
        auto err = esp_wifi_sta_enterprise_disable();
        if (err != ESP_OK) {
            return err;
        }
        active_peap.reset();
    }
#endif
    return ESP_OK;
}

esp_err_t ConfigureWifiCredentials(wifi_config_t& config, const std::string& password,
                                   const PeapProfile& peap) {
    auto err = ResetWifiEnterprise();
    if (err != ESP_OK) {
        return err;
    }
    if (!peap) {
        std::memcpy(config.sta.password, password.data(),
                    std::min(password.size(), sizeof(config.sta.password)));
        return esp_wifi_set_config(WIFI_IF_STA, &config);
    }
#ifdef CONFIG_ESP_WIFI_CONNECT_PEAP
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_ENTERPRISE;
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) {
        active_peap = peap;
        err = EnablePeap(*peap);
        if (err != ESP_OK) {
            ResetWifiEnterprise();
        }
    }
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
