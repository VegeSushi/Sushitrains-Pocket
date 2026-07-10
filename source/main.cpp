#include <gba.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --------------------------------------------------------
// HARDWARE DEFINES & GRAPHICS FUNCTIONS
// --------------------------------------------------------
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

// FIXED: Use libgba's VRAM address, but cast it so we can write to it
#define VIDEO_BUFFER ((volatile u16*)VRAM) 

u16* backBuffer;

void drawPixel(int x, int y, u16 color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        // FIXED: Now writes to our invisible canvas
        backBuffer[y * SCREEN_WIDTH + x] = color; 
    }
}

// Standard Bresenham's line algorithm for the GBA
void drawLine(int x0, int y0, int x1, int y1, u16 color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void drawBackground() {
    u16 skyColor = RGB5(14, 22, 31);
    u16 grassColor = RGB5(8, 22, 8);

    int i = 0;
    for (; i < SCREEN_WIDTH * 60; i++) {
        // FIXED: Now writes to our invisible canvas
        backBuffer[i] = skyColor;
    }
    
    for (; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        // FIXED: Now writes to our invisible canvas
        backBuffer[i] = grassColor;
    }
}

void fillRect(int x, int y, int w, int h, u16 color) {
    for (int iy = 0; iy < h; iy++) {
        for (int ix = 0; ix < w; ix++) {
            drawPixel(x + ix, y + iy, color);
        }
    }
}

// Draws an empty box outline
void drawRectOutline(int x, int y, int w, int h, u16 color) {
    drawLine(x, y, x + w, y, color);
    drawLine(x, y + h, x + w, y + h, color);
    drawLine(x, y, x, y + h, color);
    drawLine(x + w, y, x + w, y + h, color);
}

void clearScreen(u16 color) {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        VIDEO_BUFFER[i] = color;
    }
}

// --------------------------------------------------------
// MAP DATA FORMAT
// --------------------------------------------------------
struct MapSegment {
    float length; // How long the train drives on this segment
    float curve;  // Negative = left curve, Positive = right curve, 0 = straight
};

// Easily modifiable track layout
const MapSegment trackMap[] = {
    { 150.0f,  0.0f },   // Start straight
    { 200.0f, -0.04f },  // Sharp left turn
    { 100.0f,  0.0f },   // Straightaway
    { 250.0f,  0.02f },  // Gentle right curve
    { 150.0f, -0.01f },  // Slight left
    { 300.0f,  0.0f }    // Long straight to finish
};
const int MAP_SEGMENTS = sizeof(trackMap) / sizeof(MapSegment);

// Helper function to look up the track curve at a specific distance
float getCurveAtDistance(float distance) {
    float current_dist = 0;
    for (int i = 0; i < MAP_SEGMENTS; i++) {
        current_dist += trackMap[i].length;
        if (distance < current_dist) {
            return trackMap[i].curve;
        }
    }
    return 0.0f; // Return straight if we run out of map
}

// --------------------------------------------------------
// GAME LOGIC & STATE MACHINE
// --------------------------------------------------------
enum class GameState {
    MENU,
    PLAY,
    DERAIL_ANIM,
    DERAIL
};

class GameContext {
public:
    GameState state;
    int selected_option;
    bool needs_redraw;
    float train_position; 
    float speed;
    int anim_timer;
    float total_map_length;

    GameContext() : state(GameState::MENU), selected_option(0), needs_redraw(true), train_position(0.0f), speed(0.0f) {
        total_map_length = 0.0f;
        for (int i = 0; i < MAP_SEGMENTS; i++) {
            total_map_length += trackMap[i].length;
        }
    }

    void switchState(GameState newState) {
        state = newState;
        needs_redraw = true;

        if (newState == GameState::MENU) {
            REG_DISPCNT = MODE_0 | BG0_ON;
            consoleDemoInit();
        } else if (newState == GameState::PLAY) {
            REG_DISPCNT = MODE_3 | BG2_ON;
            train_position = 0.0f;
            speed = 0.0f;
        } else if (newState == GameState::DERAIL) {
            REG_DISPCNT = MODE_0 | BG0_ON;
            consoleDemoInit();
        }
    }
};

int main() {
    backBuffer = (u16*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * 2);
    irqInit();
    irqEnable(IRQ_VBLANK);

    GameContext ctx;
    ctx.switchState(GameState::MENU); 

    while (true) {
        scanKeys();
        u16 keys = keysDown();
        u16 current_keys_held = keysHeld(); // FIXED: Changed from keysCurrent to keysHeld

        if (ctx.state == GameState::MENU) {
            if (ctx.needs_redraw) {
                iprintf("\x1b[2J"); 
                
                iprintf("\x1b[3;5H=== SUSHITRAINS ===");

                iprintf("\x1b[8;5H%c Start Shift", ctx.selected_option == 0 ? '>' : ' ');
                iprintf("\x1b[10;5H%c Options", ctx.selected_option == 1 ? '>' : ' ');
                
                ctx.needs_redraw = false;
            }

            if (keys & KEY_UP) {
                ctx.selected_option--;
                if (ctx.selected_option < 0) ctx.selected_option = 1; 
                ctx.needs_redraw = true;
            }
            if (keys & KEY_DOWN) {
                ctx.selected_option++;
                if (ctx.selected_option > 1) ctx.selected_option = 0; 
                ctx.needs_redraw = true;
            }

            if (keys & KEY_A) {
                if (ctx.selected_option == 0) {
                    ctx.switchState(GameState::PLAY);
                }
            }

        } else if (ctx.state == GameState::PLAY) {
            // 1. Logic Updates
            if (current_keys_held & KEY_UP) ctx.speed += 0.05f; 
            if (current_keys_held & KEY_DOWN) ctx.speed -= 0.1f; 
            if (ctx.speed < 0) ctx.speed = 0;
            if (ctx.speed > 5.0f) ctx.speed = 5.0f;

            ctx.train_position += ctx.speed;

            float current_curve = getCurveAtDistance(ctx.train_position);
        
            // We use standard C++ abs() to make negative (left) curves positive for the math
            float g_force = ctx.speed * abs(current_curve); 
        
            // A threshold of 0.10 means a sharp 0.04 curve has a max safe speed of 2.5
            if (g_force > 0.10f) {
                ctx.anim_timer = 60; 
                ctx.switchState(GameState::DERAIL_ANIM);
                continue; // Skip the rest of the drawing loop this frame
            }

            if (keys & KEY_B) {
                ctx.switchState(GameState::MENU);
                continue;
            }

            // 2. Rendering
            drawBackground(); // Draws our new sky and grass

            float track_x = 0;
            float track_dx = 0;
            float camera_y = 5.0f; 

            int prev_sx_l = -1, prev_sy = -1, prev_sx_r = -1;

            for (int i = 1; i < 30; i++) {
                float z = i * 2.0f; 
                
                float curve = getCurveAtDistance(ctx.train_position + z);
                track_dx += curve;
                track_x += track_dx;

                int sx_l = 120 + (int)((track_x - 3.0f) / z * 100.0f); 
                int sx_r = 120 + (int)((track_x + 3.0f) / z * 100.0f); 
                int sy   = 60 + (int)(camera_y / z * 100.0f);          

                if (sy >= SCREEN_HEIGHT) sy = SCREEN_HEIGHT - 1;

                if (prev_sy != -1) {
                    // Draw Rails (Bright Silver/White)
                    drawLine(prev_sx_l, prev_sy, sx_l, sy, RGB5(28, 28, 28));
                    drawLine(prev_sx_r, prev_sy, sx_r, sy, RGB5(28, 28, 28));
                    
                    // Draw Wooden Sleepers (Brown)
                    if ((i + (int)ctx.train_position) % 2 == 0) {
                        drawLine(sx_l, sy, sx_r, sy, RGB5(14, 9, 5));
                    }
                }

                prev_sx_l = sx_l;
                prev_sx_r = sx_r;
                prev_sy = sy;
            }

            int max_speed_height = 50;
            int current_speed_height = (int)((ctx.speed / 5.0f) * max_speed_height);
            
            // Color shifts based on speed (Green -> Yellow -> Red)
            u16 speedColor = RGB5(0, 31, 0); // Green
            if (ctx.speed > 2.0f) speedColor = RGB5(31, 31, 0); // Yellow
            if (ctx.speed > 3.5f) speedColor = RGB5(31, 0, 0);  // Red

            // Draw the speed bar growing upwards
            fillRect(10, 140 - current_speed_height, 10, current_speed_height, speedColor);
            drawRectOutline(9, 140 - max_speed_height, 12, max_speed_height + 1, RGB5(31, 31, 31));

            // 2. MINI-MAP / ROUTE PROGRESS (Top center horizontal bar)
            int map_width = 100;
            int map_x = (SCREEN_WIDTH / 2) - (map_width / 2); // Center it
            int map_y = 10;
            
            // Draw background track line
            fillRect(map_x, map_y, map_width, 4, RGB5(10, 10, 10));
            drawRectOutline(map_x - 1, map_y - 1, map_width + 2, 6, RGB5(20, 20, 20));

            // Calculate train blip position
            float progress = ctx.train_position / ctx.total_map_length;
            if (progress > 1.0f) progress = 1.0f; // Cap at 100%
            int train_dot_x = map_x + (int)(progress * (map_width - 4));
            
            // Draw train blip (Cyan)
            fillRect(train_dot_x, map_y - 1, 4, 6, RGB5(0, 31, 31));

            // 3. LOCAL TOP-DOWN MAP (Bottom right corner)
            int td_w = 40; // Smaller width
            int td_h = 50; // Slightly taller to see ahead
            int td_x = SCREEN_WIDTH - td_w - 5; 
            int td_y = SCREEN_HEIGHT - td_h - 5; 
            
            // Draw map background
            fillRect(td_x, td_y, td_w, td_h, RGB5(2, 2, 2));
            drawRectOutline(td_x - 1, td_y - 1, td_w + 2, td_h + 2, RGB5(15, 15, 15));

            float look_behind = 35.0f;
            float look_ahead = 35.0f;
            
            // Pass 1: Find the train's exact X drift to use as our camera center
            float train_sim_x = 0.0f;
            float temp_dx = 0.0f;
            for (float d = 0; d < ctx.train_position; d += 1.0f) {
                temp_dx += (getCurveAtDistance(d) * 2.5f);
                train_sim_x += temp_dx;
            }

            // Pass 2: Draw only the track window
            float sim_dx = 0.0f;
            float sim_x = 0.0f;
            int prev_map_x = -1, prev_map_y = -1;
            
            for (float d = 0; d < ctx.train_position + look_ahead; d += 1.0f) {
                sim_dx += (getCurveAtDistance(d) * 2.5f);
                sim_x += sim_dx;

                // Only draw if the track is within our 50m moving window
                if (d >= ctx.train_position - look_behind) {
                    // Map distance to Y axis (bottom = behind, top = ahead)
                    float window_pos = d - (ctx.train_position - look_behind);
                    int plot_y = td_y + td_h - (int)((window_pos / (look_ahead + look_behind)) * td_h);
                    
                    // Center the X axis around the train, scale down to fit the small box
                    int plot_x = td_x + (td_w / 2) + (int)((sim_x - train_sim_x) * 0.2f);
                    
                    // Clamp drawing to the box so it doesn't spill over
                    if (plot_x < td_x) plot_x = td_x;
                    if (plot_x >= td_x + td_w) plot_x = td_x + td_w - 1;

                    if (prev_map_x != -1) {
                        drawLine(prev_map_x, prev_map_y, plot_x, plot_y, RGB5(10, 31, 10)); // Green track
                    }
                    prev_map_x = plot_x;
                    prev_map_y = plot_y;
                }
            }
            
            // Draw the train exactly at its camera center
            int p_plot_y = td_y + td_h - (int)((look_behind / (look_ahead + look_behind)) * td_h);
            int p_plot_x = td_x + (td_w / 2);
            fillRect(p_plot_x - 1, p_plot_y - 1, 3, 3, RGB5(31, 0, 0));
            
            VBlankIntrWait();
            dmaCopy(backBuffer, (void*)VIDEO_BUFFER, SCREEN_WIDTH * SCREEN_HEIGHT * 2);

        } else if (ctx.state == GameState::DERAIL) {
            if (ctx.needs_redraw) {
                iprintf("\x1b[2J"); 
                iprintf("\x1b[7;3H!!! FATAL DERAILMENT !!!");
                iprintf("\x1b[9;3HYou took a curve too fast.");
                iprintf("\x1b[14;3HPress A to return to Menu");
                ctx.needs_redraw = false;
            }

            // Wait for player to press A to try again
            if (keys & KEY_A) {
                ctx.switchState(GameState::MENU);
            }
        } else if (ctx.state == GameState::DERAIL_ANIM) {
            ctx.anim_timer--;

            // When the 60 frames are up, switch to the final text screen
            if (ctx.anim_timer <= 0) {
                ctx.switchState(GameState::DERAIL);
                continue;
            }

            // Flash the screen red every 8 frames
            if (ctx.anim_timer % 8 < 4) {
                for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) backBuffer[i] = RGB5(31, 0, 0);
            } else {
                drawBackground();
            }

            // Generate violent random screen shake
            int shake_x = (rand() % 21) - 10; // Random number between -10 and +10
            int shake_y = (rand() % 11) - 5;

            float track_x = 0;
            float track_dx = 0;
            float camera_y = 1.0f; // Drop the camera down to the ground!

            int prev_sx_l = -1, prev_sy = -1, prev_sx_r = -1;

            for (int i = 1; i < 30; i++) {
                float z = i * 2.0f; 
                float curve = getCurveAtDistance(ctx.train_position + z);
                track_dx += curve;
                track_x += track_dx;

                // Add the shake offsets to the screen coordinates
                int sx_l = 120 + (int)((track_x - 3.0f) / z * 100.0f) + shake_x; 
                int sx_r = 120 + (int)((track_x + 3.0f) / z * 100.0f) + shake_x; 
                int sy   = 60 + (int)(camera_y / z * 100.0f) + shake_y;          

                if (sy >= SCREEN_HEIGHT) sy = SCREEN_HEIGHT - 1;

                if (prev_sy != -1) {
                    drawLine(prev_sx_l, prev_sy, sx_l, sy, RGB5(28, 28, 28));
                    drawLine(prev_sx_r, prev_sy, sx_r, sy, RGB5(28, 28, 28));
                    if ((i + (int)ctx.train_position) % 2 == 0) {
                        drawLine(sx_l, sy, sx_r, sy, RGB5(14, 9, 5));
                    }
                }

                prev_sx_l = sx_l; prev_sx_r = sx_r; prev_sy = sy;
            }

            VBlankIntrWait();
            dmaCopy(backBuffer, (void*)VIDEO_BUFFER, SCREEN_WIDTH * SCREEN_HEIGHT * 2);
        }
    }

    return 0;
}