// Audio de la pantalla: musica de fondo + efectos, por el amplificador I2S
// integrado de la WT32-SC01 Plus (conector SPEAKER).
//
// Todo el audio corre en una tarea propia en el core 0, asi que dibujar la
// pantalla (core 1) no corta el sonido. Estas funciones solo encolan pedidos
// y se pueden llamar desde los eventos de LVGL.

#pragma once

#include <stdint.h>

enum Sfx : uint8_t {
  SFX_CORRECT,
  SFX_WRONG,
  SFX_BUBBLE,
  SFX_ROJO,
  SFX_AMARILLO,
  SFX_VERDE,
  SFX_AZUL,
};

void audioBegin();

// Reproduce un efecto. Si ya hay uno sonando, se pone en fila detras
// (asi "correcto" + "rojo" se oyen uno despues del otro).
// SFX_BUBBLE nunca se pone en fila: si algo esta sonando, se descarta.
void audioPlaySfx(Sfx sfx);

// Musica de fondo en bucle. Llamar con el mismo valor varias veces no hace nada.
void audioMusic(bool on);
