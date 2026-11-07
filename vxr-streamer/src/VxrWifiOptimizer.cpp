#include "VxrWifiOptimizer.h"

namespace vxr {

VxrWifiOptimizer::VxrWifiOptimizer()
    : m_wlanClient(nullptr)
    , m_scanLocked(false)
    , m_lockedCount(0)
{
}

VxrWifiOptimizer::~VxrWifiOptimizer() {
    UnlockBackgroundScan();
}

bool VxrWifiOptimizer::LockBackgroundScan() {
    if (m_scanLocked.load()) {
        return true;
    }

    DWORD negVer = 0;
    HANDLE hClient = nullptr;
    DWORD status = WlanOpenHandle(2, nullptr, &negVer, &hClient);
    if (status != ERROR_SUCCESS || hClient == nullptr) {
        return false;
    }

    PWLAN_INTERFACE_INFO_LIST pIfList = nullptr;
    status = WlanEnumInterfaces(hClient, nullptr, &pIfList);
    if (status != ERROR_SUCCESS || pIfList == nullptr) {
        WlanCloseHandle(hClient, nullptr);
        return false;
    }

    m_wlanClient = hClient;
    m_lockedGuids.clear();
    m_lockedCount = 0;

    BOOL bEnable = FALSE; // Disable background scanning
    for (DWORD i = 0; i < pIfList->dwNumberOfItems; ++i) {
        const GUID& guid = pIfList->InterfaceInfo[i].InterfaceGuid;
        DWORD setRes = WlanSetInterface(
            m_wlanClient,
            &guid,
            wlan_intf_opcode_background_scan_enabled,
            sizeof(BOOL),
            &bEnable,
            nullptr
        );
        if (setRes == ERROR_SUCCESS) {
            m_lockedGuids.push_back(guid);
            m_lockedCount++;
        }

        // Also enable Media Streaming Mode if supported by the NIC driver for low-latency QoS
        BOOL bMediaStreaming = TRUE;
        WlanSetInterface(
            m_wlanClient,
            &guid,
            wlan_intf_opcode_media_streaming_mode,
            sizeof(BOOL),
            &bMediaStreaming,
            nullptr
        );
    }

    WlanFreeMemory(pIfList);
    m_scanLocked.store(true);
    return true;
}

void VxrWifiOptimizer::UnlockBackgroundScan() {
    if (!m_scanLocked.exchange(false)) {
        if (m_wlanClient != nullptr) {
            WlanCloseHandle(m_wlanClient, nullptr);
            m_wlanClient = nullptr;
        }
        return;
    }

    if (m_wlanClient != nullptr) {
        BOOL bEnable = TRUE; // Restore normal background scanning
        BOOL bMediaStreaming = FALSE;
        for (const GUID& guid : m_lockedGuids) {
            WlanSetInterface(
                m_wlanClient,
                &guid,
                wlan_intf_opcode_background_scan_enabled,
                sizeof(BOOL),
                &bEnable,
                nullptr
            );
            WlanSetInterface(
                m_wlanClient,
                &guid,
                wlan_intf_opcode_media_streaming_mode,
                sizeof(BOOL),
                &bMediaStreaming,
                nullptr
            );
        }
        m_lockedGuids.clear();
        m_lockedCount = 0;
        WlanCloseHandle(m_wlanClient, nullptr);
        m_wlanClient = nullptr;
    }
}

} // namespace vxr
