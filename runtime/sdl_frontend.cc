#include <cstdint>
#include <iostream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "runtime/gba_hal.h"
#include "runtime/rom_loader.h"

#ifdef FE7_HAS_SDL3
#include <SDL3/SDL.h>
#endif

ABSL_FLAG(std::string, rom, "", "Path to a retail FE7 US (AE7E) GBA ROM.");

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

  bool running = (window && renderer && texture);
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT) {
        running = false;
      }
    }
    fe7::hal::StepFrame(0);
    fe7::hal::VideoSubmitFrame();
    SDL_UpdateTexture(texture, nullptr, fe7::hal::GetFramebuffer(),
                      fe7::kViewportWidth * static_cast<int>(sizeof(uint32_t)));
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    // Approximate ~60 Hz pacing until audio-driven or vsync frame pacing is
    // wired up.
    SDL_Delay(16);
  }

  // Destroy SDL resources in reverse creation order before SDL_Quit().
  if (texture != nullptr) SDL_DestroyTexture(texture);
  if (renderer != nullptr) SDL_DestroyRenderer(renderer);
  if (window != nullptr) SDL_DestroyWindow(window);
  SDL_Quit();
#else
  // Allow smoke-testing the binary in headless environments without SDL3
  // installed.
  std::cout << "fe7_native built without SDL3; stepping 1 frame headlessly.\n";
  fe7::hal::StepFrame(0);
#endif

  fe7::hal::Shutdown();
  return 0;
}
