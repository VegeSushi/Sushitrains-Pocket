#include <gba.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    PLAY
};

class GameContext {
public:
    GameState state;
    int selected_option;
    bool needs_redraw;
    float train_position; 
    float speed;

    GameContext() : state(GameState::MENU), selected_option(0), needs_redraw(true), train_position(0.0f), speed(0.0f) {}

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
            
            VBlankIntrWait();
            dmaCopy(backBuffer, (void*)VIDEO_BUFFER, SCREEN_WIDTH * SCREEN_HEIGHT * 2);

        }
    }

    return 0;
}