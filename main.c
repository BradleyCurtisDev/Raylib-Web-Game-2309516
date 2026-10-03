#include "raylib.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

typedef enum GameState {
    STATE_TITLE,
    STATE_LOADING,
    STATE_PLAYING,
    STATE_GAMEOVER
} GameState;

static GameState state = STATE_TITLE;

static float prices[MAX_POINTS];
static float volumes[MAX_POINTS];
static int   pointCount = 0;

static float terrainY[MAX_SAMPLES];
static int   sampleCount = 0;
static float levelLength = 0.0f;
static bool  bullMarket = true;

static float cameraX = 0.0f;
static float stateTimer = 0.0f;

static unsigned int rngState = 12345;
static float RandFloat(void)
{
    rngState = rngState*1664525u + 1013904223u;
    return (float)((rngState >> 8) & 0xFFFF)/65535.0f;
}

// temporary map data for if i cant get the real money trends later on or if the api fails
static void LoadFallbackData(void)
{
    rngState = 12345;
    pointCount = 160;
    float p = 60000.0f;
    float drift = 0.0f;
    for (int i = 0; i < pointCount; i++)
    {
        if (i%20 == 0) drift = (RandFloat() - 0.5f)*0.03f;
        p *= 1.0f + drift + (RandFloat() - 0.5f)*0.02f;
        prices[i]  = p;
        volumes[i] = 50.0f + RandFloat()*150.0f;
    }
}

static void BuildTerrain(void)
{
    static float tmp[MAX_SAMPLES];

    float minP = prices[0], maxP = prices[0];
    for (int i = 1; i < pointCount; i++)
    {
        if (prices[i] < minP) minP = prices[i];
        if (prices[i] > maxP) maxP = prices[i];
    }
    float range = maxP - minP;
    if (range < 0.0001f) range = 1.0f;

    sampleCount = (pointCount - 1)*SUBDIV + 1;
    for (int i = 0; i < sampleCount; i++)
    {
        float f = (float)i/SUBDIV;
        int a = (int)f;
        if (a > pointCount - 2) a = pointCount - 2;
        float t = f - (float)a;
        float price = prices[a] + (prices[a + 1] - prices[a])*t;
        terrainY[i] = TERRAIN_BOTTOM - ((price - minP)/range)*(TERRAIN_BOTTOM - TERRAIN_TOP);
    }

    for (int i = 0; i < sampleCount; i++)
    {
        float sum = 0.0f;
        for (int k = -1; k <= 1; k++)
        {
            int j = i + k;
            if (j < 0) j = 0;
            if (j > sampleCount - 1) j = sampleCount - 1;
            sum += terrainY[j];
        }
        tmp[i] = sum/3.0f;
    }
    memcpy(terrainY, tmp, sizeof(float)*sampleCount);

    for (int i = 1; i < sampleCount; i++)
    {
        float d = terrainY[i] - terrainY[i - 1];
        if (d >  MAX_STEP) d =  MAX_STEP;
        if (d < -MAX_STEP) d = -MAX_STEP;
        terrainY[i] = terrainY[i - 1] + d;
        if (terrainY[i] < 60.0f)  terrainY[i] = 60.0f;
        if (terrainY[i] > 500.0f) terrainY[i] = 500.0f;
    }

    levelLength = (float)(sampleCount - 1)*SAMPLE_DX;
    bullMarket = (prices[pointCount - 1] >= prices[0]);
}

static float TerrainAtIndex(int i)
{
    if (i < 0) i = 0;
    if (i > sampleCount - 1) i = sampleCount - 1;
    return terrainY[i];
}

static float TerrainHeightAt(float worldX)
{
    float f = worldX/SAMPLE_DX;
    int i = (int)floorf(f);
    float t = f - (float)i;
    return TerrainAtIndex(i) + (TerrainAtIndex(i + 1) - TerrainAtIndex(i))*t;
}

static void DrawCentered(const char *text, int y, int size, Color col)
{
    DrawText(text, (SCREEN_W - MeasureText(text, size))/2, y, size, col);
}

static void DrawSky(void)
{
    Color top = bullMarket ? (Color){ 20, 60, 50, 255 }  : (Color){ 70, 25, 35, 255 };
    Color bot = bullMarket ? (Color){ 60, 150, 110, 255 } : (Color){ 160, 70, 60, 255 };
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, top, bot);
}

static void DrawTerrain(void)
{
    int first = (int)floorf((cameraX - PLAYER_SCREEN_X)/SAMPLE_DX) - 1;
    int last  = first + (int)(SCREEN_W/SAMPLE_DX) + 3;

    for (int i = first; i < last; i++)
    {
        float x0 = (float)i*SAMPLE_DX - cameraX + PLAYER_SCREEN_X;
        float x1 = x0 + SAMPLE_DX;
        float y0 = TerrainAtIndex(i);
        float y1 = TerrainAtIndex(i + 1);

        Color fill = (Color){ 18, 24, 38, 255 };
        DrawTriangle((Vector2){ x0, y0 }, (Vector2){ x0, SCREEN_H }, (Vector2){ x1, SCREEN_H }, fill);
        DrawTriangle((Vector2){ x0, y0 }, (Vector2){ x1, SCREEN_H }, (Vector2){ x1, y1 }, fill);

        Color line = (y1 <= y0) ? (Color){ 60, 230, 120, 255 } : (Color){ 240, 70, 70, 255 };
        DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y1 }, 4.0f, line);
    }
}

static void ChangeState(GameState next)
{
    state = next;
    stateTimer = 0.0f;
}

static void StartRun(void)
{
    cameraX = 0.0f;
    ChangeState(STATE_PLAYING);
}

static void UpdateDrawFrame(void)
{
    float dt = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;
    stateTimer += dt;

    switch (state)
    {
        case STATE_TITLE:
            cameraX += 60.0f*dt;
            if (cameraX > levelLength*0.5f) cameraX = 0.0f;
            if (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                ChangeState(STATE_LOADING);
            break;

        case STATE_LOADING:
            // the web request should go here and if it takes too long to load the data, the game will generate the offline data map instead
            if (stateTimer > 1.2f)
            {
                LoadFallbackData();
                BuildTerrain();
                StartRun();
            }
            break;

        case STATE_PLAYING:
            cameraX += SCROLL_SPEED*dt;
            if (cameraX >= levelLength)
            {
                cameraX = levelLength;
                ChangeState(STATE_GAMEOVER);
            }
            break;

        case STATE_GAMEOVER:
            if (IsKeyPressed(KEY_R)) StartRun();
            if (IsKeyPressed(KEY_ESCAPE)) { cameraX = 0.0f; ChangeState(STATE_TITLE); }
            break;
    }

    BeginDrawing();
        ClearBackground(BLACK);
        DrawSky();
        DrawTerrain();

        switch (state)
        {
            case STATE_TITLE:
                DrawCentered("MARKET SURFER", 90, 64, RAYWHITE);
                DrawCentered("Ride the real crypto chart", 160, 24, LIGHTGRAY);
                if ((int)(stateTimer*2.0f)%2 == 0)
                    DrawCentered("Press SPACE to start", 250, 28, YELLOW);
                break;

            case STATE_LOADING:
            {
                int dots = (int)(stateTimer*3.0f)%4;
                DrawCentered(TextFormat("Fetching the market%.*s", dots, "..."), 240, 32, RAYWHITE);
                break;
            }

            case STATE_PLAYING:
            {
                float py = TerrainHeightAt(cameraX);
                DrawCircleV((Vector2){ PLAYER_SCREEN_X, py - 10.0f }, 10.0f, ORANGE);

                float f = cameraX/(SAMPLE_DX*SUBDIV);
                int a = (int)f;
                if (a > pointCount - 2) a = pointCount - 2;
                float price = prices[a] + (prices[a + 1] - prices[a])*(f - (float)a);
                DrawText(TextFormat("Price: %.2f", price), 20, 20, 24, RAYWHITE);
                DrawText(TextFormat("Progress: %d%%", (int)(100.0f*cameraX/levelLength)), 20, 50, 24, RAYWHITE);
                break;
            }

            case STATE_GAMEOVER:
                DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 0, 0, 0, 140 });
                DrawCentered("You rode the whole market!", 160, 44, YELLOW);
                DrawCentered("Press R to restart   /   ESC for title", 260, 24, RAYWHITE);
                break;
        }
    EndDrawing();
}

int main(void)
{
    InitWindow(SCREEN_W, SCREEN_H, "Market Surfer");
    SetExitKey(KEY_NULL);

    LoadFallbackData();
    BuildTerrain();

// help from AI understanding emscripten
#if defined(PLATFORM_WEB)
    // The browser owns the loop, so hand it the frame function (it must not block)
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) UpdateDrawFrame();
#endif

    CloseWindow();
    return 0;
}
