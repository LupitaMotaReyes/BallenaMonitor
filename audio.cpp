// Ver audio.h
#include <Arduino.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <AudioOutputMixer.h>

#include "audio.h"

// Sonidos (MP3 a 44100 Hz en PROGMEM; ver tools/mp3_header.py)
#include "bgm.h"
#include "correct.h"
#include "wrong2.h"
#include "bubble.h"
#include "rojo.h"
#include "amarillo.h"
#include "verde.h"
#include "azul.h"

// ---- Pines del amplificador I2S integrado (conector SPEAKER) ----------
constexpr int PIN_I2S_BCLK = 36;
constexpr int PIN_I2S_LRCK = 35;
constexpr int PIN_I2S_DOUT = 37;

// ---- Volumen -------------------------------------------------------------
// La bocina es de 8 ohm / 1 W y el amplificador a 5 V puede dar mas que eso:
// MASTER_GAIN limita el volumen total. No subirlo de 0.75 (~0.9 W de pico en 8 ohm).
constexpr float MASTER_GAIN = 0.6;
constexpr float MUSIC_GAIN = 1.0;       // musica de fondo (el MP3 ya viene ~4 dB bajo el maximo)
constexpr float MUSIC_DUCK_GAIN = 0.3;  // musica mientras suena un efecto
constexpr float SFX_GAIN = 1.0;

struct SfxData {
  const uint8_t *data;
  uint32_t len;
};

// Mismo orden que el enum Sfx (audio.h)
static const SfxData SFX_TABLE[] = {
  {correct, sizeof(correct)},    // SFX_CORRECT
  {wrong2, sizeof(wrong2)},      // SFX_WRONG
  {bubble, sizeof(bubble)},      // SFX_BUBBLE
  {rojo, sizeof(rojo)},          // SFX_ROJO
  {amarillo, sizeof(amarillo)},  // SFX_AMARILLO
  {verde, sizeof(verde)},        // SFX_VERDE
  {azul, sizeof(azul)},          // SFX_AZUL
};

enum AudioCmdType : uint8_t { CMD_SFX, CMD_MUSIC_ON, CMD_MUSIC_OFF };

struct AudioCmd {
  AudioCmdType type;
  Sfx sfx;
};

static QueueHandle_t cmdQueue = nullptr;
static bool musicRequested = false;  // solo lo toca el hilo de la UI

static AudioOutputI2S *out;
static AudioOutputMixer *mixer;
static AudioOutputMixerStub *musicStub, *sfxStub;
static AudioGeneratorMP3 *musicMp3, *sfxMp3;
static AudioFileSourcePROGMEM *musicSrc, *sfxSrc;

// Efectos en fila detras del que esta sonando
constexpr int SFX_PENDING_MAX = 4;
static Sfx pending[SFX_PENDING_MAX];
static int pendingCount = 0;
static Sfx currentSfx;

static void startMusic() {
  musicSrc->close();
  musicSrc->open(bgm, sizeof(bgm));
  musicMp3->begin(musicSrc, musicStub);
}

static void startSfx(Sfx sfx) {
  if (sfxMp3->isRunning()) {
    sfxMp3->stop();
  }
  sfxSrc->close();
  sfxSrc->open(SFX_TABLE[sfx].data, SFX_TABLE[sfx].len);
  sfxMp3->begin(sfxSrc, sfxStub);
  currentSfx = sfx;
  musicStub->SetGain(MUSIC_DUCK_GAIN);
}

static void handleCmd(const AudioCmd &cmd, bool &musicOn) {
  switch (cmd.type) {
    case CMD_MUSIC_ON:
      musicOn = true;
      break;
    case CMD_MUSIC_OFF:
      musicOn = false;
      if (musicMp3->isRunning()) {
        musicMp3->stop();
      }
      break;
    case CMD_SFX:
      // La burbuja no hace esperar: cualquier otro efecto la interrumpe
      if (!sfxMp3->isRunning() || (currentSfx == SFX_BUBBLE && cmd.sfx != SFX_BUBBLE)) {
        startSfx(cmd.sfx);
      } else if (cmd.sfx != SFX_BUBBLE && pendingCount < SFX_PENDING_MAX) {
        pending[pendingCount++] = cmd.sfx;
      }
      break;
  }
}

static void audioTask(void *) {
  bool musicOn = false;
  AudioCmd cmd;

  for (;;) {
    while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) {
      handleCmd(cmd, musicOn);
    }

    // Musica en bucle: al terminar vuelve a empezar
    if (musicOn) {
      if (!musicMp3->isRunning()) {
        startMusic();
      } else if (!musicMp3->loop()) {
        musicMp3->stop();
        startMusic();
      }
    }

    if (sfxMp3->isRunning() && !sfxMp3->loop()) {
      sfxMp3->stop();
      if (pendingCount > 0) {
        Sfx next = pending[0];
        pendingCount--;
        memmove(pending, pending + 1, pendingCount * sizeof(Sfx));
        startSfx(next);
      } else {
        musicStub->SetGain(MUSIC_GAIN);
      }
    }

    // Ceder el CPU un momento; el buffer DMA del I2S cubre la pausa
    vTaskDelay(1);
  }
}

void audioBegin() {
  out = new AudioOutputI2S();
  // Buffer DMA chico (~23 ms) para que los efectos suenen en cuanto se toca la pantalla
  // (el valor por defecto es ~65 ms). El audio tiene su propio core, asi que no se vacia.
  out->SetBuffers(4, 1024);
  out->SetPinout(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT);
  out->SetOutputModeMono(true);  // una sola bocina
  out->SetGain(MASTER_GAIN);

  mixer = new AudioOutputMixer(32, out);
  musicStub = mixer->NewInput();
  musicStub->SetGain(MUSIC_GAIN);
  sfxStub = mixer->NewInput();
  sfxStub->SetGain(SFX_GAIN);

  musicSrc = new AudioFileSourcePROGMEM();
  sfxSrc = new AudioFileSourcePROGMEM();
  musicMp3 = new AudioGeneratorMP3();
  sfxMp3 = new AudioGeneratorMP3();

  cmdQueue = xQueueCreate(8, sizeof(AudioCmd));
  // Core 0: la UI (loop de Arduino) corre en el core 1
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 3, nullptr, 0);
}

void audioPlaySfx(Sfx sfx) {
  if (cmdQueue == nullptr) return;
  AudioCmd cmd = {CMD_SFX, sfx};
  xQueueSend(cmdQueue, &cmd, 0);
}

void audioMusic(bool on) {
  if (cmdQueue == nullptr || on == musicRequested) return;
  musicRequested = on;
  AudioCmd cmd = {on ? CMD_MUSIC_ON : CMD_MUSIC_OFF, SFX_CORRECT};
  xQueueSend(cmdQueue, &cmd, 0);
}
