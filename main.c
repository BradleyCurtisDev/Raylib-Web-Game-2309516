#include "raylib.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif

#define SCREEN_W        960
#define SCREEN_H        540

#define MAX_POINTS      400
#define SUBDIV          4
#define MAX_SAMPLES     ((MAX_POINTS - 1)*SUBDIV + 1)
#define SAMPLE_DX       12.0f

#define START_Y         300.0f
#define PX_PER_SIGMA    34.0f
#define MAX_STEP        16.0f
#define CAMERA_Y        340.0f
#define PLAYER_SCREEN_X 220.0f

#define GRAVITY         950.0f
#define AIR_DIVE_MULT   2.0f
#define SLOPE_ACCEL     340.0f
#define HOLD_BOOST      1.8f
#define UPHILL_FACTOR   0.6f
#define CRUISE_SPEED    330.0f
#define CRUISE_PULL     0.8f
#define MIN_SPEED       170.0f
#define MAX_SPEED       620.0f
#define LAUNCH_ACCEL    450.0f
#define LAUNCH_COOLDOWN 0.2f
#define RELEASE_POP     340.0f
#define RELEASE_MIN_HOLD 0.12f
#define RELEASE_MIN_SLOPE 0.12f
#define MIN_AIR_TIME    0.3f
#define PERFECT_IMPACT  160.0f
#define GOOD_IMPACT     340.0f
#define SAFE_IMPACT     500.0f
#define DAMAGE_PER_PX   0.1f
#define MAX_MARGIN      100.0f

#define MAX_PARTICLES   96
#define MAX_TEXTS       8

typedef enum GameState {
    STATE_TITLE,
    STATE_LOADING,
    STATE_PLAYING,
    STATE_GAMEOVER
} GameState;

typedef struct Player {
    float x;
    float y;
    float speed;
    float vx;
    float vy;
    float prevSlope;
    float angle;
    float margin;
    float flash;
    float airTime;
    float cooldown;
    float holdTime;
    int   score;
    bool  grounded;
    bool  holding;
} Player;

typedef struct Particle {
    Vector2 pos;
    Vector2 vel;
    float   life;
    float   maxLife;
    float   size;
    Color   color;
} Particle;

typedef struct FloatText {
    Vector2 pos;
    float   life;
    char    text[24];
    Color   color;
} FloatText;

static GameState state = STATE_TITLE;

static float prices[MAX_POINTS];
static float volumes[MAX_POINTS];
static int   pointCount = 0;

static float terrainY[MAX_SAMPLES];
static int   sampleCount = 0;
static float levelLength = 0.0f;
static bool  bullMarket = true;

static float cameraX = 0.0f;
static float cameraY = 0.0f;
static float stateTimer = 0.0f;

static Player    player;
static bool      runWon = false;
static Particle  particles[MAX_PARTICLES];
static FloatText texts[MAX_TEXTS];

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
    static float pointH[MAX_POINTS];

    float sum2 = 0.0f;
    for (int i = 1; i < pointCount; i++)
    {
        float r = (prices[i] - prices[i - 1])/prices[i - 1];
        sum2 += r*r;
    }
    float sigma = sqrtf(sum2/(float)(pointCount - 1));
    if (sigma < 0.000001f) sigma = 1.0f;

    pointH[0] = START_Y;
    for (int i = 1; i < pointCount; i++)
    {
        float r = (prices[i] - prices[i - 1])/prices[i - 1];
        pointH[i] = pointH[i - 1] - (r/sigma)*PX_PER_SIGMA;
    }

    sampleCount = (pointCount - 1)*SUBDIV + 1;
    for (int i = 0; i < sampleCount; i++)
    {
        float f = (float)i/SUBDIV;
        int a = (int)f;
        if (a > pointCount - 2) a = pointCount - 2;
        float t = f - (float)a;
        terrainY[i] = pointH[a] + (pointH[a + 1] - pointH[a])*t;
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

static float TerrainSlopeAt(float worldX)
{
    return (TerrainHeightAt(worldX + 24.0f) - TerrainHeightAt(worldX - 24.0f))/48.0f;
}

static void EmitParticle(Vector2 pos, Vector2 vel, float life, float size, Color color)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        if (particles[i].life <= 0.0f)
        {
            particles[i] = (Particle){ pos, vel, life, life, size, color };
            return;
        }
    }
}

static void SpawnText(Vector2 pos, const char *text, Color color)
{
    for (int i = 0; i < MAX_TEXTS; i++)
    {
        if (texts[i].life <= 0.0f)
        {
            texts[i].pos = pos;
            texts[i].life = 1.0f;
            texts[i].color = color;
            strncpy(texts[i].text, text, sizeof(texts[i].text) - 1);
            texts[i].text[sizeof(texts[i].text) - 1] = '\0';
            return;
        }
    }
}

static void UpdateEffects(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        if (particles[i].life > 0.0f)
        {
            particles[i].life -= dt;
            particles[i].pos.x += particles[i].vel.x*dt;
            particles[i].pos.y += particles[i].vel.y*dt;
        }
    }
    for (int i = 0; i < MAX_TEXTS; i++)
    {
        if (texts[i].life > 0.0f)
        {
            texts[i].life -= dt;
            texts[i].pos.y -= 50.0f*dt;
        }
    }
}

static void DrawEffects(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        if (particles[i].life <= 0.0f) continue;
        float k = particles[i].life/particles[i].maxLife;
        Color c = particles[i].color;
        c.a = (unsigned char)(255.0f*k);
        DrawCircleV((Vector2){ particles[i].pos.x - cameraX + PLAYER_SCREEN_X, particles[i].pos.y - cameraY },
                    particles[i].size*(0.4f + 0.6f*k), c);
    }
    for (int i = 0; i < MAX_TEXTS; i++)
    {
        if (texts[i].life <= 0.0f) continue;
        Color c = texts[i].color;
        c.a = (unsigned char)(255.0f*fminf(1.0f, texts[i].life*1.5f));
        int w = MeasureText(texts[i].text, 26);
        DrawText(texts[i].text, (int)(texts[i].pos.x - cameraX + PLAYER_SCREEN_X) - w/2,
                 (int)(texts[i].pos.y - cameraY), 26, c);
    }
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
        float y0 = TerrainAtIndex(i) - cameraY;
        float y1 = TerrainAtIndex(i + 1) - cameraY;

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
    float slope = TerrainSlopeAt(0.0f);
    memset(particles, 0, sizeof(particles));
    memset(texts, 0, sizeof(texts));

    player.x = 0.0f;
    player.y = TerrainHeightAt(0.0f);
    player.speed = CRUISE_SPEED;
    player.vx = CRUISE_SPEED;
    player.vy = 0.0f;
    player.prevSlope = slope;
    player.angle = atanf(slope)*RAD2DEG;
    player.margin = MAX_MARGIN;
    player.flash = 0.0f;
    player.airTime = 0.0f;
    player.cooldown = 0.0f;
    player.holdTime = 0.0f;
    player.score = 0;
    player.grounded = true;
    player.holding = false;
    runWon = false;
    cameraX = 0.0f;
    cameraY = player.y - CAMERA_Y;
    ChangeState(STATE_PLAYING);
}

static void LandPlayer(float groundY, float slope)
{
    float len = sqrtf(1.0f + slope*slope);
    float tangent = (player.vx + player.vy*slope)/len;
    float impact = (player.vy - player.vx*slope)/len;
    if (impact < 0.0f) impact = 0.0f;

    Vector2 where = { player.x, groundY - 30.0f };

    if (player.airTime >= MIN_AIR_TIME)
    {
        if (impact > SAFE_IMPACT)
        {
            player.margin -= (impact - SAFE_IMPACT)*DAMAGE_PER_PX;
            player.flash = 0.4f;
            tangent *= 0.7f;
            SpawnText(where, "OUCH!", (Color){ 240, 70, 70, 255 });
        }
        else if (impact < PERFECT_IMPACT)
        {
            player.score += 25;
            tangent *= 1.08f;
            SpawnText(where, "PERFECT!", YELLOW);
        }
        else if (impact < GOOD_IMPACT)
        {
            player.score += 10;
            SpawnText(where, "+10", RAYWHITE);
        }

        for (int i = 0; i < 10; i++)
            EmitParticle((Vector2){ player.x, groundY },
                         (Vector2){ (RandFloat() - 0.3f)*260.0f, -RandFloat()*220.0f },
                         0.5f, 4.0f, (Color){ 220, 230, 255, 255 });
    }

    player.speed = fminf(fmaxf(tangent, MIN_SPEED), MAX_SPEED);
    player.y = groundY;
    player.vy = 0.0f;
    player.prevSlope = slope;
    player.cooldown = LAUNCH_COOLDOWN;
    player.grounded = true;
}

static void UpdatePlayer(float dt)
{
    if (dt < 0.001f) dt = 0.001f;

    bool wasHolding = player.holding;
    float heldFor = player.holdTime;
    player.holding = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S) || IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool released = wasHolding && !player.holding;
    player.holdTime = player.holding ? player.holdTime + dt : 0.0f;

    if (player.flash > 0.0f) player.flash -= dt;

    float slope;
    float targetAngle;

    if (player.grounded)
    {
        slope = TerrainSlopeAt(player.x);
        float len = sqrtf(1.0f + slope*slope);
        float sinT = slope/len;

        float accel = SLOPE_ACCEL*sinT*(player.holding ? HOLD_BOOST : 1.0f);
        if (sinT < 0.0f) accel *= UPHILL_FACTOR;
        player.speed += accel*dt + (CRUISE_SPEED - player.speed)*CRUISE_PULL*dt;
        player.speed = fminf(fmaxf(player.speed, MIN_SPEED), MAX_SPEED);

        player.vx = player.speed/len;
        float bend = (slope - player.prevSlope)*player.vx/dt;
        if (player.cooldown > 0.0f) player.cooldown -= dt;

        if (released && heldFor >= RELEASE_MIN_HOLD && slope > RELEASE_MIN_SLOPE)
        {
            float speedPct = fminf(1.0f, player.speed/MAX_SPEED);
            player.grounded = false;
            player.airTime = 0.0f;
            player.vy = slope*player.vx - RELEASE_POP*(0.7f + 0.6f*speedPct);
            SpawnText((Vector2){ player.x, player.y - 40.0f }, "POP!", (Color){ 255, 220, 120, 255 });
            for (int i = 0; i < 6; i++)
                EmitParticle((Vector2){ player.x, player.y },
                             (Vector2){ -player.vx*0.2f + (RandFloat() - 0.5f)*80.0f, -RandFloat()*140.0f },
                             0.4f, 3.5f, (Color){ 255, 200, 80, 255 });
        }
        else if (bend > LAUNCH_ACCEL && player.cooldown <= 0.0f)
        {
            player.grounded = false;
            player.airTime = 0.0f;
            player.vy = player.prevSlope*player.vx - 20.0f;
            SpawnText((Vector2){ player.x, player.y - 40.0f }, "AIR!", (Color){ 180, 220, 255, 255 });
        }
        else
        {
            player.x += player.vx*dt;
            player.y = TerrainHeightAt(player.x);
            player.prevSlope = slope;
            targetAngle = atanf(slope)*RAD2DEG;
            player.angle += (targetAngle - player.angle)*fminf(1.0f, 14.0f*dt);

            if (player.holding)
                EmitParticle((Vector2){ player.x - 18.0f, player.y },
                             (Vector2){ -player.speed*0.15f, -RandFloat()*60.0f },
                             0.35f, 3.0f + player.speed/300.0f, (Color){ 255, 200, 80, 255 });
        }
    }

    if (!player.grounded)
    {
        float g = player.holding ? GRAVITY*AIR_DIVE_MULT : GRAVITY;
        player.vy += g*dt;
        player.x += player.vx*dt;
        player.y += player.vy*dt;
        player.airTime += dt;

        targetAngle = atan2f(player.vy, player.vx)*RAD2DEG;
        player.angle += (targetAngle - player.angle)*fminf(1.0f, 10.0f*dt);

        float groundY = TerrainHeightAt(player.x);
        if (player.y >= groundY)
            LandPlayer(groundY, TerrainSlopeAt(player.x));
        else
            EmitParticle((Vector2){ player.x - 12.0f, player.y },
                         (Vector2){ -player.vx*0.1f, 0.0f }, 0.25f, 2.5f, (Color){ 255, 255, 255, 200 });
    }

    cameraX = player.x;
}

static void DrawPlayer(void)
{
    float a = player.angle*DEG2RAD;
    Vector2 pos = { PLAYER_SCREEN_X, player.y - cameraY };
    Vector2 rider = { pos.x + sinf(a)*13.0f, pos.y - cosf(a)*13.0f };

    Rectangle board = { pos.x, pos.y - 2.0f, 44.0f, 6.0f };
    DrawRectanglePro(board, (Vector2){ 22.0f, 3.0f }, player.angle, player.holding ? (Color){ 255, 200, 80, 255 } : RAYWHITE);
    DrawCircleV(rider, 9.0f, ORANGE);
    DrawCircleV((Vector2){ rider.x + 3.0f, rider.y - 2.0f }, 2.5f, BLACK);
}

static void UpdateDrawFrame(void)
{
    float dt = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;
    stateTimer += dt;

    float followY = (state == STATE_PLAYING) ? player.y : TerrainHeightAt(cameraX);
    float camBlend = 5.0f*dt;
    if (camBlend > 1.0f) camBlend = 1.0f;
    cameraY += (followY - CAMERA_Y - cameraY)*camBlend;

    UpdateEffects(dt);

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
            UpdatePlayer(dt);
            if (player.margin <= 0.0f)
            {
                runWon = false;
                ChangeState(STATE_GAMEOVER);
            }
            else if (player.x >= levelLength)
            {
                cameraX = levelLength;
                runWon = true;
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
                DrawCentered("Hold SPACE / CLICK on downhills to build speed. Let go going uphill.", 450, 20, RAYWHITE);
                break;

            case STATE_LOADING:
            {
                int dots = (int)(stateTimer*3.0f)%4;
                DrawCentered(TextFormat("Fetching the market%.*s", dots, "..."), 240, 32, RAYWHITE);
                break;
            }

            case STATE_PLAYING:
            {
                DrawEffects();
                DrawPlayer();
                if (player.flash > 0.0f)
                    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 255, 0, 0, (unsigned char)(player.flash/0.4f*110.0f) });

                float marginPct = player.margin/MAX_MARGIN;
                if (marginPct < 0.0f) marginPct = 0.0f;
                Color barCol = (marginPct > 0.5f) ? (Color){ 60, 230, 120, 255 } : (marginPct > 0.25f) ? ORANGE : (Color){ 240, 70, 70, 255 };
                DrawText("MARGIN", SCREEN_W - 260, 18, 18, RAYWHITE);
                DrawRectangle(SCREEN_W - 190, 20, 170, 16, (Color){ 0, 0, 0, 140 });
                DrawRectangle(SCREEN_W - 190, 20, (int)(170.0f*marginPct), 16, barCol);
                DrawRectangleLines(SCREEN_W - 190, 20, 170, 16, RAYWHITE);

                float spd = (player.grounded ? player.speed : player.vx);
                float spdPct = fminf(1.0f, spd/MAX_SPEED);
                DrawText("SPEED", SCREEN_W - 260, 44, 18, RAYWHITE);
                DrawRectangle(SCREEN_W - 190, 46, 170, 16, (Color){ 0, 0, 0, 140 });
                DrawRectangle(SCREEN_W - 190, 46, (int)(170.0f*spdPct), 16, (Color){ 255, 200, 80, 255 });
                DrawRectangleLines(SCREEN_W - 190, 46, 170, 16, RAYWHITE);

                DrawText(TextFormat("Score: %d", player.score), SCREEN_W - 260, 72, 22, YELLOW);
                DrawText("HOLD SPACE / CLICK / DOWN: press into the slope (downhill = speed, uphill = slow)", 20, SCREEN_H - 28, 18, RAYWHITE);

                float f = player.x/(SAMPLE_DX*SUBDIV);
                int a = (int)f;
                if (a < 0) a = 0;
                if (a > pointCount - 2) a = pointCount - 2;
                float t = f - (float)a;
                if (t > 1.0f) t = 1.0f;
                float price = prices[a] + (prices[a + 1] - prices[a])*t;
                DrawText(TextFormat("Price: %.2f", price), 20, 20, 24, RAYWHITE);
                DrawText(TextFormat("Progress: %d%%", (int)(100.0f*player.x/levelLength)), 20, 50, 24, RAYWHITE);
                break;
            }

            case STATE_GAMEOVER:
                DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 0, 0, 0, 140 });
                if (runWon) DrawCentered("You rode the whole market!", 130, 44, YELLOW);
                else        DrawCentered("Liquidated!", 130, 56, (Color){ 240, 70, 70, 255 });
                DrawCentered(TextFormat("Score: %d", player.score), 220, 32, RAYWHITE);
                DrawCentered(TextFormat("Progress: %d%%", (int)(100.0f*player.x/levelLength)), 262, 24, LIGHTGRAY);
                DrawCentered("Press R to restart   /   ESC for title", 330, 24, RAYWHITE);
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
