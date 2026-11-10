# Revisão de desempenho da v0.81 e plano para os Caps. 2 a 5 (v0.81-14)

**Data:** 10/11/2026. **Base:** código da `v0.81-13` (o working tree era idêntico ao backup
`artifacts/Backups/v0.81-13_20261108-155929` antes desta revisão).

Cada item diz de onde vem a evidência:

- **PC:** medido no runner desktop (`scratch/review-v081/`), com captura de tela e perfil de GML.
- **Log do Vita:** linhas `PERF_SUMMARY`, `DEV_SAMPLE` e `GML_TOP_STEP_DRAW` em `logs/chapter-log/`.
- **Código:** leitura do C atual contra o GML original (`artifacts/nxrune-border-analysis/chapterN-code`).
- **Hipótese:** ainda não medido; precisa de teste no Vita.

## 1. Estabilidade do frametime (30 e 60 FPS)

### Problema encontrado no limitador (Código + PC)

`VitaFramePacer_plan` (`src/vita-probe/source/vita_frame_pacer.c`), na versão da v0.81-10 a
v0.81-13: quando o quadro passava do prazo, o pacer avançava até o próximo ponto da grade de
33,3 ms e devolvia esse ponto como prazo; o laço do `playable_main.c` ficava girando até ele. Um
quadro de 34 ms ficava preso até 66,7 ms.

Simulação do laço real (`scratch/review-v081/pacer_test.c`, mesmo `vita_frame_pacer.c`):

| Trabalho por quadro | v0.81-13 | v0.81-14 |
|---|---|---|
| 30 FPS, 20 ms | 30,0 | 30,0 |
| 30 FPS, 34 ms | **15,0** | 29,4 |
| 30 FPS, 40 ms | **15,0** | 25,0 |
| 30 FPS, um pico de 36 ms a cada 6 quadros | **25,7** | 30,0 |
| 60 FPS, 20 ms | **30,0** | 50,0 |

Isso bate com os logs do Vita: em `logs/chapter-log/Chapter5/30 FPS Lag` (v0.81-9/10, modo
30 FPS), `room_town_mid` tem trabalho médio de 17 ms e só 125 a 132 quadros por janela de 5 s
(25 a 26 FPS). Com 17 ms de média, o limite seria 150 quadros; os quadros acima de 33 ms viram
66 ms e derrubam a média.

**Correção na v0.81-14:** quadro atrasado não espera mais. Se o atraso é menor que 1/4 do
período, a grade é mantida (a média continua 33,3 ms); se é maior, a grade é reancorada no
instante atual. Ganho esperado no Vita: salas que oscilam em volta de 33 ms deixam de cair para
15 FPS, e o modo 60 FPS deixa de virar 30 quando o quadro passa de 16,7 ms. **Hipótese** até o
log do Vita confirmar.

### VSync

A v0.81-11 deixou o VSync sempre ligado no modo 30 e a v0.81-12 voltou ao comportamento
anterior (`vsyncEnabled && (modo 2 || modo 0)`). Sem mudança aqui. Observação (log do Vita):
com `vsync=1`, o `work_us` de `room_dw_garden_meetflowery` fica em 16,5 ms de média com máximo
de 20 ms, sinal de que a espera do VBlank entra no tempo do quadro. Com o pacer corrigido,
isso não derruba mais o quadro seguinte.

## 2. Revisão das otimizações em C da v0.81

| Otimização | Veredito | Evidência |
|---|---|---|
| Grama das plataformas (`obj_plat_vertical_floor_grass_strip`) | Correta | PC: 3718 para 25 ops/quadro na `orange_gauntlet`, imagem idêntica. Pode melhorar: a chave do cache ainda lê o tilemap célula a célula a cada quadro. |
| Camadas do penhasco (`obj_parallax_cliffs_moving_layers`) | Correta | PC: `room_dw_cliff_shop`, quadros 120 e 240 idênticos ao GML com o caminho nativo ativo (`CH5_CLIFF_NATIVE=enabled`). O A/B anterior (`scratch/v081-ch5-c-candidates`) não ativava o caminho nativo (`native_cliff: false`), então não provava nada. |
| Sol do penhasco (`obj_parallax_cliffs_sunlayer`, v0.81-10) | **Errada, removida** | Código: o cache congelava a animação (`index++`), a cor do sol por fase, `sun_visible` e a posição (faltava `horizon_offset_default` = 266, `sunx`, `suny`, platswap). PC: na `room_dw_cliff_shop` a v0.81-13 some com o sol do horizonte (1130 pixels diferentes por quadro; `scratch/review-v081/shots/shop-240-gml-vs-v081-13.png`). O evento original custa 0,03 ms por quadro no PC; o "ganho de 15 a 20 ms" da doc da v0.81-10 não aparece. |
| Chão das plataformas (`obj_plat_floortex_FLOOR`, v0.81-10) | **Divergia, reescrita** | Código: desenhava a surface do chão em `x/y` com escala 1 (o GML usa `bbox_left/bbox_top` e `sprite_height/(surface_height*40)`), depois da frente em vez de antes, com a cor escurecida em vez de branco, congelava `darken_blend` e desenhava a surface da frente quando o GML desenha sprites. A versão nova segue o GML e só atua no estado estável com `draw_all_surfaces`. PC: imagens idênticas ao GML em `orange_gauntlet` e `garden_hopschef`. O GML desse objeto custa só 0,02 a 0,16 ms por quadro no PC, então o ganho real é pequeno. |
| Portas de correr (`obj_plat_fusuma_mask`) | Correta, ganho incerto | Código: só esconde durante o Draw as paredes totalmente fora da câmera e restaura em seguida. Sem sala de teste com paredes elegíveis no PC. |
| Plataformas azuis (`obj_blue_platform`) | Correta, ganho pequeno | PC: quadros idênticos; ganho dentro do ruído porque a sala aberta direto não dispara a sequência. |

Os testes `scripts/test-v081-*.py` da v0.81 só conferem se o texto do código existe; não
validam imagem nem desempenho.

## 3. Salas pesadas dos Caps. 2 e 3

### Cap. 2 (logs do Vita v0.80-17 a v0.80-46, VitaGL)

| Sala | Média por quadro | FPS mediano (janela 5 s) | Situação |
|---|---|---|---|
| `room_dw_cyber_post_music_boss_slide` | 26,7 ms (DEV: 60-64 ms nos fogos) | 28 (16 nos fogos) | `obj_firework_explosion`: Draw 18,4 ms + Step 16,5 ms por quadro (`GML_TOP_STEP_DRAW`). Já existe tradução em C (`vita_fireworks_native.inc`), adicionada depois desses logs e **nunca testada no Vita**. Teste prioritário. |
| `room_dw_cyber_maze_queenscreen` | 18,3 ms | 50 | Bom para 30; não segura 60. |
| `room_dw_city_traffic_2`, `roadblock`, `mice3` | 16-18 ms | 51-58 | Idem. |
| `room_dw_cyber_maze_fireworks` | sem log | - | Usa o mesmo objeto de fogos, mas a tradução em C está limitada à `post_music_boss_slide`; ali só há o desbaste de partículas. Candidato a estender. |
| `room_dw_mansion_*` | 8-16 ms | 45-59 | Picos de 400 a 800 ms só na entrada (carga de textura). |

Segundo candidato do Cap. 2: `obj_parallaxer_layer_cyber_Draw_0`, 4,9 ms por quadro na mesma sala
(1727 ns/op, quase tudo chamadas de desenho).

### Cap. 3 (só logs do Native GXM de 01/11, sem perfil VitaGL)

Os logs do Cap. 3 com dados de quadro são do renderer Native GXM, então o custo de desenho não
vale para o VitaGL. Os de lógica (Step) valem:

| Sala (índice do Cap. 3) | FPS mediano | Maior custo de lógica |
|---|---|---|
| `room_dw_snow_zone` (batalha do Cavaleiro) | 21 | `obj_encounterbasic` na entrada da batalha; `obj_knight_*` 1,5-2 ms |
| `room_board_1` | 22 | desenho ~30 ms no GXM; `obj_shadowman_sax_attack` 2 ms |
| `room_dw_teevie_large_01` | 22 | desenho ~29 ms no GXM |
| `room_board_dungeon_2` | 27 | `obj_b2d_intro` 7 ms na introdução |
| Salas do tabuleiro com `obj_grassanim_new` | 28 | 4 ms por quadro só da grama animada |

**Falta:** capturas DEV (`GML_TOP_STEP_DRAW`) no VitaGL dessas salas, pelo caminho normal.

## 4. O que fazer a seguir, em ordem

1. **Testar a v0.81-14 no Vita** em 30 FPS: `room_town_mid`/`north` (Cap. 5), batalha do Floradinn,
   penhascos (o sol deve voltar ao horizonte) e `room_dw_cyber_post_music_boss_slide` (Cap. 2; o log
   deve mostrar `VITAGL_CH2_FIREWORK_NATIVE=active`).
2. Se os fogos em C funcionarem, estender para `room_dw_cyber_maze_fireworks`.
3. Capturas DEV do Cap. 3 no VitaGL (salas da tabela acima) e do Cap. 4 (lista em
   `docs/candidatos-c-caps4-5-v0.80-59.md`).
4. Só depois disso portar mais GML: `obj_parallaxer_layer_cyber` (Cap. 2), `obj_grassanim_new` (Cap. 3),
   e um cache correto do sol do penhasco se o custo de GPU da surface dele aparecer no Vita.

## 5. Cenas abaixo de 20 FPS: simplificação visual aceita

Diretriz do Wolff (10/11): priorizar desempenho, aceitando perda visual moderada (por exemplo,
um céu mais simples) quando a cena fica injogável.

| Cena | Evidência | Simplificação proposta | Estado |
|---|---|---|---|
| Fogos, `room_dw_cyber_post_music_boss_slide` (Cap. 2) | Log do Vita: 16 FPS, fogos com 35 ms por quadro | Desenhar metade das partículas, em C | Feito na v0.81-15, sem teste no Vita |
| `room_dw_cliff_kawkawdash` (Cap. 5) | Log do Vita (v0.81-9): média 82 ms, 11 FPS. Fora dos picos, o desenho fica em 11-13 ms; os picos (74 a 671 ms, `draw_object=obj_parallax_cliffs_moving_layers`) coincidem com `evict=301` no `ROOM_TXTR`, ou seja, as camadas grandes do fundo entram e saem da memória de textura | Não desenhar a camada mais distante do fundo do penhasco nessa sala (céu mais liso) para liberar memória de textura | Precisa de um log da v0.81-15 antes: a v0.81-10 mexeu nessas salas e não há log depois dela |
| `room_dw_fcastle_obscured_bullets` (Cap. 5) | Log do Vita: 65 ms; parte é GPU com surfaces do tamanho da sala | Montar as máscaras das portas em meia resolução | A fazer |
| `room_dw_churchc_titanclimb1_tiled` (Cap. 4) | Log do Vita: 77 ms, antes da torre em C da v0.80-59 | Primeiro medir a torre em C; se ainda ficar abaixo de 20 FPS, reduzir a decoração da torre | Falta log |
| Batalha final, `room_dw_fcastle_flowery` (Cap. 5) | Log do Vita: 48 ms, maior parte antes da v0.80-57 | Fundo de penhasco já no modo Simple; próximo passo seria atualizar o reflexo/fundo a meia taxa | Falta log recente |

## Observação sobre o runner no PC

`gml_Script_pal_swap_set` custa 1,4 s por quadro no desktop em `room_dw_cliff_twirlflowerwind`
(e ~180 ms em outras salas do Cap. 5). É do backend desktop, não do Vita, mas impede A/B nessas
salas no PC.
