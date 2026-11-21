#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "runtime/gba_hal.h"
#include "runtime/rom_loader.h"

#ifdef FE7_HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace {

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " --rom <path>\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string rom_path;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--rom" && i + 1 < argc) {
      rom_path = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    }
  }

  if (rom_path.empty()) {
    PrintUsage(argv[0]);
    return 1;
  }

  fe7::RomImage rom;
  std::string error;
  if (!fe7::LoadRomFile(rom_path, &rom, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  if (!fe7::hal::Init(rom)) {
    std::cerr << "Error: failed to initialize GbaHal\n";
    return 1;
  }

#ifdef FE7_HAS_SDL3
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    std::cerr << "Error: SDL_Init failed: " << SDL_GetError() << "\n";
    fe7::hal::Shutdown();
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow(
      "Fire Emblem 7 (Native)", fe7::kViewportWidth * 3, fe7::kViewportHeight * 3,
      SDL_WINDOW_RESIZABLE);
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
  SDL_Texture* texture =
      renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XBGR8888,
                                   SDL_TEXTUREACCESS_STREAMING, fe7::kViewportWidth,
                                   fe7::kViewportHeight)
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
    SDL_Delay(16);
  }

  if (texture != nullptr) SDL_DestroyTexture(texture);
  if (renderer != nullptr) SDL_DestroyRenderer(renderer);
  if (window != nullptr) SDL_DestroyWindow(window);
  SDL_Quit();
#else
  std::cout << "fe7_native built without SDL3; stepping 1 frame headlessly.\n";
  fe7::hal::StepFrame(0);
#endif

  fe7::hal::Shutdown();
  return 0;
}
