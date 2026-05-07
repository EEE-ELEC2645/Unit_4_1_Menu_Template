#ifndef GAME_2_H
#define GAME_2_H

#include "Menu.h"

// @brief Game 2 - Student can implement their own game here

typedef enum {
    STATE_IDLE, // default animation
    STATE_EATING, // fed
    STATE_SLEEPING, // energy recharging
    STATE_PLAYING, // joystick interaction (petting)
    STATE_UNWELL, // one or more stats below 20%
    STATE_HAPPY, // all stats above 90%
    STATE_DYING, // all bars 0, 5 second grace period
    STATE_DEAD  // gravestone,
} CatState;

typedef enum {
    EVENT_NONE,
    EVENT_BTN_FEED, // feed button pressed
    EVENT_BTN_SLEEP, // sleep button pressed
    EVENT_JOYSTICK,  // joystick moved (petting)
    EVENT_STAT_EMPTY, // one or more stats below 20%
    EVENT_STAT_FULL,   // all stats above 90%
    EVENT_ACTION_DONE, // eating/sleeping animation finished
    EVENT_DEAD        // all bars hit 0
} CatEvent;

typedef enum {
    PHASE_INSTRUCTIONS,
    PHASE_PLAYING
} GamePhase;

typedef struct {
    CatState state;
    uint8_t  hunger; 
    uint8_t  happiness; 
    uint8_t  energy;
    uint32_t state_timer; // HAL_GetTick() timestamp of last state entry
    uint32_t dying_timer;  // tracks 5 second grace period
} Archie_t;

typedef struct {
    float x, y;
    uint8_t type; // ITEM_NONE, ITEM_FISH, ITEM_BONES
    uint8_t active; // 1 = on screen
    uint32_t spawn_time; // for delayed appearance (bones after eating)
} FoodItem_t;

#define ITEM_NONE  0
#define ITEM_FISH  1
#define ITEM_BONES 2
#define MAX_ITEMS  3

void FSM_Init(Archie_t *cat);
void FSM_Update(Archie_t *cat, CatEvent event);
 
// @return MenuState - Where to go next (typically MENU_STATE_HOME for menu)


MenuState Game2_Run(void);

#endif // GAME_2_H
