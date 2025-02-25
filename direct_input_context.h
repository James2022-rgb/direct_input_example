#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <functional>

#if !defined(NOMINMAX)
# define NOMINMAX
#endif

#if !defined(DIRECTINPUT_VERSION)
# define DIRECTINPUT_VERSION 0x0800
#endif
#include <initguid.h>
#include <dinput.h>

class DirectInputContext final {
public:
  static inline constexpr LONG kAxisMin = -32767;
  static inline constexpr LONG kAxisMax = +32767;

  enum class InputType : uint32_t {
    kPOV,
    kAxis,
    kButton,
  };

  /// These are sorted by `offset`, which is a byte offset into `DIJOYSTATE2`.
  /// This allows us to give each axis a consistent index we can use,
  /// as opposed to their DirectInput axis names like "X", "Y", "Z" which can be counter-intuitive for many devices,
  /// i.e. left throttle being "Rx" and right toe brake being "Ry".
  struct Input final {
    InputType type;
    DWORD index;
    DWORD offset;
  };

  struct Device final {
    GUID guid {};
    std::string name;
    /// Could be `IDirectInputDevice8A` or `IDirectInputDevice8W`, depending on whether `UNICODE` is defined.
    IDirectInputDevice8* pDevice = nullptr;
    DIDEVCAPS caps {};

    std::vector<Input> povs;
    std::vector<Input> buttons;
    std::vector<Input> axes;

    /// Updated in `UpdateState`.
    DIJOYSTATE2 state {};

    std::string GetGuidString() const;
    char const* GetAxisName(DWORD index) const;

    DWORD GetPovValue(DWORD index) const;
    LONG GetAxisValue(DWORD index) const;
    BYTE GetButtonValue(DWORD index) const;
  };

  DirectInputContext() = default;
  ~DirectInputContext() noexcept = default;

  DirectInputContext(DirectInputContext const&) = delete;
  DirectInputContext(DirectInputContext&&) = delete;
  DirectInputContext& operator=(DirectInputContext const&) = delete;
  DirectInputContext& operator=(DirectInputContext&&) = delete;

  bool Initialize();
  void Shutdown();

  void UpdateDetection();
  void UpdateState();

  std::vector<GUID> GetDeviceGuids() const {
    std::vector<GUID> guids;
    guids.reserve(devices_.size());
    for (auto const& [guid, device] : devices_) {
      guids.push_back(guid);
    }
    return guids;
  }

  Device const* GetDevice(GUID const& guid) const {
    auto it = devices_.find(guid);
    if (it == devices_.end()) {
      return nullptr;
    }
    return &it->second;
  }

private:
  struct GuidHasher final {
    size_t operator()(GUID const& v) const noexcept {
      RPC_STATUS status = RPC_S_OK;
      return ::UuidHash(const_cast<GUID*>(&v), &status);
    }
  };

  /// Could be `IDirectInput8A` or `IDirectInput8W`.
  IDirectInput8* pDI_ = nullptr;

  std::unordered_map<GUID, Device, GuidHasher> devices_;
};
