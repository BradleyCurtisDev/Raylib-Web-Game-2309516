# Market Surfer



Play it online: [[Market Surfer on Itch.io](https://bradleycurtis.itch.io/market-surfer)]

![Gameplay](demo.gif)

## Controls

- Hold SPACE/ CLICK into slopes, going downhill gives you speed. When pressed in the air, you will be pulled down faster.
- Let go going uphill.
- R starts a new random level after a run. ESC goes back to the title.


## How to compile with Emscripten

You need raylib 6.0 and Emscripten (emsdk 6.0.11). Activate emsdk, then run this from the folder that contains `main.c`:

```
C:\emsdk\emsdk_env.bat
emcc main.c -o web/index.html -std=c99 -Wall -Os -DPLATFORM_WEB -IC:/raylib/raylib/src C:/raylib/raylib/src/libraylib.web.a -sUSE_GLFW=3 -sASYNCIFY --preload-file sounds
```

Change `C:/raylib/raylib/src` if raylib is installed somewhere else. This creates `index.html`, `index.js`, `index.wasm` and `index.data` in the `web` folder.

## How to run it in a browser

```
cd web
python -m http.server 8000
```

Then open http://localhost:8000

## The web request

The game uses the public Binance API, with no key needed:

```
https://api.binance.com/api/v3/klines?symbol=BTCUSDT&interval=15m&limit=200
```

Setup: I have set up the program to choose randomly between six different Cryptocurrencies (BTC, ETH, SOL, DOGE, XRP and ADA) for each time you play. The latest 200 fifteen minute candles are gathered and the closing price is then plotted as the terrain heights so the game builds the map based on recent price data.

If the API request fails, the game uses an offline backup map so it can still be played offline however the offline map isnt random.

## Credits

- raylib: https://www.raylib.com
- Emscripten: https://emscripten.org
- Price data: Binance public API
- Sounds: All sounds were gathered from [Freesounds.org](https://freesound.org/)

AI use: I used Claude Sonnet 5.5 To help with some of the work in the project such as: Bug fixing, Understanding tools like emscripten and help with compiling the main.c file for WebAssembly.
