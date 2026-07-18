#pragma once

#include <Arduino.h>

void ui_init();
void ui_splash(const char* msg);
void ui_next_screen();
// Brief invert flash as button-press feedback (the speaker can't be used on
// this board/core combo).
void ui_button_flash();
// Redraws the current screen from g_state. tapoReady = KLAP session is up.
void ui_draw(bool tapoReady);
