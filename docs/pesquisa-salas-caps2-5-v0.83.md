# Pesquisa local das salas pesadas, Caps. 2 a 5 (v0.83-4)

**Data:** 10/11/2026. **Método (PC):** runner desktop (`scratch/review-v081/survey.py`) abrindo 29
salas direto (`--start-room`), 423 quadros, perfil de GML a cada 100 quadros. A métrica é
**ops de VM por quadro**, que não depende do PC; no Vita cada op custa cerca de 0,3 a 0,5 µs.
Limites: o salto direto não dispara cenas nem batalhas, e os atalhos em C exclusivos do Vita
(fogos do Cap. 2, por exemplo) não rodam no PC.

## Aplicado na v0.83-4

| Objeto | Sala do teste | Ops/quadro antes → depois | Imagem | Ganho estimado no Vita |
|---|---|---|---|---|
| `obj_verlet_rope` (Draw, C exato) | `room_dw_fcastle_foyer` (Cap. 5) | 12278 → 8567 | idêntica (quadros 300, 301, 302, 420) | ~1,3 ms |
| `obj_conveyorbelt` (Draw, C exato) | `room_dw_cyber_maze_queenscreen` (Cap. 2) | 8685 → 5572 | idêntica | ~1,1 ms |
| `obj_grassanim_new` (Step a 1/3) | `room_dw_garden_floradinnencounter` (Cap. 5) | 13765 → 9822 | 56 pixels diferentes no quadro 420 (grama um passo atrás) | ~1,4 ms, e as superfícies do chão (`obj_plat_floortex_FLOOR`) passam a ser refeitas 1 vez em 3 |

A grama animada é uma perda visual aceita: os tiles mudam a cada 3 quadros em vez de todo
quadro, mantendo a velocidade e a fase da animação. Ela também existe nas salas de tabuleiro
do Cap. 3 (`room_board_*`), onde o log do Native GXM mostrava 4 ms por quadro.

## Próximos candidatos (maior custo medido)

| Ops/quadro | Script | Sala | Observação |
|---|---|---|---|
| 27348 + 20234 | `obj_firework_explosion` Step + Draw | `room_dw_cyber_post_music_boss_slide` (Cap. 2) | Já tem C no Vita (v0.82); falta log do Vita |
| 6176 | `obj_city_parallaxStreet_Draw_0` | `room_dw_city_traffic_2` (Cap. 2) | Desenho em mosaico + carros; bom para C exato |
| 4424 + 3168 | `obj_traffic_car` Draw + Step | `room_dw_city_traffic_2` (Cap. 2) | Step tem lógica de jogo (colisão) |
| 5385 (pico 58160) | `obj_rotating_tower_controller_new_Draw_0` | `room_dw_churchc_titanclimb1_tiled` (Cap. 4) | Já tem C parcial (v0.80-59); o pico de 58 mil ops precisa ser investigado |
| 3237 | `obj_climb_destructableclimbarea_Draw_0` | `room_dw_churchb_darkclimb` (Cap. 4) | Draw puro |
| 2757 | `obj_proxycandle_Step_0` | `darkclimb`, `angelprophecy_encounter` (Cap. 4) | Step com `scr_wave`/`scr_onscreen` |
| 3123 | `obj_mainchara_board_Step_0` | `room_board_1` (Cap. 3) | Controle do jogador; risco alto |
| 2788 + 2108 | `obj_dw_teevie_cameras_cheer_Draw_0`, `obj_dw_teevie_tv_screens_Draw_0` | `room_dw_teevie_large_01` (Cap. 3) | Draw puro |
| 3386 + 2757 | `obj_plat_fusuma_mask_Draw_0`, `obj_culling_manager_Draw_76` | `room_dw_fcastle_obscured_bullets` (Cap. 5) | A sala mais pesada do Cap. 5 no PC (29976 ops) |
| 2543 | `obj_dustball_Step_0` | `room_dw_couch_overworld_01` (Cap. 3) | |

Ferramentas: `scratch/review-v081/survey.py` (pesquisa), `survey_report.py` (ranking) e
`ab83.py` (A/B antes/depois).
