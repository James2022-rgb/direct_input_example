
#include "direct_input_context.h"

#include <cinttypes>

#include <optional>
#include <format>

// ------------------------------------------------------------------------------------------------
// DX11 Includes and Libraries
//

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

// ------------------------------------------------------------------------------------------------
// Dear ImGui code is mostly taken from:
// - https://github.com/ocornut/imgui/blob/master/examples/example_win32_directx11/main.cpp
//

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

// DirectInput Data
DirectInputContext g_direct_input_context;

// DX11 Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
    return true;
  }

  switch (msg)
  {
  case WM_SIZE:
    if (wParam == SIZE_MINIMIZED) {
      return 0;
    }
    g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
    g_ResizeHeight = (UINT)HIWORD(lParam);
    return 0;
  case WM_SYSCOMMAND:
    // Disable ALT application menu
    if ((wParam & 0xfff0) == SC_KEYMENU) {
      return 0;
    }
    break;
  case WM_DESTROY:
    ::PostQuitMessage(0);
    return 0;
  }

  return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

void UpdateFrame() {
  static std::optional<GUID> s_opt_selected_guid;

  g_direct_input_context.UpdateDetection();
  g_direct_input_context.UpdateState();

  std::vector<GUID> guids = g_direct_input_context.GetDeviceGuids();

  // Reset selected GUID if it's no longer valid.
  if (s_opt_selected_guid.has_value()) {
    if (std::find(guids.begin(), guids.end(), s_opt_selected_guid.value()) == guids.end()) {
      s_opt_selected_guid.reset();
    }
  }

  ImGui::Begin("Direct Input Devices");

  if (ImGui::BeginTable("DevicesTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {
    ImGui::TableNextColumn(); ImGui::Text("Name");
    ImGui::TableNextColumn(); ImGui::Text("Inst. GUID");
    ImGui::TableNextColumn(); ImGui::Text("# POVs");
    ImGui::TableNextColumn(); ImGui::Text("# Axes");
    ImGui::TableNextColumn(); ImGui::Text("# Buttons");

    for (GUID const& guid : guids) {
      DirectInputContext::Device const* device = g_direct_input_context.GetDevice(guid);

      std::string const guid_str = device->GetGuidString();
      ImGui::PushID(guid_str.c_str());

      ImGui::TableNextColumn();
      if (ImGui::Selectable(device->name.c_str(), s_opt_selected_guid == guid, ImGuiSelectableFlags_None)) {
        if (s_opt_selected_guid == guid) {
          s_opt_selected_guid.reset();
        }
        else {
          s_opt_selected_guid = guid;
        }
      }

      ImGui::TableNextColumn(); ImGui::Text("%s", guid_str.c_str());
      ImGui::TableNextColumn(); ImGui::Text("%" PRIu32, device->caps.dwPOVs);
      ImGui::TableNextColumn(); ImGui::Text("%" PRIu32, device->caps.dwAxes);
      ImGui::TableNextColumn(); ImGui::Text("%" PRIu32, device->caps.dwButtons);

      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  if (s_opt_selected_guid.has_value()) {
    GUID const& guid = s_opt_selected_guid.value();
    DirectInputContext::Device const* device = g_direct_input_context.GetDevice(guid);

    std::string const guid_str = device->GetGuidString();
    ImGui::PushID(guid_str.c_str());

    ImGui::Text("Selected Device: \"%s\" (%s)", device->name.c_str(), guid_str.c_str());

    if (device->caps.dwPOVs > 0) {
      if (ImGui::BeginTable("POVsTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {
        ImGui::TableNextColumn(); ImGui::Text("What");
        ImGui::TableNextColumn(); ImGui::Text("Value");

        for (DWORD i = 0; i < device->caps.dwPOVs; ++i) {
          // > The position is indicated in hundredths of a degree clockwise from north (away from the user).
          DWORD const angle_deg = device->GetPovValue(i) / 100;

          ImGui::TableNextColumn(); ImGui::Text("POV %" PRIu32, i);
          ImGui::TableNextColumn(); ImGui::Text("%" PRIu32, angle_deg);
        }

        ImGui::EndTable();
      }
    }

    if (device->caps.dwAxes > 0) {
      if (ImGui::BeginTable("AxesTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {
        ImGui::TableNextColumn(); ImGui::Text("What");
        ImGui::TableNextColumn(); ImGui::Text("Value");

        for (DWORD i = 0; i < device->caps.dwAxes; ++i) {
          LONG const value = device->GetAxisValue(i);

          float const gauge_value = static_cast<float>(value - DirectInputContext::kAxisMin) / static_cast<float>(DirectInputContext::kAxisMax - DirectInputContext::kAxisMin);

          ImGui::TableNextColumn(); ImGui::Text("Axis %" PRIu32 " (%s)", i, device->GetAxisName(i));
          ImGui::TableNextColumn(); ImGui::ProgressBar(gauge_value, ImVec2(-1, 0), std::format("{} ([{}, {}])", value, DirectInputContext::kAxisMin, DirectInputContext::kAxisMax).c_str());
        }

        ImGui::EndTable();
      }
    }

    if (device->caps.dwButtons > 0) {
      if (ImGui::BeginTable("ButtonsTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {
        ImGui::TableNextColumn(); ImGui::Text("What");
        ImGui::TableNextColumn(); ImGui::Text("Value");

        for (DWORD i = 0; i < device->caps.dwButtons; ++i) {
          bool const value = (device->GetButtonValue(i) & 0x80) != 0;

          ImGui::TableNextColumn(); ImGui::Text("Button %" PRIu32, i);
          ImGui::TableNextColumn(); ImGui::Text("%s", value ? "Pressed" : "Released");
        }

        ImGui::EndTable();
      }
    }

    ImGui::PopID();
  }

  ImGui::End();
}

int main(int argc, char* argv[]) {
  if (!g_direct_input_context.Initialize()) {
    return 1;
  }

  WNDCLASSEXW wc = {
    .cbSize = sizeof(wc),
    .style = CS_CLASSDC,
    .lpfnWndProc = WndProc,
    .cbClsExtra = 0L,
    .cbWndExtra = 0L,
    .hInstance = GetModuleHandle(nullptr),
    .hIcon = nullptr,
    .hCursor = nullptr,
    .hbrBackground = nullptr,
    .lpszMenuName = nullptr,
    .lpszClassName = L"Direct Input Example",
    .hIconSm = nullptr,
  };
  ::RegisterClassExW(&wc);

  HWND hwnd = ::CreateWindowW(
    wc.lpszClassName,
    L"Direct Input Example",
    WS_OVERLAPPEDWINDOW,
    100, 100, 1280, 800,
    nullptr,
    nullptr,
    wc.hInstance,
    nullptr
  );

  // Initialize Direct3D
  if (!CreateDeviceD3D(hwnd)) {
    CleanupDeviceD3D();
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 1;
  }

  // Show the window
  ::ShowWindow(hwnd, SW_SHOWDEFAULT);
  ::UpdateWindow(hwnd);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
#if 1
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
#endif
#if 1
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
#endif

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  //ImGui::StyleColorsLight();

  // Setup Platform/Renderer backends
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  // Main loop
  bool done = false;
  while (!done) {
    // Poll and handle messages (inputs, window resize, etc.)
    // See the WndProc() function below for our to dispatch events to the Win32 backend.
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
    {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
      if (msg.message == WM_QUIT) {
        done = true;
      }
    }
    if (done) {
      break;
    }

    // Handle window being minimized or screen locked
    if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
    {
      ::Sleep(10);
      continue;
    }
    g_SwapChainOccluded = false;

    // Handle window resize (we don't resize directly in the WM_SIZE handler)
    if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
    {
      CleanupRenderTarget();
      g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
      g_ResizeWidth = g_ResizeHeight = 0;
      CreateRenderTarget();
    }

    // Start the Dear ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Update
    UpdateFrame();

    // Rendering
    ImGui::Render();
    const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Present
    HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
    //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
    g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
  }

  // Cleanup
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  CleanupDeviceD3D();
  ::DestroyWindow(hwnd);
  ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

  g_direct_input_context.Shutdown();


#if CONFIG_USE_FTXUI
  while (true) {
    using namespace ftxui;

    if (kbhit()) {
      int const ch = getch();

      if (ch == 'q') {
        break;
      }
    }

    std::vector<Element> elements;
    elements.emplace_back(text("Press 'q' to quit."));

    for (auto const& [ guid, device ] : context.devices) {
      if (device.pDevice == nullptr) {
        continue;
      }
      
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

      for (DWORD i = 0; i < device.caps.dwPOVs; ++i) {
        // > The position is indicated in hundredths of a degree clockwise from north (away from the user).
        DWORD const angle_deg = state.rgdwPOV[i] / 100;
        elements.emplace_back() = hbox({
          text(std::format("POV {}: ", i)),
          text(std::format("{}", angle_deg)) | flex
        });
      }

      for (DWORD i = 0; i < device.caps.dwAxes; ++i) {
        auto GetValue = [&state](DWORD index) -> LONG {
          switch (index) {
            case 0: return state.lX;
            case 1: return state.lY;
            case 2: return state.lZ;
            case 3: return state.lRx;
            case 4: return state.lRy;
            case 5: return state.lRz;
            case 6: return state.rglSlider[0];
            case 7: return state.rglSlider[1];
            default: return 0;
          }
        };

        LONG const value = GetValue(i);

        float const gauge_value = static_cast<float>(value - kAxisMin) / static_cast<float>(kAxisMax - kAxisMin);

        elements.emplace_back() = hbox({
          text(std::format("Axis {}: ", i)),
          text(std::format("{} ([{}, {}])", value, kAxisMin, kAxisMax)),
          gauge(gauge_value)
        });
      }

      for (DWORD i = 0; i < device.caps.dwButtons; ++i) {
        bool const value = (state.rgbButtons[i] & 0x80) != 0;

        elements.emplace_back() = hbox({
          text(std::format("Button {}: ", i)),
          text(std::format("{}", value))
        });
      }
    }

    Element document =
      vbox(elements) | vscroll_indicator | frame;

    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(document));

    Render(screen, document);
    std::cout << reset_position << screen.ToString() << std::flush;
    reset_position = screen.ResetPosition(false);

    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
#endif


}

bool CreateDeviceD3D(HWND hWnd)
{
  // Setup swap chain
  DXGI_SWAP_CHAIN_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = 2;
  sd.BufferDesc.Width = 0;
  sd.BufferDesc.Height = 0;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hWnd;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  UINT createDeviceFlags = 0;
  //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
  HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
  if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
      res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
  if (res != S_OK)
      return false;

  CreateRenderTarget();
  return true;
}

void CleanupDeviceD3D()
{
  CleanupRenderTarget();
  if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
  if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
  if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
  ID3D11Texture2D* pBackBuffer;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
  g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
  pBackBuffer->Release();
}

void CleanupRenderTarget()
{
  if (g_mainRenderTargetView) {
    g_mainRenderTargetView->Release();
    g_mainRenderTargetView = nullptr;
  }
}
