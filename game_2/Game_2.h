#ifndef GAME_2_H
#define GAME_2_H

#include "Menu.h"

// @brief Game 2 - Student can implement their own game here

typedef enum {
    STATE_IDLE,      // All stats okay, default animation
    STATE_EATING,    // Meat being dragged / fed
    STATE_SLEEPING,  // Night background, energy recharging
    STATE_PLAYING,   // Joystick interaction / petting
    STATE_UNWELL,    // One or more stats hit zero
    STATE_HAPPY      // All stats full, plays happy tone
} CatState;

typedef enum {
    EVENT_NONE,
    EVENT_BTN_FEED,      // Feed button pressed
    EVENT_BTN_SLEEP,     // Sleep button pressed
    EVENT_JOYSTICK,      // Joystick moved (petting)
    EVENT_STAT_EMPTY,    // Any stat hits 0
    EVENT_STAT_FULL,     // All stats full
    EVENT_ACTION_DONE    // Eating/sleeping animation finished
} CatEvent;

typedef struct {
    CatState state;
    uint8_t  hunger;     // 0–100
    uint8_t  happiness;  // 0–100
    uint8_t  energy;     // 0–100
    uint32_t state_timer; // HAL_GetTick() timestamp of last state entry
} Archie_t;

void FSM_Init(Archie_t *cat);
void FSM_Update(Archie_t *cat, CatEvent event);
 
// @return MenuState - Where to go next (typically MENU_STATE_HOME for menu)


MenuState Game2_Run(void);

#endif // GAME_2_H
