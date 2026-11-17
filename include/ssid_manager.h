#ifndef SSID_MANAGER_H
#define SSID_MANAGER_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "wifi_credentials.h"

struct SsidItem {
    std::string ssid;
    std::string password;
    // 0 = unknown. 1-14 = 2.4 GHz, 36-177 = 5 GHz. The ranges do not overlap,
    // so one uint8_t NVS key (channel / channelN) covers both bands.
    uint8_t channel = 0;
    PeapProfile peap;
};

class SsidManager {
public:
    static SsidManager& GetInstance() {
        static SsidManager instance;
        return instance;
    }

    bool AddSsid(const std::string& ssid, const std::string& password, uint8_t channel = 0,
                 const PeapProfile& peap = {});
    void UpdateSsidChannel(const std::string& ssid, uint8_t channel);
    bool RemoveSsid(int index);
    bool SetDefaultSsid(int index);
    void Clear();
    std::vector<SsidItem> GetSsidList() const;
    std::vector<uint8_t> GetSavedChannels() const;

private:
    SsidManager();
    ~SsidManager();

    void LoadFromNvs();
    bool Save(std::vector<SsidItem> list);

    mutable std::mutex mutex_;
    std::vector<SsidItem> ssid_list_;
};

#endif // SSID_MANAGER_H
