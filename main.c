#include "raylib.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif

#define SCREEN_W        960
#define SCREEN_H        540

// how many candles and terrain points are stored
#define MAX_POINTS      400
#define SUBDIV          4
#define MAX_SAMPLES     ((MAX_POINTS - 1)*SUBDIV + 1)
#define SAMPLE_DX       20.0f

// how tall the hills are and where the camera sits
#define START_Y         300.0f
#define PX_PER_SIGMA    64.0f
#define MAX_STEP        32.0f
#define CAMERA_Y        340.0f
#define PLAYER_SCREEN_X 220.0f

#define CAMERA_ZOOM     0.8f
#define PLAYER_SCALE    0.65f

#define GRAVITY         800.0f
#define AIR_DIVE_MULT   2.0f
#define SLOPE_ACCEL     300.0f
#define HOLD_BOOST      1.8f
#define UPHILL_FACTOR   0.6f
#define CRUISE_SPEED    260.0f
#define CRUISE_PULL     0.8f
#define MIN_SPEED       140.0f
#define MAX_SPEED       480.0f
#define LAUNCH_ACCEL    230.0f
#define CREST_POP       300.0f
#define LAUNCH_COOLDOWN 0.2f
#define MIN_AIR_TIME    0.3f
#define PERFECT_IMPACT  130.0f
#define GOOD_IMPACT     280.0f
#define UPHILL_SAFE_SLOPE 0.4f
#define UPHILL_DAMAGE   45.0f
#define MAX_MARGIN      100.0f

// seconds to wait before using the offline level
#define FETCH_GIVE_UP   12.0f

// coins that get fetched plus one offline level
#define MAX_COINS       6
#define MAX_LEVELS      (MAX_COINS + 1)

#define MAX_PARTICLES   96
#define MAX_TEXTS       8

typedef enum GameState {
    STATE_TITLE,
    STATE_LOADING,
    STATE_PLAYING,
    STATE_GAMEOVER
} GameState;

// where the web request is up to
typedef enum FetchStatus {
    FETCH_IDLE,
    FETCH_PENDING,
    FETCH_OK,
    FETCH_FAILED
} FetchStatus;

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

// one coin worth of candles
typedef struct Level {
    float  prices[MAX_POINTS];
    float  volumes[MAX_POINTS];
    double times[MAX_POINTS];
    int    count;
    int    source;
    char   name[24];
} Level;

static const char *coinNames[MAX_COINS] = { "BTC", "ETH", "SOL", "DOGE", "XRP", "ADA" };
// every level slot with the offline one last
static Level levels[MAX_LEVELS];
static int   lastLevel = -1;
static int   fetchCoin = -1;

// the level being played right now
static float  prices[MAX_POINTS];
static float  volumes[MAX_POINTS];
static double priceTimes[MAX_POINTS];
static int    pointCount = 0;

// web request state
static FetchStatus fetchStatus = FETCH_IDLE;
static bool        dataLoaded = false;
static bool        offlineMode = false;
static char        sourceLabel[48] = "";
static char        levelTitle[32] = "";

static float terrainY[MAX_SAMPLES];
static int   sampleCount = 0;
static float levelLength = 0.0f;
static bool  bullMarket = true;

static float cameraX = 0.0f;
static float cameraY = 0.0f;
static float stateTimer = 0.0f;

// sounds and music load after the first key press because browsers block audio before that
static bool  audioReady = false;
static Sound sndAir;
static Sound sndPerfect;
static Sound sndDamage;
static Sound sndWin;
static Sound sndLose;
static Music musicTrack;

static Camera2D camera2d;

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
static void GenerateOfflineLevels(void)
{
   Level *lv = &levels[MAX_COINS];
    rngState = 12345;
    float p = 60000.0f;
    float drift = 0.0f;
    lv->count = 160;
    for (int i = 0; i < lv->count; i++)
    {
        if (i%20 == 0) drift = (RandFloat() - 0.5f)*0.03f;
        p *= 1.0f + drift + (RandFloat() - 0.5f)*0.02f;
        lv->prices[i]  = p;
        lv->volumes[i] = 50.0f + RandFloat()*150.0f;
        lv->times[i]   = 1760000000000.0 + (double)i*900000.0;
    }
    lv->source = 2;
    strcpy(lv->name, "Offline Sample");
}

#if defined(PLATFORM_WEB)
EMSCRIPTEN_KEEPALIVE void BeginCoin(int coin)
{
    fetchCoin = -1;
    if (fetchStatus != FETCH_PENDING || coin < 0 || coin >= MAX_COINS) return;
    fetchCoin = coin;
    levels[coin].count = 0;
    levels[coin].source = 0;
    strcpy(levels[coin].name, coinNames[coin]);
}

// saves one candle
EMSCRIPTEN_KEEPALIVE void AddCoinPoint(double close, double volume, double timeMs)
{
    if (fetchStatus != FETCH_PENDING || fetchCoin < 0) return;
    Level *lv = &levels[fetchCoin];
    if (lv->count >= MAX_POINTS) return;
    lv->prices[lv->count] = (float)close;
    lv->volumes[lv->count] = (float)volume;
    lv->times[lv->count] = timeMs;
    lv->count++;
}

// a coin only counts if it got at least 30 candles
EMSCRIPTEN_KEEPALIVE void EndCoin(void)
{
    if (fetchStatus == FETCH_PENDING && fetchCoin >= 0)
        levels[fetchCoin].source = (levels[fetchCoin].count >= 30) ? 1 : 0;
    fetchCoin = -1;
}

// tells the game if any coin loaded
EMSCRIPTEN_KEEPALIVE void FetchDone(void)
{
    if (fetchStatus != FETCH_PENDING) return;
    int ready = 0;
    for (int i = 0; i < MAX_COINS; i++)
        if (levels[i].source == 1) ready++;
    fetchStatus = (ready > 0) ? FETCH_OK : FETCH_FAILED;
}

// runs in the browser and asks binance for every coin at the same time with no key needed
// each request has a timeout and the reply is checked before its trusted
EM_JS(void, JsFetchMarket, (void), {
    const TIMEOUT_MS = 5000;
    const SYMBOLS = ["BTCUSDT", "ETHUSDT", "SOLUSDT", "DOGEUSDT", "XRPUSDT", "ADAUSDT"];

    async function getJson(url) {
        const ctl = new AbortController();
        const timer = setTimeout(() => ctl.abort(), TIMEOUT_MS);
        try {
            const res = await fetch(url, { signal: ctl.signal });
            if (!res.ok) throw new Error("HTTP " + res.status);
            return await res.json();
        } finally {
            clearTimeout(timer);
        }
    }

    async function fetchCoin(symbol) {
        try {
            const data = await getJson("https://api.binance.com/api/v3/klines?symbol=" + symbol + "&interval=15m&limit=200");
            if (!Array.isArray(data) || data.length < 30) throw new Error("bad data");
            const rows = data.map(r => ({ c: parseFloat(r[4]), v: parseFloat(r[5]), t: r[0] }));
            if (!rows.every(r => isFinite(r.c) && isFinite(r.v) && r.c > 0)) throw new Error("bad numbers");
            return rows;
        } catch (e) {
            console.warn(symbol + " failed:", e);
            return null;
        }
    }

    (async () => {
        const results = await Promise.all(SYMBOLS.map(s => fetchCoin(s)));
        results.forEach((rows, i) => {
            if (rows === null) return;
            Module._BeginCoin(i);
            for (const r of rows) Module._AddCoinPoint(r.c, r.v, r.t);
            Module._EndCoin();
        });
        Module._FetchDone();
    })();
});
#endif

// picks a random coin that is not the last one played or the offline level if nothing loaded
static void PickLevel(void)
{
    int candidates[MAX_LEVELS];
    int n = 0;

    for (int i = 0; i < MAX_COINS; i++)
        if (levels[i].source == 1 && i != lastLevel) candidates[n++] = i;
    if (n == 0)
        for (int i = 0; i < MAX_COINS; i++)
            if (levels[i].source == 1) candidates[n++] = i;
    if (n == 0) candidates[n++] = MAX_COINS;

    int pick = candidates[GetRandomValue(0, n - 1)];
    Level *lv = &levels[pick];
    lastLevel = pick;
    pointCount = lv->count;
    memcpy(prices, lv->prices, sizeof(float)*lv->count);
    memcpy(volumes, lv->volumes, sizeof(float)*lv->count);
    memcpy(priceTimes, lv->times, sizeof(double)*lv->count);
    offlineMode = (lv->source == 2);
    if (lv->source == 1)
    {
        snprintf(levelTitle, sizeof(levelTitle), "%s/USDT", lv->name);
        strcpy(sourceLabel, "15m candles - Binance");
    }
    else
    {
        snprintf(levelTitle, sizeof(levelTitle), "%s", lv->name);
        strcpy(sourceLabel, "Offline sample data");
    }
}


// turns the prices into hills
static void BuildTerrain(void)
{
    static float tmp[MAX_SAMPLES];
    static float pointH[MAX_POINTS];

    // sigma is a typical price move so every coin gets hills of a similar size
    // it uses the median move instead of the standard deviation because a few huge candles
    // make the standard deviation too big and then everything else looks flat
    static float absMoves[MAX_POINTS];
    for (int i = 1; i < pointCount; i++)
    {
        float r = fabsf((prices[i] - prices[i - 1])/prices[i - 1]);
        int j = i - 1;
        while (j > 0 && absMoves[j - 1] > r) { absMoves[j] = absMoves[j - 1]; j--; }
        absMoves[j] = r;
    }
    float sigma = absMoves[(pointCount - 1)/2]/0.6745f;
    if (sigma < 0.000001f) sigma = 1.0f;

    // each price change moves the hill up or down
    pointH[0] = START_Y;
    for (int i = 1; i < pointCount; i++)
    {
        float r = (prices[i] - prices[i - 1])/prices[i - 1];
        pointH[i] = pointH[i - 1] - (r/sigma)*PX_PER_SIGMA;
    }

    // fill in points between the candles
    sampleCount = (pointCount - 1)*SUBDIV + 1;
    for (int i = 0; i < sampleCount; i++)
    {
        float f = (float)i/SUBDIV;
        int a = (int)f;
        if (a > pointCount - 2) a = pointCount - 2;
        float t = f - (float)a;
        terrainY[i] = pointH[a] + (pointH[a + 1] - pointH[a])*t;
    }

    // smooth it with a small average
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

    // stop any slope from getting too steep
    for (int i = 1; i < sampleCount; i++)
    {
        float d = terrainY[i] - terrainY[i - 1];
        if (d >  MAX_STEP) d =  MAX_STEP;
        if (d < -MAX_STEP) d = -MAX_STEP;
        terrainY[i] = terrainY[i - 1] + d;
    }

    // level length and sky colour
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
    float w = 2.0f*SAMPLE_DX;
    return (TerrainHeightAt(worldX + w) - TerrainHeightAt(worldX - w))/(2.0f*w);
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

// in the web build these files come from the preloaded sounds folder inside the data file
static void LoadGameAudio(void)
{
    if (audioReady) return;
    InitAudioDevice();

    sndAir = LoadSound("sounds/Air.wav");
    sndPerfect = LoadSound("sounds/Perfect.wav");
    sndDamage = LoadSound("sounds/DamageTaken.wav");
    sndWin = LoadSound("sounds/Game Win.wav");
    sndLose = LoadSound("sounds/Game Lose.wav");
    musicTrack = LoadMusicStream("sounds/BackgroundTrack.wav");

    SetSoundVolume(sndAir, 0.6f);
    musicTrack.looping = true;
    SetMusicVolume(musicTrack, 0.35f);
    PlayMusicStream(musicTrack);
    audioReady = true;
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
        DrawCircleV(particles[i].pos, particles[i].size*(0.4f + 0.6f*k), c);
    }
    for (int i = 0; i < MAX_TEXTS; i++)
    {
        if (texts[i].life <= 0.0f) continue;
        Color c = texts[i].color;
        c.a = (unsigned char)(255.0f*fminf(1.0f, texts[i].life*1.5f));
        int w = MeasureText(texts[i].text, 30);
        DrawText(texts[i].text, (int)texts[i].pos.x - w/2, (int)texts[i].pos.y, 30, c);
    }
}

static void DrawCentered(const char *text, int y, int size, Color col)
{
    DrawText(text, (SCREEN_W - MeasureText(text, size))/2, y, size, col);
}

static void DrawTerrain(void)
{
    int first = (int)floorf((cameraX - PLAYER_SCREEN_X/CAMERA_ZOOM)/SAMPLE_DX) - 1;
    int last  = first + (int)(SCREEN_W/CAMERA_ZOOM/SAMPLE_DX) + 3;
    float bottom = camera2d.target.y + SCREEN_H/CAMERA_ZOOM;

    for (int i = first; i < last; i++)
    {
        float x0 = (float)i*SAMPLE_DX;
        float x1 = x0 + SAMPLE_DX;
        float y0 = TerrainAtIndex(i);
        float y1 = TerrainAtIndex(i + 1);
        Color fill = (Color){ 18, 24, 38, 255 };
        DrawTriangle((Vector2){ x0, y0 }, (Vector2){ x0, bottom }, (Vector2){ x1, bottom }, fill);
        DrawTriangle((Vector2){ x0, y0 }, (Vector2){ x1, bottom }, (Vector2){ x1, y1 }, fill);
        Color line = (y1 <= y0) ? (Color){ 60, 230, 120, 255 } : (Color){ 240, 70, 70, 255 };
        DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y1 }, 5.0f, line);
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
    player.score = 0;
    player.grounded = true;
    player.holding = false;
    runWon = false;
    cameraX = 0.0f;
    cameraY = player.y - CAMERA_Y;
    if (audioReady) ResumeMusicStream(musicTrack);
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
        float uphill = -slope;

        bool rising = player.vy < 0.0f;

        if (!rising)
        {
            if (uphill > UPHILL_SAFE_SLOPE)
            {
                player.margin -= (uphill - UPHILL_SAFE_SLOPE)*UPHILL_DAMAGE*(player.vx/CRUISE_SPEED);
                player.flash = 0.4f;
                tangent *= 0.7f;
                SpawnText(where, "OUCH!", (Color){ 240, 70, 70, 255 });
                if (audioReady) PlaySound(sndDamage);
            }
            else if (impact < PERFECT_IMPACT)
            {
                player.score += 25;
                tangent *= 1.08f;
                SpawnText(where, "PERFECT!", YELLOW);
                if (audioReady) PlaySound(sndPerfect);
            }
            else if (impact < GOOD_IMPACT)
            {
                player.score += 10;
                SpawnText(where, "+10", RAYWHITE);
            }
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

    player.holding = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S) || IsMouseButtonDown(MOUSE_BUTTON_LEFT);

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

        if (bend > LAUNCH_ACCEL && player.cooldown <= 0.0f)
        {
            player.grounded = false;
            player.airTime = 0.0f;
            player.vy = player.prevSlope*player.vx;
            if (!player.holding) player.vy -= CREST_POP*(player.vx/CRUISE_SPEED);
            SpawnText((Vector2){ player.x, player.y - 40.0f }, "AIR!", (Color){ 180, 220, 255, 255 });
            if (audioReady)
            {
                SetSoundPitch(sndAir, 0.92f + (float)GetRandomValue(0, 20)/100.0f);
                PlaySound(sndAir);
            }
        }
        else
        {
            player.x += player.vx*dt;
            player.y = TerrainHeightAt(player.x);
            player.prevSlope = slope;
            targetAngle = atanf(slope)*RAD2DEG;
            player.angle += (targetAngle - player.angle)*fminf(1.0f, 14.0f*dt);

            if (player.holding)
                EmitParticle((Vector2){ player.x - 12.0f, player.y },
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
            EmitParticle((Vector2){ player.x - 8.0f, player.y },
                         (Vector2){ -player.vx*0.1f, 0.0f }, 0.25f, 2.0f, (Color){ 255, 255, 255, 200 });
    }

    cameraX = player.x;
}

static void DrawPlayer(void)
{
    float a = player.angle*DEG2RAD;
    float s = PLAYER_SCALE;
    Vector2 pos = { player.x, player.y };
    Vector2 rider = { pos.x + sinf(a)*13.0f*s, pos.y - cosf(a)*13.0f*s };

    Rectangle board = { pos.x, pos.y - 2.0f*s, 44.0f*s, 6.0f*s };
    DrawRectanglePro(board, (Vector2){ 22.0f*s, 3.0f*s }, player.angle, player.holding ? (Color){ 255, 200, 80, 255 } : RAYWHITE);
    DrawCircleV(rider, 9.0f*s, ORANGE);
    DrawCircleV((Vector2){ rider.x + 3.0f*s, rider.y - 2.0f*s }, 2.5f*s, BLACK);
}

static void NewRun(void)
{
    PickLevel();
    BuildTerrain();
    StartRun();
}

static void UpdateDrawFrame(void)
{
    float dt = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;
    stateTimer += dt;

    if (audioReady) UpdateMusicStream(musicTrack);

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
            {
                LoadGameAudio();
                ChangeState(STATE_LOADING);
            }
            break;

        case STATE_LOADING:
            // data is kept after the first load so restarting never fetches again
            // the game keeps drawing while the request runs and goes offline if it fails or takes too long
            if (!dataLoaded)
            {
                // the request starts once and the desktop build has no browser so it goes straight to offline
                if (fetchStatus == FETCH_IDLE)
                {
                    fetchStatus = FETCH_PENDING;
#if defined(PLATFORM_WEB)
                    JsFetchMarket();
#else
                    fetchStatus = FETCH_FAILED;
#endif
                }
                if (fetchStatus == FETCH_OK) dataLoaded = true;
                else if (fetchStatus == FETCH_FAILED || stateTimer > FETCH_GIVE_UP)
                {
                    fetchStatus = FETCH_FAILED;
                    dataLoaded = true;
                }
            }
            if (dataLoaded && stateTimer > 0.8f) NewRun();
            break;

        case STATE_PLAYING:
            UpdatePlayer(dt);
            if (player.margin <= 0.0f)
            {
                runWon = false;
                if (audioReady)
                {
                    PlaySound(sndLose);
                    PauseMusicStream(musicTrack);
                }
                ChangeState(STATE_GAMEOVER);
            }
            else if (player.x >= levelLength)
            {
                cameraX = levelLength;
                runWon = true;
                if (audioReady)
                {
                    PlaySound(sndWin);
                    PauseMusicStream(musicTrack);
                }
                ChangeState(STATE_GAMEOVER);
            }
            break;

        case STATE_GAMEOVER:
            if (IsKeyPressed(KEY_R)) NewRun();
            if (IsKeyPressed(KEY_ESCAPE)) { cameraX = 0.0f; ChangeState(STATE_TITLE); }
            break;
    }

    BeginDrawing();
        ClearBackground(BLACK);

        Color skyTop = bullMarket ? (Color){ 20, 60, 50, 255 }  : (Color){ 70, 25, 35, 255 };
        Color skyBottom = bullMarket ? (Color){ 60, 150, 110, 255 } : (Color){ 160, 70, 60, 255 };
        DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, skyTop, skyBottom);

        camera2d.target = (Vector2){ cameraX, cameraY + CAMERA_Y };
        BeginMode2D(camera2d);
            DrawTerrain();
            if (state == STATE_PLAYING)
            {
                DrawEffects();
                DrawPlayer();
            }
        EndMode2D();

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
                if (stateTimer > 3.0f)
                    DrawCentered("Still trying - will switch to offline data soon", 290, 20, LIGHTGRAY);
                break;
            }

            case STATE_PLAYING:
            {
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
                if (price < 10.0f) DrawText(TextFormat("Price: %.4f", price), 20, 20, 24, RAYWHITE);
                else DrawText(TextFormat("Price: %.2f", price), 20, 20, 24, RAYWHITE);
                DrawText(TextFormat("Progress: %d%%", (int)(100.0f*player.x/levelLength)), 20, 50, 24, RAYWHITE);
                DrawText(levelTitle, 20, 80, 24, YELLOW);
                DrawText(sourceLabel, 20, 108, 18, LIGHTGRAY);
                if (offlineMode) DrawText("OFFLINE MODE", 20, 130, 22, (Color){ 255, 140, 60, 255 });

                if (stateTimer < 2.5f)
                {
                    float fade = fminf(1.0f, (2.5f - stateTimer)*2.0f);
                    DrawCentered(levelTitle, 110, 56, (Color){ 255, 255, 255, (unsigned char)(255.0f*fade) });
                    DrawCentered(sourceLabel, 175, 22, (Color){ 220, 220, 220, (unsigned char)(255.0f*fade) });
                }
                break;
            }

            case STATE_GAMEOVER:
                DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){ 0, 0, 0, 140 });
                if (runWon) DrawCentered("You rode the whole market!", 130, 44, YELLOW);
                else        DrawCentered("Liquidated!", 130, 56, (Color){ 240, 70, 70, 255 });
                DrawCentered(TextFormat("Score: %d", player.score), 220, 32, RAYWHITE);
                DrawCentered(TextFormat("Progress: %d%%", (int)(100.0f*player.x/levelLength)), 262, 24, LIGHTGRAY);
                DrawCentered(TextFormat("Level: %s", levelTitle), 296, 24, LIGHTGRAY);
                DrawCentered("Press R for a new random level   /   ESC for title", 350, 24, RAYWHITE);
                break;
        }
    EndDrawing();
}

int main(void)
{
    InitWindow(SCREEN_W, SCREEN_H, "Market Surfer");
    SetExitKey(KEY_NULL);
    camera2d.offset = (Vector2){ PLAYER_SCREEN_X, CAMERA_Y };
    camera2d.rotation = 0.0f;
    camera2d.zoom = CAMERA_ZOOM;
    // random seed so the coin is different each time
    SetRandomSeed((unsigned int)time(NULL));
    // make the offline level first so the title has a map
    GenerateOfflineLevels();
    PickLevel();
    BuildTerrain();

#if defined(PLATFORM_WEB)
    // the browser runs the loop so it is given the frame function and that cannot block
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) UpdateDrawFrame();
#endif

    if (audioReady)
    {
        UnloadSound(sndAir);
        UnloadSound(sndPerfect);
        UnloadSound(sndDamage);
        UnloadSound(sndWin);
        UnloadSound(sndLose);
        UnloadMusicStream(musicTrack);
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}
