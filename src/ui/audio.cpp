// Audio output through SDL2 (the original used the waveOut API with a feeder thread).
// Qt owns main(); keep SDL from renaming it to SDL_main (matters on Windows)
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <mutex>

#include "mainwindow.h"

static SDL_AudioDeviceID s_dev;
static std::recursive_mutex s_mutex;
static bool s_sdlReady;

static void audio_callback(void *, Uint8 *stream, int len)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    audio_render((int16_t *)stream, len / 4);
}

static bool open_device(int frames)
{
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = (int)g_outputRate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = (Uint16)(frames < 256 ? 256 : frames > 16384 ? 16384 : frames);
    want.callback = audio_callback;
    s_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    return s_dev != 0;
}

bool audio_open(int frames)
{
    audio_close();
    if (!s_sdlReady) {
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0 && SDL_AudioInit("dummy") != 0)
            return false;
        s_sdlReady = true;
    }
    if (!open_device(frames)) {
        // no sound card: fall back to SDL's silent driver so the player still runs in real time
        fprintf(stderr, "audio: %s; continuing without sound\n", SDL_GetError());
        SDL_AudioQuit();
        if (SDL_AudioInit("dummy") != 0 || !open_device(frames))
            return false;
    }
    SDL_PauseAudioDevice(s_dev, 0);
    return true;
}

void audio_close(void)
{
    if (s_dev != 0) {
        SDL_CloseAudioDevice(s_dev);
        s_dev = 0;
    }
}

void audio_lock(void)
{
    s_mutex.lock();
}

void audio_unlock(void)
{
    s_mutex.unlock();
}
