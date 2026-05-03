#include "Game_2.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Buzzer.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t buzzer_cfg;  // Buzzer control
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data; // for reading joystick input

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
            cat->hunger = MIN(cat->hunger + 1, 100);
            if (event == EVENT_ACTION_DONE) { cat->state = STATE_IDLE; }
            break;
        case STATE_SLEEPING:
            cat->energy = MIN(cat->energy + 1, 100);
            if (event == EVENT_BTN_SLEEP || cat->energy >= 100) { cat->state = STATE_IDLE; }
            break;
        case STATE_PLAYING:
            cat->happiness = MIN(cat->happiness + 1, 100);
            if (event != EVENT_JOYSTICK) { cat->state = STATE_IDLE; }
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

// stat bars!!
// x, y = top-left position, value = 0-100
void Draw_Stat_Bar(uint16_t x, uint16_t y, uint8_t value, uint8_t bar_colour) {
    uint16_t max_width = 80;  // full bar width in pixels
    uint16_t height    = 8;

    // background (empty bar)
    LCD_Draw_Rect(x, y, max_width, height, 13, 1);  // colour 13 = grey, fill=1

    // filled portion
    uint16_t filled = (uint16_t)(value * max_width / 100);
    if (filled > 0) {
        LCD_Draw_Rect(x, y, filled, height, bar_colour, 1);
    }

    // outline on top
    LCD_Draw_Rect(x, y, max_width, height, 1, 0);  // colour 1 = white, fill=0
}

MenuState Game2_Run(void) {

    Archie_t archie;
    FSM_Init(&archie);
    uint32_t last_decay = HAL_GetTick();
    uint8_t carrying_fish = 0;
    uint8_t carrying_bones = 0;
    uint8_t fish_eaten = 0;
    uint32_t fish_respawn_timer = 0;

    // mapping archie, cursor, fish
    float cursor_x = 120.0f;  // start cursor in centre of screen
    float cursor_y = 120.0f;
    float prev_cursor_x = 120.0f;
    float prev_cursor_y = 120.0f;
    float fish_x = 20.0f;
    float fish_y = 150.0f;
    #define CURSOR_SPEED 5.0f
    #define ARCHIE_X 70
    #define ARCHIE_Y 100
    #define ARCHIE_W 60
    #define ARCHIE_H 60
    #define FISH_W 20
    #define FISH_H 15

    // Play a brief startup sound
    buzzer_tone(&buzzer_cfg, 1200, 30);  // 1.2kHz at 30% volume
    HAL_Delay(50);  // Brief beep duration
    buzzer_off(&buzzer_cfg);  // Stop the buzzer
    
    MenuState exit_state = MENU_STATE_HOME;  // Default: return to menu

    // Game's own loop - runs until exit condition
    while (1) {
        uint32_t frame_start = HAL_GetTick();

        // FSM events based on input
        CatEvent event = EVENT_NONE;

        // Read input
        Input_Read();
        
        // Read joystick and move cursor
        Joystick_Read(&joystick_cfg, &joystick_data);
        cursor_x += joystick_data.coord_mapped.x * CURSOR_SPEED;
        cursor_y -= joystick_data.coord_mapped.y * CURSOR_SPEED;  // y is inverted on screen

        // Clamp cursor to screen bounds
        if (cursor_x < 2)   cursor_x = 2;
        if (cursor_x > 238) cursor_x = 238;
        if (cursor_y < 2)   cursor_y = 2;
        if (cursor_y > 238) cursor_y = 238;

        // Check if cursor actually moved this frame
        uint8_t cursor_moved = ((int16_t)cursor_x != (int16_t)prev_cursor_x || (int16_t)cursor_y != (int16_t)prev_cursor_y);
        // Update previous position
        prev_cursor_x = cursor_x;
        prev_cursor_y = cursor_y;

        // Check if cursor is hovering over Archie
        uint8_t hovering = (cursor_x >= ARCHIE_X && cursor_x <= ARCHIE_X + ARCHIE_W &&
                            cursor_y >= ARCHIE_Y && cursor_y <= ARCHIE_Y + ARCHIE_H);
        if (hovering && cursor_moved) event = EVENT_JOYSTICK;

        // Check if cursor is over menu button
        uint8_t over_menu = (cursor_x >= 3 && cursor_x <= 54 &&
                             cursor_y >= 8 && cursor_y <= 25);
        if (current_input.btn3_pressed && over_menu) {
            exit_state = MENU_STATE_HOME;
            break;
        }

        // Fish grab and drop
        uint8_t over_fish = (!carrying_fish && !fish_eaten && cursor_x >= fish_x && cursor_x <= fish_x + FISH_W && cursor_y >= fish_y && cursor_y <= fish_y + FISH_H);
        uint8_t over_bones = (!carrying_bones && fish_eaten == 2 && cursor_x >= fish_x && cursor_x <= fish_x + FISH_W && cursor_y >= fish_y && cursor_y <= fish_y + FISH_H);
        uint8_t over_archie = (cursor_x >= ARCHIE_X && cursor_x <= ARCHIE_X + ARCHIE_W && cursor_y >= ARCHIE_Y && cursor_y <= ARCHIE_Y + ARCHIE_H);

        if (current_input.btn3_pressed) {
            if (over_fish) {
                carrying_fish = 1; // pick up the fish
            } else if (over_bones) {
                carrying_bones = 1;
            } else if (carrying_fish && over_archie) {
                carrying_fish = 0;
                fish_eaten = 1;
                fish_respawn_timer = HAL_GetTick();
                archie.hunger = MIN(archie.hunger + 30, 100);
                fish_x = cursor_x;
                fish_y = cursor_y;
            }
        }

        if (carrying_fish)  { fish_x = cursor_x; fish_y = cursor_y; }
        if (carrying_bones) { fish_x = cursor_x; fish_y = cursor_y; }

        // Fish follows cursor when carried
        if (carrying_fish) {
            fish_x = cursor_x;
            fish_y = cursor_y;
        }
        // Respawn bones after 3 seconds
        if (fish_eaten == 1 && HAL_GetTick() - fish_respawn_timer > 3000) {
            fish_eaten = 2;  // 2 = bones phase
        }

        // if (current_input.btn2_pressed) event = EVENT_BTN_FEED; // i need to add another button for this feed function on the hardware, but for now just reusing btn2 for feeding and sleeping to demonstrate
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
        LCD_Draw_Rect((uint16_t)cursor_x - 2, (uint16_t)cursor_y - 2, 5, 5, 1, 1); // Draw cursor (small cross)
        
        // Title
        LCD_printString("MeowPet", 60, 10, 1, 3);
    
        // Menu exit button
        LCD_Draw_Rect(3, 8, 51, 17, 2, 1);  // red filled rect
        LCD_printString("MENU", 5, 10, 1, 2);

        LCD_Draw_Rect(ARCHIE_X, ARCHIE_Y, ARCHIE_W, ARCHIE_H, 1, 0); // for testing archie position, replace with sprite later

        // TODO: replace with sprite draw calls
        switch (archie.state) {
            case STATE_IDLE:     LCD_printString("Archie: idle",     40, 100, 1, 2); break;
            case STATE_EATING:   LCD_printString("Archie: eating",   40, 100, 1, 2); break;
            case STATE_SLEEPING: LCD_printString("Archie: sleeping", 40, 100, 1, 2); break;
            case STATE_PLAYING:  LCD_printString("Archie: playing",  40, 100, 1, 2); break;
            case STATE_UNWELL:   LCD_printString("Archie: unwell",   40, 100, 1, 2); break;
            case STATE_HAPPY:    LCD_printString("Archie: happy!",   40, 100, 1, 2); break;
        }

        // Stat bars
        LCD_printString("Hunger:", 10, 40, 1, 1);
        Draw_Stat_Bar(70, 40, archie.hunger,    5);  // colour 5 = orange
        LCD_printString("Happiness:", 10, 55, 1, 1);
        Draw_Stat_Bar(70, 55, archie.happiness, 3);  // colour 3 = green
        LCD_printString("Energy:", 10, 70, 1, 1);
        Draw_Stat_Bar(70, 70, archie.energy,    4);  // colour 4 = blue

        // Draw fish or bones
        if (fish_eaten == 0 || carrying_fish) {
            LCD_Draw_Rect((uint16_t)fish_x, (uint16_t)fish_y, FISH_W, FISH_H, 2, 1);  // red = fish
        }
        if (fish_eaten == 2) {
            LCD_Draw_Rect((uint16_t)fish_x, (uint16_t)fish_y, FISH_W, FISH_H, 4, 1);  // blue = bones
        }

        LCD_Refresh(&cfg0);
        
        // Frame timing - wait for remainder of frame time
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < GAME2_FRAME_TIME_MS) {
            HAL_Delay(GAME2_FRAME_TIME_MS - frame_time);
        }
    }
    
    return exit_state;  // Tell main where to go next
}
