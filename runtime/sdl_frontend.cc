#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "game/register_all.h"
#include "runtime/gba_hal.h"
#include "runtime/rom_loader.h"

#ifdef FE7_HAS_SDL3
#include <SDL3/SDL.h>
#endif

ABSL_FLAG(std::string, rom, "", "Path to a retail FE7 US (AE7E) GBA ROM.");
ABSL_FLAG(std::string, save, "",
          "Optional path to a .sav file to load on startup and save on exit.");

namespace {

// Loads a raw .sav file into GbaHal if `path` exists; missing files are ignored
// so first-time launches start with a clean save file.
void TryLoadSaveFile(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in.is_open()) {
    return;
  }
  const std::ifstream::pos_type size = in.tellg();
  if (size <= 0) {
    return;
  }
  std::vector<uint8_t> buf(static_cast<size_t>(size));
  in.seekg(0, std::ios::beg);
  if (in.read(reinterpret_cast<char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()))) {
    fe7::hal::LoadSaveData(buf.data(), buf.size());
  }
}

// Writes active cartridge save RAM back to `path` when non-empty.
void TryWriteSaveFile(const std::string& path) {
  if (path.empty()) {
    return;
  }
  const size_t save_size = fe7::hal::CopySaveData(nullptr, 0);
  if (save_size == 0) {
    return;
  }
  std::vector<uint8_t> buf(save_size);
  fe7::hal::CopySaveData(buf.data(), buf.size());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (out.is_open()) {
    out.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
  }
}

#ifdef FE7_HAS_SDL3
// Samples current SDL3 keyboard and optional gamepad state into a GBA KEYINPUT
// bitmask.
uint16_t PollGbaKeys(SDL_Gamepad* gamepad) {
  uint16_t keys = 0;
  const bool* kb = SDL_GetKeyboardState(nullptr);
  if (kb != nullptr) {
    if (kb[SDL_SCANCODE_Z]) keys |= fe7::kKeyA;
    if (kb[SDL_SCANCODE_X]) keys |= fe7::kKeyB;
    if (kb[SDL_SCANCODE_RSHIFT] || kb[SDL_SCANCODE_BACKSPACE]) {
      keys |= fe7::kKeySelect;
    }
    if (kb[SDL_SCANCODE_RETURN]) keys |= fe7::kKeyStart;
    if (kb[SDL_SCANCODE_RIGHT]) keys |= fe7::kKeyRight;
    if (kb[SDL_SCANCODE_LEFT]) keys |= fe7::kKeyLeft;
    if (kb[SDL_SCANCODE_UP]) keys |= fe7::kKeyUp;
    if (kb[SDL_SCANCODE_DOWN]) keys |= fe7::kKeyDown;
    if (kb[SDL_SCANCODE_S]) keys |= fe7::kKeyR;
    if (kb[SDL_SCANCODE_A]) keys |= fe7::kKeyL;
  }
  if (gamepad != nullptr) {
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST)) {
      keys |= fe7::kKeyA;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) {
      keys |= fe7::kKeyB;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK)) {
      keys |= fe7::kKeySelect;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START)) {
      keys |= fe7::kKeyStart;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
      keys |= fe7::kKeyRight;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
      keys |= fe7::kKeyLeft;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
      keys |= fe7::kKeyUp;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
      keys |= fe7::kKeyDown;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
      keys |= fe7::kKeyR;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
      keys |= fe7::kKeyL;
    }
  }
  return keys;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage(
      "Native FE7 frontend. Example: fe7_native --rom=<path>");
  absl::ParseCommandLine(argc, argv);

  const std::string rom_path = absl::GetFlag(FLAGS_rom);
  if (rom_path.empty()) {
    std::cerr << "Error: --rom is required.\n";
    return 1;
  }

  auto rom = fe7::LoadRomFile(rom_path);
  if (!rom.ok()) {
    std::cerr << "Error: " << rom.status().message() << "\n";
    return 1;
  }

  if (!fe7::hal::Init(*rom)) {
    std::cerr << "Error: failed to initialize GbaHal\n";
    return 1;
  }
  fe7::RegisterAllNativeFunctions();

  const std::string save_path = absl::GetFlag(FLAGS_save);
  TryLoadSaveFile(save_path);

#ifdef FE7_HAS_SDL3
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    std::cerr << "Error: SDL_Init failed: " << SDL_GetError() << "\n";
    fe7::hal::Shutdown();
    return 1;
  }
  // Open at 3x native GBA resolution (720x480) for crisp integer scaling on
  // desktop displays.
  SDL_Window* window =
      SDL_CreateWindow("Fire Emblem 7 (Native)", fe7::kViewportWidth * 3,
                       fe7::kViewportHeight * 3, SDL_WINDOW_RESIZABLE);
  SDL_Renderer* renderer =
      window ? SDL_CreateRenderer(window, nullptr) : nullptr;
  // Match libmgba's 32-bit XBGR8888 software framebuffer layout for
  // zero-conversion uploads.
  SDL_Texture* texture =
      renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XBGR8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   fe7::kViewportWidth, fe7::kViewportHeight)
               : nullptr;

  // Open a 48 kHz stereo S16 audio stream (non-fatal if no audio device exists
  // on the host).
  const SDL_AudioSpec audio_spec = {SDL_AUDIO_S16, 2, fe7::kAudioSampleRate};
  SDL_AudioStream* audio_stream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, nullptr, nullptr);
  if (audio_stream != nullptr) {
    SDL_ResumeAudioStreamDevice(audio_stream);
  }

  // Attach the first available gamepad if connected.
  SDL_Gamepad* gamepad = nullptr;
  int gamepad_count = 0;
  SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepad_count);
  if (gamepads != nullptr) {
    if (gamepad_count > 0) {
      gamepad = SDL_OpenGamepad(gamepads[0]);
    }
    SDL_free(gamepads);
  }

  std::vector<int16_t> audio_buf(2048 * 2);
  bool running = (window && renderer && texture);
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT) {
        running = false;
      }
    }
    fe7::hal::StepFrame(PollGbaKeys(gamepad));
    fe7::hal::VideoSubmitFrame();

    const size_t frames =
        fe7::hal::ReadAudioSamples(audio_buf.data(), audio_buf.size() / 2);
    if (audio_stream != nullptr && frames > 0) {
      SDL_PutAudioStreamData(audio_stream, audio_buf.data(),
                             static_cast<int>(frames * 2 * sizeof(int16_t)));
    }

    SDL_UpdateTexture(texture, nullptr, fe7::hal::GetFramebuffer(),
                      fe7::kViewportWidth * static_cast<int>(sizeof(uint32_t)));
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    // Approximate ~60 Hz pacing until audio-driven or vsync pacing is added.
    SDL_Delay(16);
  }

  // Destroy SDL resources in reverse creation order before SDL_Quit().
  if (gamepad != nullptr) SDL_CloseGamepad(gamepad);
  if (audio_stream != nullptr) SDL_DestroyAudioStream(audio_stream);
  if (texture != nullptr) SDL_DestroyTexture(texture);
  if (renderer != nullptr) SDL_DestroyRenderer(renderer);
  if (window != nullptr) SDL_DestroyWindow(window);
  SDL_Quit();
#else
  // Allow smoke-testing the binary in headless environments without SDL3.
  std::cout << "fe7_native built without SDL3; stepping 1 frame headlessly.\n";
  fe7::hal::StepFrame(0);
#endif

  TryWriteSaveFile(save_path);
  fe7::hal::Shutdown();
  return 0;
}
