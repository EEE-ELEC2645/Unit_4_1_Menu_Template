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

// Frame rate for this game (in milliseconds)
#define GAME2_FRAME_TIME_MS 30

// defined spectrums
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240

// This is so bars dont exceed 100 or go below 0, and to make code cleaner in FSM_Update
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

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
            if (event == EVENT_DEAD){ 
                cat->dying_timer = HAL_GetTick();
                cat->state = STATE_DYING;        
            }
            else if (event == EVENT_STAT_EMPTY)       { cat->state = STATE_UNWELL;   }
            else if (event == EVENT_BTN_FEED)    { cat->state = STATE_EATING;   }
            else if (event == EVENT_BTN_SLEEP)   { cat->state = STATE_SLEEPING; }
            else if (event == EVENT_JOYSTICK)    { cat->state = STATE_PLAYING;  }
            else if (event == EVENT_STAT_FULL)   { cat->state = STATE_HAPPY;    }
            break;
        case STATE_EATING:
            if (HAL_GetTick() - cat->state_timer > 3000) { cat->state = STATE_IDLE; }
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
            if (event == EVENT_DEAD)            { 
                cat->dying_timer = HAL_GetTick();
                cat->state = STATE_DYING;       
            }
            else if (event == EVENT_BTN_FEED)   { cat->state = STATE_EATING;   }
            else if (event == EVENT_BTN_SLEEP)  { cat->state = STATE_SLEEPING; }
            else if (event == EVENT_JOYSTICK)   { cat->state = STATE_PLAYING;  }
            break;
        case STATE_HAPPY:
            // only leave happy if stats drop below threshold or unwell
            if (cat->hunger < 90 || cat->happiness < 90 || cat->energy < 90)
                cat->state = STATE_IDLE;
            if (event == EVENT_STAT_EMPTY)   { cat->state = STATE_UNWELL;   }
            if (event == EVENT_BTN_FEED)     { cat->state = STATE_EATING;   }
            if (event == EVENT_BTN_SLEEP)    { cat->state = STATE_SLEEPING; }
            if (event == EVENT_JOYSTICK)     { cat->state = STATE_PLAYING;  }
            break; 
        case STATE_DYING:
            // Saved by player interaction
            if (event == EVENT_BTN_FEED) {
                cat->hunger    = MIN(cat->hunger    + 20, 100);
                cat->happiness = MIN(cat->happiness + 10, 100);
                cat->state = STATE_UNWELL;
            } else if (event == EVENT_BTN_SLEEP) {
                cat->energy    = MIN(cat->energy    + 20, 100);
                cat->happiness = MIN(cat->happiness + 10, 100);
                cat->state = STATE_UNWELL;
            } else if (event == EVENT_JOYSTICK) {
                cat->happiness = MIN(cat->happiness + 30, 100);
                cat->state = STATE_UNWELL;
            }
            // 5 second grace period expired
            if (HAL_GetTick() - cat->dying_timer > 5000) {
                cat->state = STATE_DEAD;
            }
            break;
        case STATE_DEAD:
            // nothing — handled in game loop render/restart
            break;
    }

    // only reset timer when state actually changes
    static CatState prev_state = STATE_IDLE;
    if (cat->state != prev_state) {
        cat->state_timer = HAL_GetTick();
        prev_state = cat->state;
    }
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

    GamePhase phase = PHASE_INSTRUCTIONS;
    uint8_t game_started = 0;

    // food item variables
    FoodItem_t items[MAX_ITEMS] = {0};
    items[0].x = 20; 
    items[0].y = 150;
    items[0].type = ITEM_FISH; 
    items[0].active = 1;
    int8_t carried_index = -1;
    uint32_t fish_respawn_timer = HAL_GetTick();
    uint8_t fish_respawn_pending = 0;
    uint32_t blink_timer = HAL_GetTick();
    uint8_t blink_visible = 1;

    // mapping archie, cursor, bin
    float cursor_x = 120.0f;  // start cursor in centre of screen
    float cursor_y = 120.0f;
    float prev_cursor_x = 120.0f;
    float prev_cursor_y = 120.0f;
    #define CURSOR_SPEED 5.0f
    #define ARCHIE_X 70
    #define ARCHIE_Y 100
    #define ARCHIE_W 60
    #define ARCHIE_H 60
    #define FISH_W 20
    #define FISH_H 15
    #define BIN_X 180
    #define BIN_Y 150
    #define BIN_W 25
    #define BIN_H 25

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

        // Check if cursor is over Archie or bin for interaction
        uint8_t over_archie = (cursor_x >= ARCHIE_X && cursor_x <= ARCHIE_X + ARCHIE_W && cursor_y >= ARCHIE_Y && cursor_y <= ARCHIE_Y + ARCHIE_H);
        uint8_t over_bin = (cursor_x >= BIN_X && cursor_x <= BIN_X + BIN_W && cursor_y >= BIN_Y && cursor_y <= BIN_Y + BIN_H);

        // Check if cursor actually moved this frame
        uint8_t cursor_moved = ((int16_t)cursor_x != (int16_t)prev_cursor_x || (int16_t)cursor_y != (int16_t)prev_cursor_y);
        prev_cursor_x = cursor_x; // Update previous cursor position
        prev_cursor_y = cursor_y;

        // Check if cursor is over menu button
        uint8_t over_menu = (cursor_x >= 3 && cursor_x <= 54 && cursor_y >= 8 && cursor_y <= 25);
        if (current_input.btn3_pressed && over_menu) {
            exit_state = MENU_STATE_HOME;
            break;
        }

        // Check if cursor over info button
        uint8_t over_info = (cursor_x >= 186 && cursor_x <= 237 && cursor_y >= 8 && cursor_y <= 25);
        if (current_input.btn3_pressed && over_info) {
            phase = PHASE_INSTRUCTIONS;
        }


        //only run game logic if we're in the playing phase
        if (phase == PHASE_PLAYING) {
            
            // Respawn fish only if no inactive slots are waiting to become bones
            if (fish_respawn_pending && HAL_GetTick() - fish_respawn_timer > 3000) {
                uint8_t bones_pending = 0;
                for (int i = 0; i < MAX_ITEMS; i++) {
                    if (!items[i].active && items[i].type == ITEM_BONES) {
                        bones_pending = 1;
                        break;
                    }
                }
                if (!bones_pending) {
                    for (int i = 0; i < MAX_ITEMS; i++) {
                        if (!items[i].active && items[i].type == ITEM_NONE) {
                            items[i].x = 20; items[i].y = 150;
                            items[i].type = ITEM_FISH;
                            items[i].active = 1;
                            fish_respawn_pending = 0;
                            break;
                        }
                    }
                }
            }

            // Bones appear after 3 seconds at drop point
            for (int i = 0; i < MAX_ITEMS; i++) {
                if (items[i].type == ITEM_BONES && !items[i].active) {
                    if (HAL_GetTick() - items[i].spawn_time > 3000) {
                        items[i].active = 1;
                    }
                }
            }

            // Carried item follows cursor
            if (carried_index >= 0) {
                items[carried_index].x = cursor_x;
                items[carried_index].y = cursor_y;
            }

            // stat bars decay every 3 seconds
            if (HAL_GetTick() - last_decay > 3000) {
                archie.hunger    = MAX(archie.hunger    - 5, 0);
                archie.happiness = MAX(archie.happiness - 5, 0);
                archie.energy    = MAX(archie.energy    - 5, 0);
                last_decay = HAL_GetTick();
            }

            // Check stat thresholds every frame
            if (archie.hunger == 0 && archie.happiness == 0 && archie.energy == 0)
                event = EVENT_DEAD;
            else if (archie.hunger <= 20 || archie.happiness <= 20 || archie.energy <= 20)
                event = EVENT_STAT_EMPTY;
            else if (archie.hunger >= 90 && archie.happiness >= 90 && archie.energy >= 90)
                event = EVENT_STAT_FULL;

            // overwrite threshold events so interactions always take priority
            if (over_archie && cursor_moved)    event = EVENT_JOYSTICK;
            if (current_input.btn2_pressed)  event = EVENT_BTN_SLEEP;

            // Handle button press for picking up / dropping items
            if (current_input.btn3_pressed) {
                if (carried_index == -1) {
                    // Try to pick up
                    for (int i = 0; i < MAX_ITEMS; i++) {
                        if (items[i].active && cursor_x >= items[i].x && cursor_x <= items[i].x + FISH_W && cursor_y >= items[i].y && cursor_y <= items[i].y + FISH_H) {
                            carried_index = i;
                            // If picking up fish from home position, start respawn
                            if (items[i].type == ITEM_FISH && (int)items[i].x == 20 && (int)items[i].y == 150) {
                                fish_respawn_pending = 1;
                                fish_respawn_timer = HAL_GetTick();
                            }
                        break;
                        }
                    }
                } else {
                    // Drop carried item
                    if (over_archie && items[carried_index].type == ITEM_FISH) {
                        // Fed to Archie
                        float drop_x = cursor_x;
                        float drop_y = cursor_y;
                        items[carried_index].active = 0;
                        items[carried_index].type = ITEM_BONES;
                        items[carried_index].x = drop_x;
                        items[carried_index].y = drop_y;
                        items[carried_index].spawn_time = HAL_GetTick();
                        archie.hunger = MIN(archie.hunger + 30, 100);
                        event = EVENT_BTN_FEED;
                        carried_index = -1;
                    } else if (over_bin) {
                        // Binned permanently
                        items[carried_index].active = 0;
                        items[carried_index].type = ITEM_NONE;
                        carried_index = -1;
                    } else {
                        // Drop anywhere
                        carried_index = -1;
                    }
                }
            }

            FSM_Update(&archie, event);
        }


        // RENDER: Draw to LCD

        LCD_Fill_Buffer(0);
        LCD_Draw_Rect((uint16_t)cursor_x - 2, (uint16_t)cursor_y - 2, 5, 5, 1, 1); // Draw cursor (small cross)
        
        // Title
        LCD_printString("MeowPet", 60, 10, 1, 3);
    
        // Menu exit button
        LCD_Draw_Rect(3, 8, 51, 17, 2, 1);  // red filled rect
        LCD_printString("MENU", 5, 10, 1, 2);
        
        // Info button
        LCD_Draw_Rect(186, 8, 51, 17, 4, 1);  // blue filled
        LCD_printString("INFO", 190, 10, 1, 2);

        LCD_Draw_Rect(ARCHIE_X, ARCHIE_Y, ARCHIE_W, ARCHIE_H, 1, 0); // for testing archie position, replace with sprite later

        // TODO: replace with sprite draw calls
        switch (archie.state) {
            case STATE_IDLE:     LCD_printString("Archie: idle",     40, 100, 1, 2); break;
            case STATE_EATING:   LCD_printString("Archie: eating",   40, 100, 1, 2); break;
            case STATE_SLEEPING: LCD_printString("Archie: sleeping", 40, 100, 1, 2); break;
            case STATE_PLAYING:  LCD_printString("Archie: playing",  40, 100, 1, 2); break;
            case STATE_UNWELL:   LCD_printString("Archie: unwell",   40, 100, 1, 2); break;
            case STATE_HAPPY:    LCD_printString("Archie: happy!",   40, 100, 1, 2); break;
            case STATE_DYING:   LCD_printString("Archie: dying:(",   40, 100, 1, 2); break;
            case STATE_DEAD:    LCD_printString("Archie: dead...",   40, 100, 1, 2); break;
        }

        // Blink effect for dying state
        if (archie.state == STATE_DYING) {
            if (HAL_GetTick() - blink_timer > 500) {
                blink_visible = !blink_visible;
                blink_timer = HAL_GetTick();
           }
        } else {
            blink_visible = 1;  // always visible in non-dying states   
        }

        // Instructions screen
        if (phase == PHASE_INSTRUCTIONS) {
            LCD_Fill_Buffer(0);

            // Menu button
            LCD_Draw_Rect(3, 8, 51, 17, 2, 1);
            LCD_printString("MENU", 5, 10, 1, 2);

            // Title
            LCD_printString("INSTRUCTIONS", 20, 35, 1, 2);

            // Instructions text
            LCD_printString("Take care of Archie", 10, 65, 1, 1);
            LCD_printString("the cat!", 10, 78, 1, 1);
            LCD_printString("Feed him fish, but make", 10, 95, 1, 1);
            LCD_printString("sure to tidy the bones", 10, 108, 1, 1);
            LCD_printString("he spits out!", 10, 121, 1, 1);
            LCD_printString("Pet him with the joystick", 10, 138, 1, 1);
            LCD_printString("Press btn2 to sleep", 10, 155, 1, 1);
            LCD_printString("Beware! If Archie's stats", 10, 172, 1, 1);
            LCD_printString("get too low, there will", 10, 185, 1, 1);
            LCD_printString("be consequences...", 10, 198, 1, 1);

            // Play / Resume button
            LCD_Draw_Rect(70, 218, 100, 23, 3, 1);
            if (game_started == 0) {
                LCD_printString("Play!", 103, 222, 0, 2);
            } else {
                LCD_printString("Resume", 90, 222, 0, 2);
            }

            // Cursor
            LCD_Draw_Rect((uint16_t)cursor_x - 2, (uint16_t)cursor_y - 2, 5, 5, 1, 1);

            // Menu button check
            uint8_t over_menu_inst = (cursor_x >= 3 && cursor_x <= 54 &&
                                    cursor_y >= 8 && cursor_y <= 25);
            if (current_input.btn3_pressed && over_menu_inst) {
                exit_state = MENU_STATE_HOME;
                break;
            }

            // Play/Resume button check
            uint8_t over_play = (cursor_x >= 70 && cursor_x <= 170 &&
                                cursor_y >= 218 && cursor_y <= 236);
            if (current_input.btn3_pressed && over_play) {
                phase = PHASE_PLAYING;
                game_started = 1;
            }

            LCD_Refresh(&cfg0);
            uint32_t frame_time = HAL_GetTick() - frame_start;
            if (frame_time < GAME2_FRAME_TIME_MS) HAL_Delay(GAME2_FRAME_TIME_MS - frame_time);
            continue;
        }

        // Dead screen overlay w restart button
        if (archie.state == STATE_DEAD) {
            // Dark overlay
            LCD_Fill_Buffer(0);
            LCD_printString("Archie has", 60, 30, 2, 2);
            LCD_printString("passed away...", 30, 55, 2, 2);

            // Archie placeholder (gravestone - grey rectangle for now)
            LCD_Draw_Rect(ARCHIE_X, ARCHIE_Y, ARCHIE_W, ARCHIE_H, 13, 1);  // grey filled
            LCD_printString("RIP", ARCHIE_X + 18, ARCHIE_Y + 22, 0, 2);

            // Play Again button
            LCD_Draw_Rect(70, 185, 100, 20, 4, 1);  // blue filled
            LCD_printString("Play Again?", 75, 190, 1, 2);

            // Cursor
            LCD_Draw_Rect((uint16_t)cursor_x - 2, (uint16_t)cursor_y - 2, 5, 5, 1, 1);

            // Check if cursor over Play Again button and btn3 pressed
            uint8_t over_restart = (cursor_x >= 70 && cursor_x <= 170 && cursor_y >= 185 && cursor_y <= 205);
            if (current_input.btn3_pressed && over_restart) {
                // Reset everything
                FSM_Init(&archie);
                for (int i = 0; i < MAX_ITEMS; i++) {
                    items[i].active = 0;
                    items[i].type = ITEM_NONE;
                }
                items[0].x = 20; items[0].y = 150;
                items[0].type = ITEM_FISH; items[0].active = 1;
                carried_index = -1;
                fish_respawn_pending = 0;
                cursor_x = 120.0f;
                cursor_y = 120.0f;
            }

            LCD_Refresh(&cfg0);
            // Skip rest of render
            uint32_t frame_time = HAL_GetTick() - frame_start;
            if (frame_time < GAME2_FRAME_TIME_MS) HAL_Delay(GAME2_FRAME_TIME_MS - frame_time);
            continue;
        }

        // Stat bars
        LCD_printString("Hunger:", 10, 40, 1, 1);
        if (archie.state == STATE_DYING && !blink_visible) 
            Draw_Stat_Bar(70, 40, 100,    2);  // flash red when dying
        else 
            Draw_Stat_Bar(70, 40, archie.hunger,    5); 

        LCD_printString("Happiness:", 10, 55, 1, 1);
        if (archie.state == STATE_DYING && !blink_visible) 
            Draw_Stat_Bar(70, 55, 100, 2);
        else 
            Draw_Stat_Bar(70, 55, archie.happiness, 3);

        LCD_printString("Energy:", 10, 70, 1, 1);
        if (archie.state == STATE_DYING && !blink_visible) 
            Draw_Stat_Bar(70, 70, 100,    2);
        else 
            Draw_Stat_Bar(70, 70, archie.energy,    4);

        // Draw bin (always visible)
        LCD_Draw_Rect(BIN_X, BIN_Y, BIN_W, BIN_H, 5, 0);  // orange outline
        LCD_printString("BIN", BIN_X + 3, BIN_Y + 8, 5, 1);

        // Draw food items
        for (int i = 0; i < MAX_ITEMS; i++) {
            if (items[i].active) {
                uint8_t colour = (items[i].type == ITEM_FISH) ? 2 : 4;  // red=fish, blue=bones
                LCD_Draw_Rect((uint16_t)items[i].x, (uint16_t)items[i].y, FISH_W, FISH_H, colour, 1);
            }
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
