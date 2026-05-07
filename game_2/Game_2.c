#include "Game_2.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Buzzer.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include "Archie_Sprites.h"

extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t buzzer_cfg;  // Buzzer control
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data; // for reading joystick input

//@brief Game 2 Implementation - Student can modify

// Frame rate for this game (in milliseconds)
#define GAME2_FRAME_TIME_MS 30

// stat bar limits 0-100, and to make code cleaner in FSM_Update
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

// FSM functions
void FSM_Init(Archie_t *cat) {
    cat->state = STATE_IDLE;
    cat->hunger = 80;
    cat->happiness = 80;
    cat->energy = 80;
    cat->state_timer = HAL_GetTick();
}

void FSM_Update(Archie_t *cat, CatEvent event) {
    switch (cat->state) {
        case STATE_IDLE:
            if (event == EVENT_DEAD){ 
                cat->dying_timer = HAL_GetTick();
                cat->state = STATE_DYING;        
            }
            else if (event == EVENT_STAT_EMPTY){ 
                cat->state = STATE_UNWELL; }
            else if (event == EVENT_BTN_FEED){ 
                cat->state = STATE_EATING; }
            else if (event == EVENT_BTN_SLEEP){ 
                cat->state = STATE_SLEEPING; }
            else if (event == EVENT_JOYSTICK){ 
                cat->state = STATE_PLAYING; }
            else if (event == EVENT_STAT_FULL){ 
                cat->state = STATE_HAPPY; }
            break;
        case STATE_EATING:
            if (HAL_GetTick() - cat->state_timer > 3000) { 
                cat->state = STATE_IDLE; }
            break;
        case STATE_SLEEPING:
            cat->energy = MIN(cat->energy + 1, 100);
            if (event == EVENT_BTN_SLEEP || cat->energy >= 100) { 
                cat->state = STATE_IDLE; }
            break;
        case STATE_PLAYING:
            cat->happiness = MIN(cat->happiness + 1, 100);
            if (event != EVENT_JOYSTICK) { 
                cat->state = STATE_IDLE; }
            break;
        case STATE_UNWELL:
            if (event == EVENT_DEAD)            { 
                cat->dying_timer = HAL_GetTick();
                cat->state = STATE_DYING;       
            }
            else if (event == EVENT_BTN_FEED) { 
                cat->state = STATE_EATING; }
            else if (event == EVENT_BTN_SLEEP) { 
                cat->state = STATE_SLEEPING; }
            else if (event == EVENT_JOYSTICK) { 
                cat->state = STATE_PLAYING; }
            break;
        case STATE_HAPPY:
            // only leave happy if stats drop below 20/unwell
            if (cat->hunger < 90 || cat->happiness < 90 || cat->energy < 90)
                cat->state = STATE_IDLE;
            if (event == EVENT_STAT_EMPTY) { 
                cat->state = STATE_UNWELL; }
            if (event == EVENT_BTN_FEED) { 
                cat->state = STATE_EATING; }
            if (event == EVENT_BTN_SLEEP) { 
                cat->state = STATE_SLEEPING; }
            if (event == EVENT_JOYSTICK) { 
                cat->state = STATE_PLAYING; }
            break; 
        case STATE_DYING:
            // happiness boost from being saved by player interaction
            if (event == EVENT_BTN_FEED) {
                cat->hunger = MIN(cat->hunger + 20, 100);
                cat->happiness = MIN(cat->happiness + 10, 100);
                cat->state = STATE_UNWELL;
            } else if (event == EVENT_BTN_SLEEP) {
                cat->energy = MIN(cat->energy + 20, 100);
                cat->happiness = MIN(cat->happiness + 10, 100);
                cat->state = STATE_UNWELL;
            } else if (event == EVENT_JOYSTICK) {
                cat->happiness = MIN(cat->happiness + 20, 100);
                cat->state = STATE_UNWELL;
            }
            // 5 second grace period expired
            if (HAL_GetTick() - cat->dying_timer > 5000) {
                cat->state = STATE_DEAD;
            }
            break;
        case STATE_DEAD:
            //handled in game loop render/restart
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
// 0-100
void Draw_Stat_Bar(uint16_t x, uint16_t y, uint8_t value, uint8_t bar_colour) {
    uint16_t max_width = 80;
    uint16_t height = 8;

    // background (empty bar)
    LCD_Draw_Rect(x, y, max_width, height, 13, 1);  // 13=grey
    // filled inside
    uint16_t filled = (uint16_t)(value * max_width / 100);
    if (filled > 0) {
        LCD_Draw_Rect(x, y, filled, height, bar_colour, 1);
    }
    // outline on top
    LCD_Draw_Rect(x, y, max_width, height, 1, 0);
}

MenuState Game2_Run(void) {

    Archie_t archie;
    FSM_Init(&archie);
    uint32_t last_decay = HAL_GetTick();

    GamePhase phase = PHASE_INSTRUCTIONS;
    uint8_t game_started = 0;

    // food item variables
    FoodItem_t items[MAX_ITEMS] = {0};
    items[0].x = 5; 
    items[0].y = 170;
    items[0].type = ITEM_FISH; 
    items[0].active = 1;
    int8_t carried_index = -1;
    uint32_t fish_respawn_timer = HAL_GetTick();
    uint8_t fish_respawn_pending = 0;
    uint32_t blink_timer = HAL_GetTick();
    uint8_t blink_visible = 1;
    uint8_t sleep_start_energy = 80; // set when sleep begins

    // mapping everything
    float cursor_x = 120.0f; // start cursor in centre of screen
    float cursor_y = 120.0f;
    float prev_cursor_x = 120.0f;
    float prev_cursor_y = 120.0f;
    #define CURSOR_SPEED 7.0f
    #define ARCHIE_X 62
    #define ARCHIE_Y 90
    #define ARCHIE_W 128
    #define ARCHIE_H 128
    #define FISH_W 64
    #define FISH_H 64
    #define BIN_X 170
    #define BIN_Y 150
    #define BIN_W 96
    #define BIN_H 96

    // Play a brief startup sound
    buzzer_tone(&buzzer_cfg, 1200, 30);  // 1.2kHz at 30% volume
    HAL_Delay(50);  // Brief beep duration
    buzzer_off(&buzzer_cfg);  // Stop the buzzer
    
    MenuState exit_state = MENU_STATE_HOME;  // Default: return to menu

    // Game's own loop - runs until exit condition
    while (1) {
        uint32_t frame_start = HAL_GetTick();

        // FSM events
        CatEvent event = EVENT_NONE;

        // Read input
        Input_Read();
        
        // read joystick and move cursor
        Joystick_Read(&joystick_cfg, &joystick_data);
        cursor_x += joystick_data.coord_mapped.x * CURSOR_SPEED;
        cursor_y -= joystick_data.coord_mapped.y * CURSOR_SPEED;  // y inverted on screen

        // clamp cursor
        if (cursor_x < 2)   cursor_x = 2;
        if (cursor_x > 238) cursor_x = 238;
        if (cursor_y < 2)   cursor_y = 2;
        if (cursor_y > 238) cursor_y = 238;

        // CHECKS
        
        const uint8_t* current_sprite = archie_idle;
        if (archie.state == STATE_PLAYING)
            current_sprite = archie_playing;
        if (archie.state == STATE_UNWELL)
            current_sprite = archie_unwell;
        if (archie.state == STATE_HAPPY)
            current_sprite = archie_happy;

        // checking if cursor is over archie, ignores transparent pixels
        uint8_t over_archie = 0;
        if (cursor_x >= ARCHIE_X && cursor_x <= ARCHIE_X + ARCHIE_SPRITE_W * 4 && cursor_y >= ARCHIE_Y && cursor_y <= ARCHIE_Y + ARCHIE_SPRITE_H * 4) {
            int sprite_px = ((int)cursor_x - ARCHIE_X) / 4;
            int sprite_py = ((int)cursor_y - ARCHIE_Y) / 4;
            if (sprite_px >= 0 && sprite_px < 32 && sprite_py >= 0 && sprite_py < 32) {
                uint8_t pixel = current_sprite[sprite_py * 32 + sprite_px];
                over_archie = (pixel != 255);
            }
        }

        // check if cursor is over bin
        uint8_t over_bin = (cursor_x >= BIN_X && cursor_x <= BIN_X + BIN_W && cursor_y >= BIN_Y && cursor_y <= BIN_Y + BIN_H);

        // check if cursor moved - used for petting cat mechanism
        uint8_t cursor_moved = ((int16_t)cursor_x != (int16_t)prev_cursor_x || (int16_t)cursor_y != (int16_t)prev_cursor_y);
        prev_cursor_x = cursor_x; // Update previous cursor position
        prev_cursor_y = cursor_y;

        // check if cursor is over menu button
        uint8_t over_menu = (cursor_x >= 3 && cursor_x <= 54 && cursor_y >= 8 && cursor_y <= 25);
        if (current_input.btn3_pressed && over_menu) {
            exit_state = MENU_STATE_HOME;
            break;
        }

        // check if cursor over info button
        uint8_t over_info = (cursor_x >= 186 && cursor_x <= 237 && cursor_y >= 8 && cursor_y <= 25);
        if (current_input.btn3_pressed && over_info) {
            phase = PHASE_INSTRUCTIONS;
        }


        //only run game logic if we're in the playing phase
        if (phase == PHASE_PLAYING) {
            
            // respawn fish only if no inactive slots waiting to become bones
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
                            items[i].x = 5; items[i].y = 170;
                            items[i].type = ITEM_FISH;
                            items[i].active = 1;
                            fish_respawn_pending = 0;
                            break;
                        }
                    }
                }
            }

            // bones spawn after 3 seconds where fish was eaten
            for (int i = 0; i < MAX_ITEMS; i++) {
                if (items[i].type == ITEM_BONES && !items[i].active) {
                    if (HAL_GetTick() - items[i].spawn_time > 3000) {
                        items[i].active = 1;
                    }
                }
            }

            // carried item follows cursor
            if (carried_index >= 0) {
                items[carried_index].x = cursor_x - 32;
                items[carried_index].y = cursor_y - 32;
            }

            // stat bars decay every 3 seconds
            if (HAL_GetTick() - last_decay > 3000) {
                archie.hunger = MAX(archie.hunger - 2, 0);
                archie.happiness = MAX(archie.happiness - 2, 0);
                archie.energy = MAX(archie.energy - 2, 0);
                last_decay = HAL_GetTick();
            }

            // check stat thresholds every frame for event
            if (archie.hunger == 0 && archie.happiness == 0 && archie.energy == 0)
                event = EVENT_DEAD;
            else if (archie.hunger <= 20 || archie.happiness <= 20 || archie.energy <= 20)
                event = EVENT_STAT_EMPTY;
            else if (archie.hunger >= 90 && archie.happiness >= 90 && archie.energy >= 90)
                event = EVENT_STAT_FULL;

            // overwrite threshold events so interactions always take priority
            if (over_archie && cursor_moved) 
                event = EVENT_JOYSTICK;
            if (current_input.btn2_pressed)
                event = EVENT_BTN_SLEEP;

            // button press for picking up/dropping items
            if (current_input.btn3_pressed) {
                if (carried_index == -1) {
                    // pick up
                    for (int i = 0; i < MAX_ITEMS; i++) {
                        if (items[i].active && cursor_x >= items[i].x && cursor_x <= items[i].x + FISH_W && cursor_y >= items[i].y && cursor_y <= items[i].y + FISH_H) {
                            carried_index = i;
                            // if picking up fish, start respawn
                            if (items[i].type == ITEM_FISH && (int)items[i].x == 5 && (int)items[i].y == 170) {
                                fish_respawn_pending = 1;
                                fish_respawn_timer = HAL_GetTick();
                            }
                        break;
                        }
                    }
                } else {
                    // drop carried item
                    if (over_archie && items[carried_index].type == ITEM_FISH) {
                        // fed to archie
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
                        // binned permanently
                        items[carried_index].active = 0;
                        items[carried_index].type = ITEM_NONE;
                        carried_index = -1;
                    } else {
                        // drop anywhere
                        carried_index = -1;
                    }
                }
            }

            FSM_Update(&archie, event);

            // record energy at moment sleep begins for calculating sleep animation frames later
            static CatState prev_state_render = STATE_IDLE;
            if (archie.state == STATE_SLEEPING && prev_state_render != STATE_SLEEPING) {
                sleep_start_energy = archie.energy;
            }
            prev_state_render = archie.state;
        }


        // RENDER: Draw to LCD

        LCD_Fill_Buffer(0);

        // Background
        const uint8_t* bg = bg_day;
        if (archie.state == STATE_SLEEPING) 
            bg = bg_night;
        if (archie.state == STATE_DYING) 
            bg = bg_dying;
        LCD_Draw_Sprite_Scaled(0, 0, 30, 30, bg, 8);
        
        // Title
        LCD_printString("MeowPet", 60, 10, 1, 3);
    
        // menu exit button
        LCD_Draw_Rect(3, 8, 51, 17, 2, 1);  // red
        LCD_printString("MENU", 5, 10, 1, 2);
        
        // info button
        LCD_Draw_Rect(188, 8, 51, 17, 4, 1);  // blue
        LCD_printString("INFO", 190, 10, 1, 2);

        // state switch! for all the diff sprites and animations
        switch (archie.state) {
            case STATE_IDLE:
                LCD_Draw_Sprite_Scaled(ARCHIE_X, ARCHIE_Y, ARCHIE_SPRITE_H, ARCHIE_SPRITE_W, archie_idle, 4);
                break;
            case STATE_EATING: {
                uint8_t eating_frame = ((HAL_GetTick() - archie.state_timer) / 250) % 2; // switch every 250ms for chomping
                if (eating_frame == 0)
                    LCD_Draw_Sprite_Scaled(ARCHIE_X, ARCHIE_Y, ARCHIE_SPRITE_H, ARCHIE_SPRITE_W, archie_eating_open, 4);
                else
                    LCD_Draw_Sprite_Scaled(ARCHIE_X, ARCHIE_Y, ARCHIE_SPRITE_H, ARCHIE_SPRITE_W, archie_eating_closed, 4);
                break;
            }
            case STATE_SLEEPING: {
                uint8_t energy_gained = archie.energy - sleep_start_energy;
                uint8_t energy_range = 100 - sleep_start_energy;
                if (energy_range == 0) 
                    energy_range = 1; // dont divide by zero
                uint8_t frame = (energy_gained * 4) / energy_range;
                if (frame > 3) 
                    frame = 3;// cap max frame
                LCD_Draw_Sprite_Scaled(ARCHIE_X, ARCHIE_Y, ARCHIE_SPRITE_H, ARCHIE_SPRITE_W, archie_sleeping[frame], 4);
                break;
            }
            case STATE_PLAYING: 
                LCD_Draw_Sprite_Scaled(ARCHIE_X, ARCHIE_Y, ARCHIE_SPRITE_H, ARCHIE_SPRITE_W, archie_playing, 4);
                break;
            case STATE_UNWELL:
                LCD_Draw_Sprite_Scaled(ARCHIE_X, ARCHIE_Y, ARCHIE_SPRITE_H, ARCHIE_SPRITE_W, archie_unwell, 4);
                break;
            case STATE_HAPPY:
                LCD_Draw_Sprite_Scaled(ARCHIE_X, ARCHIE_Y, ARCHIE_SPRITE_H, ARCHIE_SPRITE_W, archie_happy, 4);
                break;
            case STATE_DYING:
                if (blink_visible) { // blink w bars
                    LCD_Draw_Sprite_Scaled(ARCHIE_X, ARCHIE_Y, ARCHIE_SPRITE_H, ARCHIE_SPRITE_W, archie_unwell, 4);
                }
                break;
            case STATE_DEAD:
                LCD_Draw_Sprite_Scaled(ARCHIE_X, ARCHIE_Y, ARCHIE_SPRITE_H, ARCHIE_SPRITE_W, archie_grave, 4);
                break;
            }

        // bars blinking effect
        if (archie.state == STATE_DYING) {
            if (HAL_GetTick() - blink_timer > 500) {
                blink_visible = !blink_visible;
                blink_timer = HAL_GetTick();
           }
        } else {
            blink_visible = 1;  // set back to 1  
        }

        // instructions/info screen
        if (phase == PHASE_INSTRUCTIONS) {
            LCD_Fill_Buffer(0);

            // menu button
            LCD_Draw_Rect(3, 8, 51, 17, 2, 1);
            LCD_printString("MENU", 5, 10, 1, 2);

            // title
            LCD_printString("INSTRUCTIONS", 20, 35, 1, 2);

            // text
            LCD_printString("Take care of Archie", 10, 65, 1, 1);
            LCD_printString("the cat!", 10, 78, 1, 1);
            LCD_printString("Feed him fish, but make", 10, 95, 1, 1);
            LCD_printString("sure to tidy the bones", 10, 108, 1, 1);
            LCD_printString("he spits out!", 10, 121, 1, 1);
            LCD_printString("Pet him with the joystick", 10, 138, 1, 1);
            LCD_printString("Press btn2 to sleep", 10, 155, 1, 1);
            LCD_printString("Beware! If Archie's stats", 10, 172, 1, 1);
            LCD_printString("get too low, there will", 10, 185, 1, 1);
            LCD_printString("be consequences...", 10, 198, 2, 1);

            // play/resume button
            LCD_Draw_Rect(70, 218, 100, 23, 15, 1);
            if (game_started == 0) {
                LCD_printString("Play!", 90, 222, 0, 2);
            } else {
                LCD_printString("Resume", 80, 222, 0, 2);
            }

            // menu button check
            uint8_t over_menu_inst = (cursor_x >= 3 && cursor_x <= 54 && cursor_y >= 8 && cursor_y <= 25);
            if (current_input.btn3_pressed && over_menu_inst) {
                exit_state = MENU_STATE_HOME;
            break;
            }

            // play/resume button check
            uint8_t over_play = (cursor_x >= 70 && cursor_x <= 170 && cursor_y >= 218 && cursor_y <= 236);
            if (current_input.btn3_pressed && over_play) {
                phase = PHASE_PLAYING;
                game_started = 1;
            }

            // cursor
            LCD_Draw_Rect((uint16_t)cursor_x - 2, (uint16_t)cursor_y - 2, 5, 5, 1, 1);

            LCD_Refresh(&cfg0);
            uint32_t frame_time = HAL_GetTick() - frame_start;
            if (frame_time < GAME2_FRAME_TIME_MS) 
                HAL_Delay(GAME2_FRAME_TIME_MS - frame_time);
            continue;
        }

        // dead screen overlay w restart button
        if (archie.state == STATE_DEAD) {
            // overlay
            LCD_Fill_Buffer(0);
            LCD_printString("Archie has", 60, 30, 2, 2);
            LCD_printString("passed away...", 30, 55, 2, 2);

            // grave sprite
            LCD_Draw_Sprite_Scaled(65, 70, 32, 32, archie_grave, 3);

            // play again button
            LCD_Draw_Rect(45, 185, 140, 20, 4, 1);  // blue
            LCD_printString("Play Again?", 50, 188, 1, 2);

            // check if cursor over Play Again button and btn3 pressed
            uint8_t over_restart = (cursor_x >= 45 && cursor_x <= 185 && cursor_y >= 185 && cursor_y <= 205);
            if (current_input.btn3_pressed && over_restart) {
                // reset everything
                FSM_Init(&archie);
                for (int i = 0; i < MAX_ITEMS; i++) {
                    items[i].active = 0;
                    items[i].type = ITEM_NONE;
                }
                items[0].x = 5; 
                items[0].y = 170;
                items[0].type = ITEM_FISH; 
                items[0].active = 1;
                carried_index = -1;
                fish_respawn_pending = 0;
                cursor_x = 120.0f;
                cursor_y = 120.0f;
            }

            // cursor
            LCD_Draw_Rect((uint16_t)cursor_x - 2, (uint16_t)cursor_y - 2, 5, 5, 1, 1);

            LCD_Refresh(&cfg0);

            // Skip rest of render
            uint32_t frame_time = HAL_GetTick() - frame_start;
            if (frame_time < GAME2_FRAME_TIME_MS) 
            HAL_Delay(GAME2_FRAME_TIME_MS - frame_time);
            continue;
        }

        // stat bars and labels
        LCD_printString("Hunger:", 10, 40, 1, 1);
        if (archie.state == STATE_DYING && !blink_visible) 
            Draw_Stat_Bar(70, 40, 100, 2);  // flash red when dying
        else 
            Draw_Stat_Bar(70, 40, archie.hunger, 5); 

        LCD_printString("Happiness:", 10, 55, 1, 1);
        if (archie.state == STATE_DYING && !blink_visible) 
            Draw_Stat_Bar(70, 55, 100, 2);
        else 
            Draw_Stat_Bar(70, 55, archie.happiness, 3);

        LCD_printString("Energy:", 10, 70, 1, 1);
        if (archie.state == STATE_DYING && !blink_visible) 
            Draw_Stat_Bar(70, 70, 100, 2);
        else 
            Draw_Stat_Bar(70, 70, archie.energy, 4);

        // bin
        if (carried_index >= 0 && over_bin)
            LCD_Draw_Sprite_Scaled(BIN_X, BIN_Y, 32, 32, open_bin, 2);
        else
            LCD_Draw_Sprite_Scaled(BIN_X, BIN_Y, 32, 32, closed_bin, 2);

        // fish and bones
        for (int i = 0; i < MAX_ITEMS; i++) {
            if (items[i].active) {
                if (items[i].type == ITEM_FISH)
                    LCD_Draw_Sprite_Scaled((uint16_t)items[i].x, (uint16_t)items[i].y, 32, 32, fish, 2);
                else
                    LCD_Draw_Sprite_Scaled((uint16_t)items[i].x, (uint16_t)items[i].y, 32, 32, bones, 2);
            }
        }
        
        // cursor
        LCD_Draw_Rect((uint16_t)cursor_x - 2, (uint16_t)cursor_y - 2, 5, 5, 1, 1);

        LCD_Refresh(&cfg0);
        
        // Frame timing - wait for remainder of frame time
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < GAME2_FRAME_TIME_MS) {
            HAL_Delay(GAME2_FRAME_TIME_MS - frame_time);
        }
    }

    return exit_state;  // Tell main where to go next
}