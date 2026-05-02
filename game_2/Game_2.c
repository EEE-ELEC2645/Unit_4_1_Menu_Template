#include "Game_2.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Buzzer.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t buzzer_cfg;  // Buzzer control

//@brief Game 2 Implementation - Student can modify

// Game variables - customize for your game
static int player_x = 0;
static int player_y = 0;

// Frame rate for this game (in milliseconds)
#define GAME2_FRAME_TIME_MS 30

// defined spectrums
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240

// This is so bars dont exceed 100 or go below 0, and to make code cleaner in FSM_Update
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

/* Game Initialisation */
void Game2_Init(void) {
    player_x = 0;
    player_y = 0;
}

// FSM functions
void FSM_Init(Archie_t *cat) {
    cat->state       = STATE_IDLE;
    cat->hunger      = 80; // when game loads up, 80% full bar
    cat->happiness   = 80;
    cat->energy      = 80;
    cat->state_timer = HAL_GetTick();
}

void FSM_Update(Archie_t *cat, CatEvent event) {
    switch (cat->state) {

        case STATE_IDLE:
            if (event == EVENT_STAT_EMPTY)  { cat->state = STATE_UNWELL;   }
            if (event == EVENT_STAT_FULL)   { cat->state = STATE_HAPPY;    }
            if (event == EVENT_BTN_FEED)    { cat->state = STATE_EATING;   }
            if (event == EVENT_BTN_SLEEP)   { cat->state = STATE_SLEEPING; }
            if (event == EVENT_JOYSTICK)    { cat->state = STATE_PLAYING;  }
            break;
        case STATE_EATING:
            cat->hunger = MIN(cat->hunger + 20, 100);
            if (event == EVENT_ACTION_DONE) { cat->state = STATE_IDLE; }
            break;
        case STATE_SLEEPING:
            cat->energy = MIN(cat->energy + 30, 100);
            if (event == EVENT_ACTION_DONE) { cat->state = STATE_IDLE; }
            break;
        case STATE_PLAYING:
            cat->happiness = MIN(cat->happiness + 20, 100);
            if (event == EVENT_ACTION_DONE) { cat->state = STATE_IDLE; }
            break;
        case STATE_UNWELL:
            if (event == EVENT_BTN_FEED  ||
                event == EVENT_BTN_SLEEP ||
                event == EVENT_JOYSTICK) {
                cat->state = STATE_IDLE;
            }
            break;
        case STATE_HAPPY:
            if (HAL_GetTick() - cat->state_timer > 2000) {
                cat->state = STATE_IDLE;
            }
            break;
    }
    cat->state_timer = HAL_GetTick();
}


MenuState Game2_Run(void) {

    Archie_t archie;
    FSM_Init(&archie);
    uint32_t last_decay = HAL_GetTick();

    // Play a brief startup sound
    buzzer_tone(&buzzer_cfg, 1200, 30);  // 1.2kHz at 30% volume
    HAL_Delay(50);  // Brief beep duration
    buzzer_off(&buzzer_cfg);  // Stop the buzzer
    
    MenuState exit_state = MENU_STATE_HOME;  // Default: return to menu

    // Game's own loop - runs until exit condition
    while (1) {
        uint32_t frame_start = HAL_GetTick();

        // Read input
        Input_Read();
        
        // Check if button was pressed to return to menu
        if (current_input.btn3_pressed) {
            exit_state = MENU_STATE_HOME;
            break;  // Exit game loop
        }

        // FSM events based on input
        CatEvent event = EVENT_NONE;

        if (current_input.btn2_pressed) event = EVENT_BTN_FEED; // i need to add another button for this feed function on the hardware, but for now just reusing btn2 for feeding and sleeping to demonstrate
        if (current_input.btn2_pressed) event = EVENT_BTN_SLEEP;
        // if (current_input.joystick_moved) event = EVENT_JOYSTICK;

        // stat bars decay every 3 seconds
        if (HAL_GetTick() - last_decay > 3000) {
            archie.hunger    = MAX(archie.hunger    - 2, 0);
            archie.happiness = MAX(archie.happiness - 1, 0);
            archie.energy    = MAX(archie.energy    - 1, 0);
            last_decay = HAL_GetTick();

            if (archie.hunger == 0 || archie.happiness == 0 || archie.energy == 0)
                event = EVENT_STAT_EMPTY;
            if (archie.hunger == 100 && archie.happiness == 100 && archie.energy == 100)
                event = EVENT_STAT_FULL;
        }

        FSM_Update(&archie, event);

        // RENDER: Draw to LCD
        LCD_Fill_Buffer(0);
        
        // Title
        LCD_printString("MeowPet", 60, 10, 1, 3);
    
        // TODO: replace with sprite draw calls
        switch (archie.state) {
            case STATE_IDLE:     LCD_printString("Archie: idle",     40, 80, 1, 2); break;
            case STATE_EATING:   LCD_printString("Archie: eating",   40, 80, 1, 2); break;
            case STATE_SLEEPING: LCD_printString("Archie: sleeping", 40, 80, 1, 2); break;
            case STATE_PLAYING:  LCD_printString("Archie: playing",  40, 80, 1, 2); break;
            case STATE_UNWELL:   LCD_printString("Archie: unwell",   40, 80, 1, 2); break;
            case STATE_HAPPY:    LCD_printString("Archie: happy!",   40, 80, 1, 2); break;
        }

        // Temporary stat readout - replace with bar graphics later
        char stats[64];
        sprintf(stats, "H:%d HP:%d E:%d", archie.hunger, archie.happiness, archie.energy);
        LCD_printString(stats, 20, 200, 1, 1);

        LCD_printString("BT3: Menu", 60, 220, 1, 1);

        LCD_Refresh(&cfg0);
        
        // Frame timing - wait for remainder of frame time
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < GAME2_FRAME_TIME_MS) {
            HAL_Delay(GAME2_FRAME_TIME_MS - frame_time);
        }
    }
    
    return exit_state;  // Tell main where to go next
}
