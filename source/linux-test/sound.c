#include <string.h>
#include <SDL2/SDL.h>

#include "vb_set.h"
#include "vb_sound.h"
#include "vb_types.h"

static SDL_AudioDeviceID audioDevice;

bool sound_init_backend(int16_t *wavebufs[]) {
    SDL_AudioSpec desiredSpec = {
        .channels = 2,
        .freq = SAMPLE_RATE,
        .format = AUDIO_S16,
        .samples = SAMPLE_COUNT,
    };
    audioDevice = SDL_OpenAudioDevice(NULL, 0, &desiredSpec, NULL, 0);
    if (audioDevice == 0) {
        printf("Audio init error: %s\n", SDL_GetError());
        tVBOpt.SOUND = 0;
        return false;
    }

    SDL_PauseAudioDevice(audioDevice, false);

    return true;
}

void sound_close_backend() {
    SDL_CloseAudioDevice(audioDevice);
}

void sound_pause_backend() {
    SDL_PauseAudioDevice(audioDevice, true);
}

void sound_resume_backend() {
    SDL_PauseAudioDevice(audioDevice, false);
}

bool sound_push_backend(int16_t *buf) {
    // limit queue size to account for fast forwarding
    if (SDL_GetQueuedAudioSize(audioDevice) < SAMPLE_COUNT * 4 * (BUF_COUNT / 2)) {
        SDL_QueueAudio(audioDevice, buf, sizeof(int16_t) * SAMPLE_COUNT * 2);
        return true;
    }
    return false;
}
