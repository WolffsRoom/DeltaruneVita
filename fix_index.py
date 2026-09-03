import sys
from pathlib import Path

index_path = Path(r"c:\Users\wolff\Documents\SDKVita\DeltaruneVita\artifacts\Patcher\Build_Patch\webpatcher\index.html")
content = index_path.read_text(encoding="utf-8")

# Find line where /* ================= sound ================= */ or audioCtx appears
sound_idx = content.find("/* ================= sound ================= */")
if sound_idx == -1:
    sound_idx = content.find("let audioUnlocked = false;")

print(f"sound_idx: {sound_idx}")

# Head part (lines 1 to 134)
head_part = """<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<link rel="icon" type="image/x-icon" href="./favicon.ico">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Seam's Patcher - DeltaruneVita</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Mona+Sans:ital,wght@0,200..900;1,200..900&display=swap" rel="stylesheet">
<style>
  :root{
    --white:#ffffff;
    --yellow:#ffd94a;
    --orange:#ff8f2b;
    --gray:#9a9a9a;
    --heart:#ff2b2b;
  }
  @font-face{
    font-family:"DTRune";
    src:url("assets/undertale-deltarune-extended-fixed.otf") format("opentype");
    font-display:swap;
  }
  *{box-sizing:border-box;margin:0;padding:0}
  html,body{height:100%}
  body{
    background:#000;
    color:var(--white);
    font-family:"DTRune","Courier New",ui-monospace,monospace;
    font-weight:normal;
    display:flex;align-items:center;justify-content:center;
    min-height:100vh;overflow:hidden;
  }
  img{image-rendering:pixelated;-webkit-user-drag:none;user-select:none}

  /* frame authored at 1920x1080 â€” all layers overlap perfectly */
  #frame{
    position:relative;
    width:min(80vw, 142.2vh);
    aspect-ratio:16/9;
    background:#000;
    overflow:hidden;
    /* text scales with the frame */
    font-size:min(1.78vw, 3.16vh);
    line-height:1.35;
  }
  .layer{position:absolute;inset:0;width:100%;height:100%}
  #borda{z-index:5;pointer-events:none}
  /* black vignette fades the border edges into the black background */
  #vignette{position:absolute;inset:0;z-index:7;pointer-events:none;
    box-shadow:inset 0 0 7vw 2.2vw #000}
  #fundo{z-index:10}
  #shopbg{z-index:20}
  #seam-wrap{
    position:absolute;z-index:30;
    left:50%;bottom:52%;transform:translate(-50%,30px);   /* acima do dialog2, 30px pra baixo */
    width:25.7%;                 /* 135% do tamanho anterior (19%) */
    transition:left .35s ease;
  }
  #seam-wrap.left{ left:35%; }
  #seam{width:100%;display:block;filter:drop-shadow(0 .35vw 0 rgba(0,0,0,.55))}
  #dialog{z-index:40;pointer-events:none}

  /* text overlays live above the dialog art */
  .text-layer{position:absolute;z-index:50;color:var(--white);
    text-shadow:.08em .08em 0 #000;white-space:pre-wrap}

  /* single wide box (intro / final) */
  #single{left:19.5%;right:19.5%;top:55.5%;bottom:8%;display:none}

  /* split: SAM speech (left) */
  #speech{left:19%;top:56%;width:37%;height:34%;display:none}

  /* split: options (right) */
  #options{left:61.5%;top:57%;width:20%;height:32%;display:none;
    flex-direction:column;gap:.15em}

  /* dialogplus (explanation panel, upper-right) â€” slides up from below */
  #plusbg{z-index:44;opacity:0;visibility:hidden;transform:translateY(7%);
    transition:opacity .3s ease, transform .3s ease}
  #plusbg.show{opacity:1;visibility:visible;transform:translateY(0)}
  #plus{left:60.5%;top:13%;width:22%;height:37%;
    display:flex;flex-direction:column;gap:.12em;
    opacity:0;visibility:hidden;transform:translateY(7%);
    transition:opacity .3s ease, transform .3s ease}
  #plus.show{opacity:1;visibility:visible;transform:translateY(0)}
  #plus .ptitle{color:var(--orange);font-size:.68em;letter-spacing:.1em;margin-bottom:.15em}
  #plus .okrow{position:relative;padding-left:1.5em;margin-top:.4em;color:var(--white)}
  #plus .okrow .h{position:absolute;left:0;top:.06em;width:1.05em;height:1.05em;image-rendering:pixelated}

  /* all options are white; the heart pointer is the only highlight */
  .opt{position:relative;padding-left:1.5em;cursor:pointer;color:var(--white);
    white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
  .opt .h{position:absolute;left:0;top:.08em;width:1.05em;height:1.05em;
    opacity:0;image-rendering:pixelated}
  .opt.active .h{opacity:1}
  .opt .pr{color:var(--white);font-size:.78em;margin-left:.4em}

  /* download / patch progress bar (in the speech box) */
  #pbar{position:absolute;z-index:50;left:50%;top:73%;transform:translateX(-50%);width:55%;height:5%;
    display:none;border:.15em solid var(--white);background:#000;box-sizing:border-box}
  #pbar > i{display:block;height:100%;width:0;background:var(--yellow);transition:width .08s linear}
  #pbar .ptext{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;
    color:var(--white);font-family:"DTRune",monospace;font-size:0.8em;text-shadow:0.08em 0.08em 0 #000}

  .prompt{position:absolute;right:.2em;bottom:-.1em;color:var(--yellow);
    font-size:.7em;animation:blink 1s steps(1) infinite}
  @keyframes blink{50%{opacity:0}}

  #bottom-bar{position:fixed;z-index:100;left:0;right:0;bottom:0.8vh;
    display:flex;flex-direction:column;align-items:center;gap:0.3vh;pointer-events:none}
  #hint{color:var(--white);font-family:"DTRune","Courier New",monospace;
    font-size:clamp(9px,1.1vw,15px);text-shadow:1px 1px 0 #000}
  #disclaimer{color:rgba(255,255,255,0.45);font-family:"Mona Sans","Mona Sans FV",sans-serif;
    font-size:clamp(7px,0.65vw,9.5px);line-height:1.25;text-align:center;text-shadow:1px 1px 0 #000;max-width:90vw}
  #version{position:absolute;z-index:61;right:3%;bottom:1.5%;
    color:var(--white);font-size:.6em;pointer-events:none;text-shadow:.08em .08em 0 #000}

  /* black fade-in on load */
  #fade{position:absolute;inset:0;z-index:80;background:#000;
    opacity:1;pointer-events:none;transition:opacity 1.2s ease}
  #fade.gone{opacity:0}

  /* splash screen (logo) before the shop opens */
  #splash{position:absolute;inset:0;z-index:90;background:#000;
    display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1.2em;
    transition:opacity 1s ease}
  #splash.gone{opacity:0;pointer-events:none}
  #splash img{width:58%;max-width:60vw;height:auto;image-rendering:auto}
  #splash .by{color:var(--white);font-size:1.1em;letter-spacing:.06em;
    text-shadow:.06em .06em 0 #000;opacity:.9}
  #splash .press{color:var(--orange);font-size:.85em;margin-top:1.4em;
    text-shadow:.06em .06em 0 #000;animation:blink 1.1s steps(1) infinite}
  /* language selection screen */
  #lang-screen{position:absolute;inset:0;z-index:95;background:#000;
    display:flex;flex-direction:column;align-items:center;justify-content:center;
    opacity:0;pointer-events:none;transition:opacity .6s ease}
  #lang-screen.show{opacity:1;pointer-events:auto}
  #lang-screen .title{color:var(--white);font-size:1.6em;margin-bottom:0.8em;text-shadow:.08em .08em 0 #000}
  #lang-screen .menu{display:flex;flex-direction:column;gap:0.3em;align-items:flex-start}
  #lang-screen .item {
    position: relative;
    padding-left: 1.5em;
    color: var(--white);
    font-size: 1.1em;
    cursor: pointer;
    text-shadow: .06em .06em 0 #000;
    display: inline-block;
  }
  #lang-screen .item::before {
    content: "";
    position: absolute;
    left: 0;
    top: 50%;
    transform: translateY(-50%);
    width: 1.05em;
    height: 1.05em;
    background: url("assets/hearth1.png") no-repeat center;
    background-size: contain;
    opacity: 0;
    image-rendering: pixelated;
  }
  #lang-screen .item.active::before {
    opacity: 1;
  }
  #lang-screen .item.active,
  #lang-screen .item:hover {
    color:var(--yellow);
    text-shadow:.08em .08em 0 #800;
  }
  #lang-screen .lang-note {
    color: rgba(255, 255, 255, 0.5);
    font-size: 0.55em;
    margin-top: 2em;
    text-align: center;
    max-width: 80%;
    font-family: "Mona Sans", sans-serif;
  }

  @media (max-width: 767px) {
    body {
      overflow-y: auto;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 2vh 0;
      min-height: 100vh;
    }
    #frame {
      width: 95vw;
      height: auto;
      aspect-ratio: 16/9;
    }
    #bottom-bar {
      position: relative;
      margin-top: 2vh;
      bottom: auto;
    }
  }
</style>
</head>
<body>
<div id="frame">
  <img id="fundo"  class="layer" src="assets/fundo.png"   alt="">
  <img id="shopbg" class="layer" src="assets/shop_bg.png" alt="">
  <div id="seam-wrap"><img id="seam" src="assets/Seam/idle_0.png" alt="Seam"></div>
  <img id="dialog" class="layer" src="assets/dialog1.png" alt="">
  <img id="plusbg" class="layer" src="assets/dialogplus.png" alt="">

  <div id="single" class="text-layer"></div>
  <div id="speech" class="text-layer"></div>
  <div id="pbar"><i></i><div class="ptext">0%</div></div>
  <div id="options" class="text-layer"></div>
  <div id="plus"    class="text-layer"></div>

  <img id="borda" class="layer" src="assets/borda.png" alt="">
  <div id="vignette"></div>
  <div id="version">Patcher v0.72</div>
  <input type="file" id="zipInput" accept=".zip" style="display:none">
  <div id="fade"></div>
  <div id="splash">
    <img src="assets/LogoDeltaruneVita.png" alt="DELTARUNE Vita">
    <div class="by">Patcher by Wolff</div>
    <div class="press">Clique ou pressione uma tecla</div>
  </div>
  <div id="lang-screen">
    <div class="title">SELECT YOUR LANGUAGE</div>
    <div class="menu" id="lang-items">
      <div class="item active" data-lang="ptbr">PORTUGUESE</div>
      <div class="item" data-lang="es">SPANISH</div>
      <div class="item" data-lang="en">ENGLISH</div>
      <div class="item" data-lang="fr">FRENCH</div>
      <div class="item" data-lang="de">GERMANY</div>
      <div class="item" data-lang="ru">РУССКИЙ</div>
      <div class="item" data-lang="ja">JAPONES</div>
    </div>
    <div class="lang-note">This only changes the language of the website, not the files generated by it.<br>If you are on a mobile device, I recommend using it horizontally.</div>
  </div>
</div>

<div id="bottom-bar">
  <div id="hint">â†‘â†“ move &nbsp; Z/Enter confirm &nbsp; X back</div>
  <div id="disclaimer">
    DELTARUNE Â© Toby Fox 2018-2026. All rights reserved.<br>
    DELTARUNE, its characters, music, and assets belong to their respective owners. This project does not distribute the commercial files required to play the game.
  </div>
</div>

<audio id="a-move" src="assets/snd_menumove.wav" preload="auto"></audio>
<audio id="a-sel"  src="assets/snd-select.ogg" preload="auto"></audio>
<audio id="a-swing" src="assets/snd_swing.wav" preload="auto"></audio>
<audio id="a-text" src="assets/snd_text.wav" preload="auto"></audio>
<audio id="a-lantern" src="assets/Lantern.ogg" loop preload="auto"></audio>
<audio id="a-appearance" src="assets/AUDIO_APPEARANCE.wav" preload="auto"></audio>
<audio id="a-drone" src="assets/AUDIO_DRONE.wav" loop preload="auto"></audio>
<audio id="a-doorclose" src="assets/snd_doorclose.wav" preload="auto"></audio>

<script src="manifest.js?v=0.71"></script>
<script src="assets/jszip.min.js"></script>
<script>
"use strict";
const $=id=>document.getElementById(id);

/* ================= Seam animation engine ================= */
const SEAM={
  idle:["idle_0","idle_1","idle_2","idle_3"],
  talk:["talk_0","talk_1","talk_2"],
  laugh:["laugh_0","laugh_1"],
  oh:["oh"],
};
const seamImg=$("seam");
let seamState="idle", seamFrame=0, seamHold=0;
function setSeam(state,holdMs){
  seamState=state; seamFrame=0; seamHold=holdMs?performance.now()+holdMs:0;
}
// variable-speed animation ticker
(function ticker(){
  const speeds={idle:170,talk:85,laugh:130,oh:400};
  let last=0;
  function loop(t){
    const spd=speeds[seamState]||160;
    if(t-last>=spd){
      last=t;
      if(seamHold && t>seamHold && seamState!=="talk") setSeam("idle");
      const frames=SEAM[seamState]||SEAM.idle;
      seamFrame=(seamFrame+1)%frames.length;
      seamImg.src="assets/Seam/"+frames[seamFrame]+".png";
    }
    requestAnimationFrame(loop);
  }
  requestAnimationFrame(loop);
})();
"""

# Extract tail part from sound_idx onwards
tail_part = content[sound_idx:]

full_html = head_part + "\n" + tail_part

# Write back full html
index_path.write_text(full_html, encoding="utf-8")
print("Rebuilt webpatcher/index.html successfully!")



