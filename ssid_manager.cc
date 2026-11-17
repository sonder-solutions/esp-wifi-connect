#include "ssid_manager.h"

#include <esp_log.h>
#include <cJSON.h>
#include <nvs_flash.h>
#include <sdkconfig.h>
#include <algorithm>
#include <cstring>

#define TAG "SsidManager"
#define NVS_NAMESPACE "wifi"
#define MAX_WIFI_SSID_COUNT 10

static std::string MakeWifiKey(const char* prefix, int index) {
    if (index <= 0) {
        return prefix;
    }
    return std::string(prefix) + std::to_string(index);
}

#ifdef CONFIG_ESP_WIFI_CONNECT_PEAP
namespace {

constexpr char kProfilesKey[] = "profiles_v2";
constexpr size_t kMaxProfilesSize = MAX_WIFI_SSID_COUNT * (2 * kMaxPeapCaSize + 2048);

bool ReadProfiles(nvs_handle_t handle, std::vector<SsidItem>& list) {
    size_t size = 0;
    if (nvs_get_blob(handle, kProfilesKey, nullptr, &size) == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    if (size == 0 || size > kMaxProfilesSize) {
        ESP_LOGE(TAG, "Invalid saved Wi-Fi profiles size");
        return true;
    }
    std::string data(size, '\0');
    if (nvs_get_blob(handle, kProfilesKey, data.data(), &size) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read saved Wi-Fi profiles");
        return true;
    }
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> profiles(
        cJSON_ParseWithLengthOpts(data.data(), size, nullptr, true), cJSON_Delete);
    if (!cJSON_IsArray(profiles.get())) {
        ESP_LOGE(TAG, "Invalid saved Wi-Fi profiles");
        return true;
    }
    cJSON* entry = nullptr;
    cJSON_ArrayForEach (entry, profiles.get()) {
        if (list.size() >= MAX_WIFI_SSID_COUNT) {
            break;
        }
        const char* ssid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "ssid"));
        const char* password =
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "password"));
        const auto* channel = cJSON_GetObjectItemCaseSensitive(entry, "channel");
        const auto* enterprise = cJSON_GetObjectItemCaseSensitive(entry, "peap");
        auto peap = enterprise ? ReadPeapProfile(enterprise) : PeapProfile{};
        if (!ssid || !*ssid || std::strlen(ssid) > 32 ||
            (enterprise ? !peap : !password || std::strlen(password) > 64) ||
            !cJSON_IsNumber(channel) || channel->valueint < 0 || channel->valueint > 177) {
            continue;
        }
        list.push_back({ssid, password ? password : "", static_cast<uint8_t>(channel->valueint),
                        std::move(peap)});
    }
    return true;
}

esp_err_t WriteProfiles(nvs_handle_t handle, const std::vector<SsidItem>& list) {
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> profiles(cJSON_CreateArray(),
                                                                   cJSON_Delete);
    if (!profiles) {
        return ESP_ERR_NO_MEM;
    }
    for (const auto& item : list) {
        auto* entry = cJSON_CreateObject();
        if (!entry || !cJSON_AddItemToArray(profiles.get(), entry)) {
            cJSON_Delete(entry);
            return ESP_ERR_NO_MEM;
        }
        if (!cJSON_AddStringToObject(entry, "ssid", item.ssid.c_str()) ||
            !cJSON_AddNumberToObject(entry, "channel", item.channel)) {
            return ESP_ERR_NO_MEM;
        }
        if (item.peap) {
            auto* peap = cJSON_AddObjectToObject(entry, "peap");
            if (!peap || !cJSON_AddStringToObject(peap, "username", item.peap->username.c_str()) ||
                !cJSON_AddStringToObject(peap, "password", item.peap->password.c_str()) ||
                !cJSON_AddStringToObject(peap, "domain", item.peap->domain.c_str()) ||
                !cJSON_AddStringToObject(peap, "ca_pem", item.peap->ca_pem.c_str())) {
                return ESP_ERR_NO_MEM;
            }
        } else if (!cJSON_AddStringToObject(entry, "password", item.password.c_str())) {
            return ESP_ERR_NO_MEM;
        }
    }
    const std::unique_ptr<char, decltype(&cJSON_free)> data(cJSON_PrintUnformatted(profiles.get()),
                                                            cJSON_free);
    if (!data) {
        return ESP_ERR_NO_MEM;
    }
    // A single NVS value keeps auth kind, SSID and credentials together across power loss.
    return nvs_set_blob(handle, kProfilesKey, data.get(), std::strlen(data.get()) + 1);
}

}  // namespace
#endif

SsidManager::SsidManager() {
    LoadFromNvs();
}

SsidManager::~SsidManager() {
}

void SsidManager::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!Save({})) {
        return;
    }
#ifdef CONFIG_ESP_WIFI_CONNECT_PEAP
    // Also remove the pre-upgrade entries retained for older firmware.
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        for (int i = 0; i < MAX_WIFI_SSID_COUNT; ++i) {
            nvs_erase_key(handle, MakeWifiKey("ssid", i).c_str());
            nvs_erase_key(handle, MakeWifiKey("password", i).c_str());
            nvs_erase_key(handle, MakeWifiKey("channel", i).c_str());
        }
        nvs_commit(handle);
        nvs_close(handle);
    }
#endif
}

void SsidManager::LoadFromNvs() {
    ssid_list_.clear();

    // Load ssid / password / channel from NVS namespace "wifi"
    // ssid, ssid1, ... ssid9
    // password, password1, ... password9
    // channel, channel1, ... channel9 (uint8, optional; missing means unknown)
    nvs_handle_t nvs_handle;
    auto ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        // The namespace doesn't exist, just return
        ESP_LOGW(TAG, "NVS namespace %s doesn't exist", NVS_NAMESPACE);
        return;
    }
#ifdef CONFIG_ESP_WIFI_CONNECT_PEAP
    if (ReadProfiles(nvs_handle, ssid_list_)) {
        nvs_close(nvs_handle);
        return;
    }
#endif
    for (int i = 0; i < MAX_WIFI_SSID_COUNT; i++) {
        auto ssid_key = MakeWifiKey("ssid", i);
        auto password_key = MakeWifiKey("password", i);
        auto channel_key = MakeWifiKey("channel", i);

        char ssid[33];
        char password[65];
        size_t length = sizeof(ssid);
        if (nvs_get_str(nvs_handle, ssid_key.c_str(), ssid, &length) != ESP_OK) {
            continue;
        }
        length = sizeof(password);
        if (nvs_get_str(nvs_handle, password_key.c_str(), password, &length) != ESP_OK) {
            continue;
        }
        uint8_t channel = 0;
        nvs_get_u8(nvs_handle, channel_key.c_str(), &channel);
        ssid_list_.push_back({ssid, password, channel});
    }
    nvs_close(nvs_handle);
}

bool SsidManager::Save(std::vector<SsidItem> list) {
    nvs_handle_t handle;
    auto err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not open Wi-Fi settings: %s", esp_err_to_name(err));
        return false;
    }
#ifdef CONFIG_ESP_WIFI_CONNECT_PEAP
    err = WriteProfiles(handle, list);
#else
    for (int i = 0; i < MAX_WIFI_SSID_COUNT && err == ESP_OK; ++i) {
        const auto ssid_key = MakeWifiKey("ssid", i);
        const auto password_key = MakeWifiKey("password", i);
        const auto channel_key = MakeWifiKey("channel", i);
        if (static_cast<size_t>(i) < list.size()) {
            err = nvs_set_str(handle, ssid_key.c_str(), list[i].ssid.c_str());
            if (err == ESP_OK) {
                err = nvs_set_str(handle, password_key.c_str(), list[i].password.c_str());
            }
            if (err == ESP_OK) {
                err = nvs_set_u8(handle, channel_key.c_str(), list[i].channel);
            }
        } else {
            for (const auto* key : {&ssid_key, &password_key, &channel_key}) {
                const auto erased = nvs_erase_key(handle, key->c_str());
                if (erased != ESP_OK && erased != ESP_ERR_NVS_NOT_FOUND) {
                    err = erased;
                    break;
                }
            }
        }
    }
#endif
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not save Wi-Fi profiles: %s", esp_err_to_name(err));
        return false;
    }
    ssid_list_ = std::move(list);
    return true;
}

bool SsidManager::AddSsid(const std::string& ssid, const std::string& password, uint8_t channel,
                          const PeapProfile& peap) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto list = ssid_list_;
    for (auto& item : list) {
        if (item.ssid == ssid) {
            item.password = password;
            item.peap = peap;
            if (channel != 0) {
                item.channel = channel;
            }
            return Save(std::move(list));
        }
    }
    if (list.size() >= MAX_WIFI_SSID_COUNT) {
        list.pop_back();
    }
    list.insert(list.begin(), {ssid, password, channel, peap});
    return Save(std::move(list));
}

void SsidManager::UpdateSsidChannel(const std::string& ssid, uint8_t channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (channel == 0) {
        return;
    }
    auto list = ssid_list_;
    for (auto& item : list) {
        if (item.ssid == ssid && item.channel != channel) {
            item.channel = channel;
            Save(std::move(list));
            return;
        }
    }
}

bool SsidManager::RemoveSsid(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= ssid_list_.size()) {
        return false;
    }
    auto list = ssid_list_;
    list.erase(list.begin() + index);
    return Save(std::move(list));
}

bool SsidManager::SetDefaultSsid(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || static_cast<size_t>(index) >= ssid_list_.size()) {
        return false;
    }
    auto list = ssid_list_;
    std::rotate(list.begin(), list.begin() + index, list.begin() + index + 1);
    return Save(std::move(list));
}

std::vector<SsidItem> SsidManager::GetSsidList() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ssid_list_;
}

std::vector<uint8_t> SsidManager::GetSavedChannels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint8_t> channels;
    for (const auto& item : ssid_list_) {
        if (item.channel == 0) {
            continue;
        }
        if (std::find(channels.begin(), channels.end(), item.channel) == channels.end()) {
            channels.push_back(item.channel);
        }
    }
    return channels;
}
