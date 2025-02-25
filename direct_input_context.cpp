//
// Huge thanks to the following resources:
// - SDL2's SDL joystick: https://github.com/libsdl-org/SDL/blob/release-2.24.x/src/joystick/windows/SDL_dinputjoystick.c
//

#include "direct_input_context.h"

#include <iostream>
#include <format>
#include <algorithm>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "Rpcrt4.lib") // `UuidHash`.

/// 0: Use `CoCreateInstance` to create `IDirectInput8`.
/// 1: Use `DirectInput8Create` to create `IDirectInput8`.
#if !defined(CONFIG_USE_DIRECTINPUT8CREATE)
# define CONFIG_USE_DIRECTINPUT8CREATE (1)
#endif

namespace {

std::string ToMultiByteImpl(wchar_t const* str) {
  int const size = WideCharToMultiByte(CP_UTF8, 0, str, -1, nullptr, 0, nullptr, nullptr);
  if (size == 0) {
    return "";
  }
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, str, -1, result.data(), size, nullptr, nullptr);
  return result;
}

std::string ToMultiByte(TCHAR const* str) {
#ifdef UNICODE
  return ToMultiByteImpl(str);
#else
  return str;
#endif
}

#if !defined(UNICODE)
std::string ToMultiByte(wchar_t const* str) {
  return ToMultiByteImpl(str);
}
#endif

// `IDirectInput8` could be `IDirectInput8A` or `IDirectInput8W`, depending on whether `UNICODE` is defined.
IDirectInput8* CreateDirectInput8() {
  // Could be `IID_IDirectInput8A` or `IID_IDirectInput8W`.
  IID const& iid = IID_IDirectInput8;

  HMODULE hModule = GetModuleHandle(nullptr);

  IDirectInput8* pDI = NULL;

#if CONFIG_USE_DIRECTINPUT8CREATE
  // Use `DirectInput8Create`.

  HRESULT hr = DirectInput8Create(hModule, DIRECTINPUT_VERSION, iid, (void**)&pDI, nullptr);
  if (FAILED(hr)) {
    return nullptr;
  }
  
  return pDI;

#else
  // Use `CoCreateInstance`.

  HRESULT hr = CoInitialize(nullptr);
  if (FAILED(hr)) {
    return nullptr;
  }

  hr = CoCreateInstance(CLSID_DirectInput8, nullptr, CLSCTX_INPROC_SERVER, iid, (void**)&pDI);
  if (FAILED(hr)) {
    return nullptr;
  }

  hr = pDI->Initialize(hModule, DIRECTINPUT_VERSION);
  if (FAILED(hr)) {
    return nullptr;
  }
#endif

  return pDI;
}

void ReleaseDirectInput8(IDirectInput8* pDI) {
  if (pDI != nullptr) {
    pDI->Release();
  }

#if !CONFIG_USE_DIRECTINPUT8CREATE
  CoUninitialize();
#endif
}

}

std::string DirectInputContext::Device::GetGuidString() const {
  wchar_t guid_str[39] = { 0 };
  ::StringFromGUID2(this->guid, guid_str, 39);
  return ToMultiByte(guid_str);
}

char const* DirectInputContext::Device::GetAxisName(DWORD index) const {
  Input const& input = this->axes[index];

  switch (input.offset) {
  case DIJOFS_X: return "X";
  case DIJOFS_Y: return "Y";
  case DIJOFS_Z: return "Z";
  case DIJOFS_RX: return "Rx";
  case DIJOFS_RY: return "Ry";
  case DIJOFS_RZ: return "Rz";
  case DIJOFS_SLIDER(0): return "Slider 0";
  case DIJOFS_SLIDER(1): return "Slider 1";
  default: return "Unknown";
  };
}

DWORD DirectInputContext::Device::GetPovValue(DWORD index) const {
  Input const& input = this->povs[index];

  return this->state.rgdwPOV[(input.offset - DIJOFS_POV(0)) / sizeof(DWORD)];
}

LONG DirectInputContext::Device::GetAxisValue(DWORD index) const {
  Input const& input = this->axes[index];
  
  switch (input.offset) {
  case DIJOFS_X: return this->state.lX;
  case DIJOFS_Y: return this->state.lY;
  case DIJOFS_Z: return this->state.lZ;
  case DIJOFS_RX: return this->state.lRx;
  case DIJOFS_RY: return this->state.lRy;
  case DIJOFS_RZ: return this->state.lRz;
  case DIJOFS_SLIDER(0): return this->state.rglSlider[0];
  case DIJOFS_SLIDER(1): return this->state.rglSlider[1];
  default: return 0;
  }
}

BYTE DirectInputContext::Device::GetButtonValue(DWORD index) const {
  Input const& input = this->buttons[index];

  return this->state.rgbButtons[(input.offset - DIJOFS_BUTTON(0)) / sizeof(BYTE)];
}

bool DirectInputContext::Initialize() {
  pDI_ = CreateDirectInput8();
  if (pDI_ == nullptr) {
    return false;
  }

  this->UpdateDetection();

  std::cout << std::format("Found {} devices:", devices_.size()) << std::endl;
  for (auto const& [ guid, device ] : devices_) {
    wchar_t guid_str[39] = { 0 };
    ::StringFromGUID2(guid, guid_str, 39);

    std::cout << std::format(" \"{}\" ({})", device.name, ToMultiByte(guid_str)) << std::endl;
    std::cout << std::format("  {} POVs (Hats)", device.caps.dwPOVs) << std::endl;
    std::cout << std::format("  {} Axes", device.caps.dwAxes) << std::endl;
    std::cout << std::format("  {} Buttons", device.caps.dwButtons) << std::endl;
  }

  return true;
}

void DirectInputContext::Shutdown() {
  // Release `IDirectInputDevice8` for each device.
  for (auto& [ guid, device ] : devices_) {
    device.pDevice->Release();
  }
  devices_.clear();

  ReleaseDirectInput8(pDI_);
  pDI_ = nullptr;
}

void DirectInputContext::UpdateDetection() {
  std::vector<GUID> device_guids;

  // Use `IDirectInput8::EnumDevices` to populate `device_names`.
  {
    auto DIEnumDevicesCallback = [](LPCDIDEVICEINSTANCE lpddi, LPVOID pvRef) -> BOOL {
      std::vector<GUID>& device_guids = *static_cast<std::vector<GUID>*>(pvRef);

      device_guids.emplace_back(lpddi->guidInstance);

      return DIENUM_CONTINUE;
    };

    pDI_->EnumDevices(DI8DEVCLASS_GAMECTRL, DIEnumDevicesCallback, &device_guids, DIEDFL_ATTACHEDONLY);
  }

  // Remove devices that are no longer present.
  {
    auto it = std::erase_if(
      devices_,
      [&device_guids](std::pair<GUID, Device> const& kvp) {
        GUID const& guid = kvp.first;

        return std::find(device_guids.begin(), device_guids.end(), guid) == device_guids.end();
      }
    );
  }

  for (GUID const& device_guid : device_guids) {
    if (devices_.find(device_guid) != devices_.end()) {
      continue;
    }

    IDirectInputDevice8* pDevice = nullptr;
    HRESULT hr = pDI_->CreateDevice(device_guid, &pDevice, nullptr);
    if (FAILED(hr)) {
      std::cout << "IDirectInput8::CreateDevice failed." << std::endl;
      continue;
    }

    std::string product_name;
    {
      DIPROPSTRING dipstr {};
      dipstr.diph.dwSize = sizeof(DIPROPSTRING);
      dipstr.diph.dwHeaderSize = sizeof(DIPROPHEADER);
      dipstr.diph.dwObj = 0;
      dipstr.diph.dwHow = DIPH_DEVICE;

      hr = pDevice->GetProperty(DIPROP_PRODUCTNAME, &dipstr.diph);
      if (FAILED(hr)) {
        pDevice->Release();
        continue;
      }

      product_name = ToMultiByte(dipstr.wsz);
    }

    // Acquire shared access to the device (exclusive access would be required for FFB).
    hr = pDevice->SetCooperativeLevel(nullptr, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    if (FAILED(hr)) {
      pDevice->Release();
      continue;
    }

    // Extended joystick state format.
    hr = pDevice->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr)) {
      pDevice->Release();
      continue;
    }

    // Get capabilities using `IDirectInputDevice8::GetCapabilities`.
    DIDEVCAPS caps {};
    {
      caps.dwSize = sizeof(DIDEVCAPS);

      hr = pDevice->GetCapabilities(&caps);
      if (FAILED(hr)) {
        pDevice->Release();
        continue;
      }
    }

    // Enumerate device objects (POVs, axes and buttons) using `IDirectInputDevice8::EnumObjects` to set properties.
    struct InputInfo final {
      std::vector<Input> povs;
      std::vector<Input> buttons;
      std::vector<Input> axes;
      DWORD slider_count = 0;
    } input_info;
    {
      struct Capture final {
        IDirectInputDevice8* pDevice = nullptr;
        InputInfo& input_info;
      };

      auto DIEnumDeviceObjectsCallback = [](LPCDIDEVICEOBJECTINSTANCE lpddoi, LPVOID pvRef) -> BOOL {
        Capture const& capture = *static_cast<Capture const*>(pvRef);

        if (lpddoi->dwType & DIDFT_POV) {
          DWORD const index = static_cast<DWORD>(capture.input_info.povs.size());

          capture.input_info.povs.push_back(Input{
            .type = InputType::kPOV,
            .index = index,
            .offset = static_cast<DWORD>(DIJOFS_POV(index)),
          });
        }
        else if (lpddoi->dwType & DIDFT_BUTTON) {
          DWORD const index = static_cast<DWORD>(capture.input_info.buttons.size());

          capture.input_info.buttons.push_back(Input{
            .type = InputType::kButton,
            .index = index,
            .offset = static_cast<DWORD>(DIJOFS_BUTTON(index)),
          });
        }
        else if (lpddoi->dwType & DIDFT_AXIS) {
          DWORD const index = static_cast<DWORD>(capture.input_info.axes.size());

          DWORD offset = 0;
          if (lpddoi->guidType == GUID_XAxis) {
            offset = DIJOFS_X;
          }
          else if (lpddoi->guidType == GUID_YAxis) {
            offset = DIJOFS_Y;
          }
          else if (lpddoi->guidType == GUID_ZAxis) {
            offset = DIJOFS_Z;
          }
          else if (lpddoi->guidType == GUID_RxAxis) {
            offset = DIJOFS_RX;
          }
          else if (lpddoi->guidType == GUID_RyAxis) {
            offset = DIJOFS_RY;
          }
          else if (lpddoi->guidType == GUID_RzAxis) {
            offset = DIJOFS_RZ;
          }
          else if (lpddoi->guidType == GUID_Slider) {
            DWORD const slider_index = capture.input_info.slider_count++;
            offset = DIJOFS_SLIDER(slider_index);
          }
          else {
            return DIENUM_CONTINUE;
          }

          capture.input_info.axes.push_back(Input{
            .type = InputType::kAxis,
            .index = index,
            .offset = offset,
          });

          // Set the range for the axis.
          {
            DIPROPRANGE diprg {};
            diprg.diph.dwSize = sizeof(DIPROPRANGE);
            diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            diprg.diph.dwObj = lpddoi->dwType;
            diprg.diph.dwHow = DIPH_BYID;
            diprg.lMin = kAxisMin;
            diprg.lMax = kAxisMax;

            capture.pDevice->SetProperty(DIPROP_RANGE, &diprg.diph);
          }

          // Set the dead zone for the axis to 0.
          {
            DIPROPDWORD dipdw {};
            dipdw.diph.dwSize = sizeof(DIPROPDWORD);
            dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            dipdw.diph.dwObj = lpddoi->dwType;
            dipdw.diph.dwHow = DIPH_BYID;
            dipdw.dwData = 0;

            capture.pDevice->SetProperty(DIPROP_DEADZONE, &dipdw.diph);
          }
        }

        return DIENUM_CONTINUE;
      };

      Capture capture {
        .pDevice = pDevice,
        .input_info = input_info,
      };

      // Enumerate POVs (hats), axes and buttons.
      pDevice->EnumObjects(DIEnumDeviceObjectsCallback, &capture, DIDFT_POV | DIDFT_AXIS | DIDFT_BUTTON);

      // Sort by the offset into `DIJOYSTATE2`, so we can index into `DIJOYSTATE2` using a consistent index.
      std::sort(
        input_info.povs.begin(), input_info.povs.end(),
        [](Input const& lhs, Input const& rhs) {
          return lhs.offset < rhs.offset;
        }
      );
      std::sort(
        input_info.buttons.begin(), input_info.buttons.end(),
        [](Input const& lhs, Input const& rhs) {
          return lhs.offset < rhs.offset;
        }
      );
      std::sort(
        input_info.axes.begin(), input_info.axes.end(),
        [](Input const& lhs, Input const& rhs) {
          return lhs.offset < rhs.offset;
        }
      );
    }

    devices_[device_guid] = Device {
      .guid = device_guid,
      .name = product_name,
      .pDevice = pDevice,
      .caps = caps,
      .povs = std::move(input_info.povs),
      .buttons = std::move(input_info.buttons),
      .axes = std::move(input_info.axes),
    };
  }
}

void DirectInputContext::UpdateState() {
  for (auto& [ guid, device ] : devices_) {
    HRESULT hr = device.pDevice->Poll();
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
      device.pDevice->Acquire();
      hr = device.pDevice->Poll();
      if (FAILED(hr)) {
        continue;
      }
    }

    DIJOYSTATE2 state {};
    hr = device.pDevice->GetDeviceState(sizeof(DIJOYSTATE2), &state);
    if (FAILED(hr)) {
      continue;
    }

    device.state = state;
  }
}
