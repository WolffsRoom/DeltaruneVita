# Changelog

> Documentação técnica detalhada por build da série `v0.81`: consulte [`docs/builds-v0.81.md`](builds-v0.81.md).  
> Notas de lançamento oficiais e consolidadas para a atualização `v0.81`: consulte [`docs/releases/v0.81.md`](releases/v0.81.md).

## v0.86-1

- **Auto-teste pelo DELTARUNE Debug:** com o Vita na mesma rede e `Enviar reports local` ligado, o PC navega sozinho pelas salas de um roteiro (mesmo caminho seguro do navegador de salas DEV), grava um devlog por sala, segue o log ao vivo e monta um relatório (FPS e scripts GML mais caros por sala). Roteiros recomendados em `scripts/save-sync/autotest/ch1..ch5.txt`. Uso: `deltarune_vita_debug.py --autotest ch5`. Detalhes em `docs/debug-sync-protocol.md`.
- Servidor de debug: até 3 clientes ao mesmo tempo (app na bandeja + auto-teste). Comandos novos: `STATUS`, `ROOMS` e `CMD ROOM/CAPTURE/CAPTURE_STOP/MARK`.

## v0.85-4

- **Trocas de sala ~1 s mais rápidas:** a recarga dos sprites de batalha em toda porta (v0.84-4) saiu. O log da v0.85 mostrou `BATTLE_PRELOAD=rewarm` custando ~1 s a cada troca de sala (5 ou 6 páginas BC3 a ~180 ms cada, e o resto tentado de novo na porta seguinte). A recarga fica só no início de um encontro (limite de 0,4 s), e a proteção contra descarte continua.
- **Texto em lote:** no Draw do `obj_writer`, `obj_dmgwriter` e `DEVICE_MENU`, as letras entram num lote de quads, em vez de uma chamada de desenho por letra. O writer desenha um caractere por `draw_text`, e no log a soma era ~10 s de desenho de `obj_writer`. Qualquer troca de estado (blend, shader, fog, alfa, alvo, câmera, clear) esvazia o lote antes, então a ordem e a aparência não mudam.

## v0.85-3

- **CRT no primeiro uso:** com um filtro CRT já escolhido, os shaders (e a máscara do tubo) são preparados no boot. O primeiro quadro com CRT em jogo custava 0,31 s de compilação (`room_shop_ch5`, `gui_us=310707`). Não era erro: as linhas `VITAGL_CRT_MOD` são essa compilação.
- **Batalha da kawkawdash:** cada `obj_kawkaw_streamer` desenha um rastro de 15 segmentos, e cada segmento era uma chamada de desenho separada (até 1,3 s a cada ~60 quadros no log). Agora o rastro de cada instância sai numa chamada só.
- **yellowblue:** o limite de 52 MiB vale para qualquer entrada vinda do Flower Castle (o log da v0.84-3 entrou pela seth_encounter e ficou em quadros de 86 a 104 ms). O corte para 28 MiB fica para saltos de outra região.

## v0.85-2

- **Início de batalha no Capítulo 5 (kawkawdash):** com a VRAM apertada nas salas do penhasco (orçamento de 24 MiB), o descarte de emergência ainda tirava os sprites de batalha, e a luta os reenviava nos primeiros quadros (quadros de 0,6 a 1,4 s). Agora eles são a última opção de descarte. Quando saem, voltam na troca de sala seguinte ou no instante em que o encontro começa (`obj_encounterbasic`), no mesmo quadro que o jogo já gasta montando a luta, com limite de 0,4 s.

## v0.85-1

- **room_dw_fcastle_obscured_bullets (e demais salas com paredes fusuma):** a camada de silhuetas (`obj_plat_fusuma_mask`, um surface de 640x480 limpo e redesenhado com todas as paredes, portas, personagens, balas, partículas e plataformas a cada quadro) agora é refeita a 15 Hz. Nos quadros intermediários, as chamadas de desenho dentro desse surface são ignoradas e a imagem anterior continua sendo composta normalmente (`CH5_FUSUMA_MASK_REUSE=15hz`). Step, movimento e colisões seguem a cada quadro, e as silhuetas atrasam no máximo um quadro. Usa o mesmo mecanismo das torres da titanclimb2 do Capítulo 3.
- Inclui os ajustes da v0.84-4 abaixo (batalha da Floradinn, yellowblue, CRT Agressivo em tela cheia).

### Ajustes que vieram da v0.84-4

- **Batalha (Chapter 5, Floradinn):** os sprites de batalha do grupo, pré-carregados no Load Chapter, não são mais descartados pela limpeza de textura das trocas de sala. Depois de uma sala que libera tudo (o vídeo do jardim), voltam na troca de sala seguinte, por trás da transição (`BATTLE_PRELOAD=rewarm`). Antes, 12 páginas entravam no primeiro quadro da luta (2,2 s).
- **room_dw_fcastle_yellowblue:** voltando da escada vizinha, a sala mantém as próprias texturas (limite 52 MiB). O corte para 28 MiB fica só para as outras entradas, e a sala não recarrega mais as plataformas durante o jogo.
- **CRT Agressivo em tela cheia:** a borda (estática ou animada) e o jogo são compostos juntos e curvados numa passada só. A curvatura vai numa malha 48x28 e a vinheta, a borda do tubo e a grade numa máscara pré-calculada, e o shader faz só 3 leituras de textura. O shader antigo custava ~15 ms por quadro (cliff_verticalwind 17 -> 32 ms). As faixas de scanline separadas para a borda saíram. Se o novo caminho falhar, volta o tubo da v0.82. Prévia no PC: `scripts/preview-crt-full.py`.

## v0.84-3

- DEBUG: "Enviar log atual" (upload HTTP, nunca configurado) virou **Enviar reports local** (Ligado/Desligado, `debug_report_sync=1`).
- Novo app de PC **DELTARUNE Debug** (coração amarelo): com o Vita na mesma rede, recebe os devlogs e as capturas novas (`ux0:picture/SCREENSHOT/Deltarune/`) feitos enquanto ele está aberto e mostra o log do probe ao vivo (`native-gxm-probe.log` no GXM, `butterscotch-probe.log` no VitaGL). Detalhes em `docs/debug-sync-protocol.md`.
- Native GXM: as configurações não apagam mais `save_sync` e `debug_report_sync` do `config.ini` ao salvar.

## v0.84-2

- CRT Agressivo: o filtro de scanlines agora também cobre as faixas acima e abaixo da imagem quando ela não ocupa a altura toda (borda dinâmica). Antes só as faixas laterais recebiam o filtro.
- App do PC (DELTARUNE Sync): idioma configurável (os 9 do port), texto dentro da borda, título nítido, voz de escrita só na abertura, som de conexão/desconexão e som de save a cada cópia.

## v0.84-1 — Sincronização de saves PC ↔ Vita pela rede local (opcional)

Detalhes e protocolo: [`docs/save-sync-protocol.md`](save-sync-protocol.md). Sem teste no Vita.

- **Vita** (`vita_save_sync.c`): com a opção **Sistema > Sincronizar saves (PC)** ligada (`save_sync=1`, desligada por padrão), o jogo anuncia o Vita por broadcast UDP (26540) e atende o app do PC por TCP (26541): lista, envia e recebe só `dr.ini`, `vacationmemories.png` e `filech*`, com gravação atômica e backup em `deltarune_saves/sync_backup/`. Thread de baixa prioridade, só com o jogo aberto.
- **PC** (`scripts/save-sync/deltarune_vita_sync.py`): encontra o Vita, sincroniza a cada 3 s, guarda backups, espera o DELTARUNE fechar antes de gravar no PC e compara o `dr.ini` normalizado. Bandeja com `pystray`, ou console.
- **Teste local**: o mesmo C compilado como servidor no Windows + o cliente, com saves reais do Cap. 5: todos os casos passaram (descoberta, as duas direções, backups, idempotência, nome inválido recusado).

## v0.83-6 — Batalhas mais leves, CRT Agressivo nas bordas e Save Anywhere

Sem teste no Vita. Análise dos logs da v0.83-4 (`room_dw_fcastle_flowery`): 257 envios BC3, 245 despejos LRU e 104 adiamentos durante a batalha, com ~6 MiB de VRAM livre (atlas ~40 MiB + 35 superfícies ~21 MiB). O pico de 2,5 s (quadro 18260, 1,85 s de Step) coincide com a troca de música para `Flowerman_Arrangement.ogg` (4,4 MiB).

- **Música longa**: faixas acima de 2 MiB são lidas do cartão aos poucos (buffer stdio de 256 KiB, ~15 s de áudio) em vez de inteiras na thread principal na hora da troca.
- **BC3 durante batalhas (modo Otimizado)**: páginas carregadas enquanto existe `obj_battlecontroller` usam BC3 (metade do tamanho de RGBA4444): menos bytes no início da batalha e menos despejos. Fontes, UI e diálogo continuam sem perda.
- **CRT Agressivo**: scanlines mais fortes no tubo (0,55 a 1,0) e o mesmo filtro de scanlines/grade do CRT Suave/Médio nas bordas do console, recortado fora da imagem do jogo.
- **Save Anywhere**: segurar L + R e apertar SELECT abre o menu de save do próprio capítulo (`obj_savemenu`, o mesmo objeto do ponto de save) quando o jogador está livre (`global.interact == 0`, sem batalha, diálogo ou menu). O log mostra `SAVE_ANYWHERE=opened` ou o motivo da recusa. No PC o menu abre nos Caps. 2 a 5, mas sem texto, porque a sala foi aberta direto sem a inicialização do jogo.

## v0.83-5 — Correções do teste da v0.83-4 (torre da batalha final, penhascos, intro do Cap. 2)

- **Torre da batalha final de volta**: removido o limite de ~3 MiB de texturas novas por quadro da v0.83-1. No log da v0.83-4 ainda havia travadas de 2,5 s, e as páginas que a torre usa nos quadros em que é redesenhada (3,75 Hz) perdiam a vez sempre, então a torre sumia.
- **Penhascos (`room_dw_cliff_kawkawdash`)**: log da v0.83-4 com média de 47 ms (~20 ms de Step por quadro). `obj_dw_cliffs_tallgrass` Step_2 ganhou C exato para a grama não tocada: no PC, 20285 → 12541 ops por quadro, imagem idêntica.
- **Sol do penhasco**: o evento que redesenha a surface do sol roda 1 quadro em 3; nos outros a mesma surface é desenhada acompanhando a câmera (perda visual aceita: o brilho do sol anima a 1/3). No PC, ~60 a 100 pixels de diferença por quadro na `room_dw_cliff_shop`.
- **Intro duplicada**: removido o desvio forçado para `PLACE_LOGO` (v0.81-9/10). Capítulos zerados tocavam `AUDIO_INTRONOISE` duas vezes (o fluxo original já passa por `PLACE_LOGO`), e capítulos não zerados tocavam uma vez sem dever. Agora vale só o fluxo original do jogo.

## v0.83-4 — Três atalhos em C encontrados pela pesquisa local dos Caps. 2 a 5

Detalhes: [`docs/pesquisa-salas-caps2-5-v0.83.md`](pesquisa-salas-caps2-5-v0.83.md). Medido no PC; sem teste no Vita.

- `obj_verlet_rope` (Draw, C exato): foyer do castelo 12278 → 8567 ops por quadro, imagem idêntica.
- `obj_conveyorbelt` (Draw, C exato): `room_dw_cyber_maze_queenscreen` 8685 → 5572, imagem idêntica.
- `obj_grassanim_new` (Step em 1 de cada 3 quadros, perda visual aceita): `room_dw_garden_floradinnencounter` 13765 → 9822, e o chão das plataformas é refeito com a mesma frequência.

## v0.83-3 — libshacccg.suprx aceito também em ur0:data/external/

- O jogo e o launcher aceitam `ur0:data/libshacccg.suprx` ou `ur0:data/external/libshacccg.suprx`, os dois caminhos que o vitaGL usa (`third_party/vitaGL-nosplash/source/gxm.c`, `start_shader_compiler`). Até a v0.83-2 só o primeiro era aceito, e quem tinha o arquivo só em `external/` era barrado logo depois do `vglInit`.

## v0.83-2 — Aviso claro quando falta o libshacccg.suprx

- **Launcher** (`src/vita-launcher/main.c`): confere `ur0:data/libshacccg.suprx` antes de abrir o VitaGL e, se faltar, mostra uma tela de texto (PT/EN) pedindo o ShaRKF00D, e grava `launcher libshacccg_missing` no `boot_stages.txt`. Um usuário cujo `boot_stages.txt` parava em `vitagl_init_complete` via só tela preta: o diálogo nativo do vitagl.bin não aparecia e o jogo fechava antes de criar o config.ini.

## v0.83-1 — Prólogo em todos os idiomas dentro da caixa, CRT Agressivo com curvatura real, loading mais curto e batalha final sem congelar nas trocas de fase

Detalhes e evidências: [`docs/v0.83-prologo-crt-loading-batalha-final.md`](v0.83-prologo-crt-loading-batalha-final.md). Nada testado no Vita.

- **Prólogo**: glifos japoneses e cirílicos na linha de base latina e tamanho automático por fala (no PC, 18 telas com texto fora da caixa passaram a 0); seleção de idioma com pontos de posição, cartão mais legível e sem "Русский" duplicado. Avisos da árvore centralizados com fade ("Aproxime-se da árvore..." de longe, "Aperte X" de perto) e nome do idioma em amarelo com bounce suave. Prévia: `scripts/render-prologue-preview.py`.
- **CRT Agressivo**: shader de tubo no lugar do blit final da `application_surface` (curvatura de barril, uma scanline por linha nativa, grade RGB, vinheta, reflexo), uma passada, sem read-back; volta ao overlay antigo se falhar. Prévia: `scripts/preview-crt-tube.py`.
- **Loading**: pré-carga de batalha com orçamento de 1,5 s (era ~4,9 s no Cap. 5 com cache válido); `boot_stages.txt` com o tempo de cada etapa.
- **Batalha final**: no máximo ~3 MiB de páginas novas por quadro durante `room_dw_fcastle_flowery` (UI/grupo fixos continuam imediatos), contra quadros de 0,5 a 2,75 s nas trocas de fase do log v0.80-57.

## v0.82 (build v0.81-15) — Fogos do Cap. 2 mais leves e registro de inicialização sem devmode

- **Fogos do Cap. 2** (`vita_fireworks_native.inc`): na `room_dw_cyber_post_music_boss_slide`, onde os fogos custavam ~35 ms por quadro no Vita (16 FPS), o caminho em C desenha uma partícula a cada duas (todas continuam se movendo). O caminho em C passa a valer também em `room_dw_cyber_maze_fireworks` e `room_dw_city_postbaseball_1/2`, que já descartavam parte das explosões na criação e desenham todas as partículas. Perda visual aceita: fogos menos densos. Sem teste no Vita; o log mostra `VITAGL_CH2_FIREWORK_NATIVE=active ... draw_stride=`.
- **Etapas de inicialização** (`playable_main.c`, `boot_stage`): `ux0:data/deltarune/deltarunevita/boot_stages.txt` recebe uma linha por etapa (pastas, saves, `vglInit`, `libshacccg`, dados, config.ini, vídeo, primeiro quadro), mesmo sem devmode. Antes, um crash antes de `VitaSettings_load` não deixava nem config.ini nem log; a última linha do arquivo agora diz onde a abertura parou.

## v0.81-14 — Limitador de FPS sem queda para 15/30 FPS e correção de dois atalhos em C da v0.81-10

Detalhes, medições e plano dos Caps. 2 e 3: [`docs/revisao-desempenho-v0.81-14.md`](revisao-desempenho-v0.81-14.md).

- **Limitador de FPS** (`src/vita-probe/source/vita_frame_pacer.c`): um quadro atrasado não espera mais o próximo ponto da grade. Antes, um quadro de 34 ms no modo 30 virava 66,7 ms (15 FPS) e um de 20 ms no modo 60 virava 33,3 ms (30 FPS). Atraso menor que 1/4 do período mantém a grade; maior reancora no instante atual. Simulação no PC: 34 ms passa de 15 para 29,4 FPS; 20 ms no modo 60 passa de 30 para 50 FPS. Sem teste no Vita.
- **Sol do penhasco** (`runner.c`, `Runner_tryNativeCliffLayersDraw`): removido o cache da surface de `obj_parallax_cliffs_sunlayer` da v0.81-10, que congelava animação, cor e `sun_visible` e desenhava o sol cerca de 266 px acima do horizonte (no PC, o sol sumia da `room_dw_cliff_shop`). O evento original volta a rodar a cada quadro; custa 0,03 ms no PC.
- **Chão das plataformas** (`vm_builtins.c`, `VMBuiltins_tryFloortexDraw`): reescrito para seguir o GML (posição, escala vertical, ordem, cor e `darken_blend` por quadro) e atuar só no estado estável com `draw_all_surfaces`. Imagens idênticas ao GML no PC em `orange_gauntlet` e `garden_hopschef`.

## v0.81-11 — Resolução Definitiva da Dupla Barra de TP no Floradinn, Eliminação do Thrashing de Memória em Castle Town/Hometown (Trim de 56 MiB) e VSync Incondicional a 30 FPS

- **Eliminação Definitiva da Dupla Barra de TP no Floradinn (`room_dw_garden_floradinnencounter`)**:
  - **O que foi feito**: em `src/butterscotch/src/runner.c` (`Runner_executeResolvedEvent` e `Runner_drawViews`), `obj_sakurafubuki_new` detecta transição concluída (`stage >= 2 || pct >= 2.0`) ou batalha ativa (`obj_tensionbar` / `obj_battlecontroller`), reseta `surf_view1 = -4`, retorna incondicionalmente de qualquer evento de Draw e desativa a câmera secundária (`views[1].enabled = false; views[1].surfaceId = -1;`); em `src/butterscotch/src/gl_legacy/gl_legacy_renderer.c` (`glLegacyDrawSurface`), removida a busca por instâncias externas de sakura e restringida a interceptação apenas quando a chamada de desenho parte da própria instância de `obj_sakurafubuki_new` com dimensões diferentes de 640x480.
  - **Por quê**: ao encerrar a animação de pétalas, o GameMaker liberava `surf_view1` mantendo o ID na variável. Ao entrar em batalha, a surface legítima da barra de TP (`healthbar_surf`, 96x250) recebia o mesmo ID reciclado, fazendo o evento `Draw_73` desenhar a barra de TP espelhada em `(0, 0)` no canto da tela, enquanto o renderer anterior suprimia desenhos legítimos do `obj_tensionbar`.
  - **Pra quê**: eliminar completamente a barra de TP duplicada em `(0, 0)` e garantir que a barra oficial de TP desenhe normalmente com todos os textos e glifos na coordenada correta (`x ≈ 42`).
- **Eliminação do Thrashing de Memória em Castle Town e Hometown no Chapter 5**:
  - **O que foi feito**: em `src/butterscotch/src/runner.c` (`Runner_handlePendingRoomChange`), adicionadas regras de 56 MiB para `vitaSharedDarkCastleTransition` e `vitaSharedTownTransition` no Chapter 5; e em `src/butterscotch/src/gl_legacy/gl_legacy_renderer.c` (`GLLegacyRenderer_prepareRoomTextureSet`), estendida a regra de preaquecimento de atores dinâmicos do Dark World (`spr_krisd`, `spr_susied`, `spr_ralseid`, etc.) para o Chapter 5.
  - **Por quê**: transições entre salas de Castle Town e Hometown caíam no fallback de 32 MiB de VRAM, gerando expurgos de 12 a 20 MiB seguidos de recodificação imediata e travadas de 90 a 195 ms por porta.
  - **Pra quê**: manter os atlas de Kris, Susie, Ralsei e cenário aquecidos, eliminando travadas e oscilações ao transitar entre salas do castelo e da cidade.
- **VSync Incondicional a 30 FPS (`waitForVblank = true`)**:
  - **O que foi feito**: em `src/vita-probe/source/playable_main.c`, `waitForVblank` agora é verdadeiro sempre que `settings.fpsTargetMode == 0` (30 FPS), independentemente da opção `vsyncEnabled`.
  - **Por quê**: com VSync desativado na UI, o modo 30 FPS realizava trocas de buffer sem sincronização vertical, causando tearing e descompasso com os 60 Hz do LCD do Vita.
  - **Pra quê**: assegurar cadência perfeita de 33,333 ms (exatamente 2 ciclos de VBlank por quadro) a 30 FPS.

## v0.81-10 — Resolução da queda a 30 FPS (Pacer Bugfix), Fastpaths Nativos em C para Ch. 4/5 (Floortex & Sunlayer), Glifos DualShock 4 no Krákrá e Prevenção da Dupla Barra de TP

- **Frame Pacer / VSync a 30 FPS (eliminação de queda abrupta de 30 para 15 FPS)**:
  - **O que foi feito**: em `src/vita-probe/source/vita_frame_pacer.c` (`VitaFramePacer_setRate`, `VitaFramePacer_reset`, `VitaFramePacer_plan`), inicialização do deadline em `nowUs + period`, remoção do avanço de deadline incondicional no início de `plan()`, retorno imediato de `coarseSleepUs = 0` quando há atraso de quadro e avanço do deadline em passos inteiros até o próximo múltiplo do período no final do plano.
  - **Por quê**: nos logs de Hometown (`room_town_north`), o jogo rodava solto a 46 FPS no modo 60 FPS/Unlocked, mas desabava para 15 FPS ao travar em 30 FPS. O pacer executava um double-advance no topo e no laço de atraso, adiando o deadline em 2 períodos (~66 ms) e impondo um sleep forçado de ~55 ms via `sceKernelDelayThread`.
  - **Pra quê**: estabilizar o frame time a exatos 33,333 ms em 30 FPS contínuos, acabando com as quedas artificiais induzidas pelo frame pacer.
- **Fastpaths Nativos em C para Salas Pesadas do Cap. 4 e 5 (`obj_plat_floortex_FLOOR` e `obj_parallax_cliffs_sunlayer`)**:
  - **O que foi feito**: implementado `VMBuiltins_tryFloortexDraw` em `src/butterscotch/src/vm_builtins.c` interceptando `gml_Object_obj_plat_floortex_FLOOR_Draw_0` e variante `AlignBottom1TILE`, ignorando plataformas culled/não geradas e desenhando superfícies assadas (`front_surf`, `surf`, `back_surf`) diretamente em C sem re-interpretar 9 KB de bytecode GML; e em `src/butterscotch/src/runner.c` (`Runner_tryNativeCliffLayersDraw`), cache da surface 400x300 de `obj_parallax_cliffs_sunlayer`, desenhada via vtable sem re-executar `OTHER_USER0` todo frame.
  - **Por quê**: nos penhascos (`room_dw_cliff_twirlflowerwind`) e no castelo, `floortex` consumia 33,7 ms/frame em centenas de chamadas GML e criava 3 superfícies por plataforma, inflando a VRAM para 96,5 MB e acionando o LRU do VitaGL, que desalocava texturas essenciais (ataques dos inimigos, Kris, Krákrá e chão).
  - **Pra quê**: elevar o FPS nos penhascos/castelo e evitar a exaustão de VRAM, mantendo todos os ataques, chão e personagens visíveis.
- **Glifos DualShock 4 / PS4 na Batalha do Krákrá (`room_dw_cliff_kawkawdash`)**:
  - **O que foi feito**: em `src/vita-probe/source/playable_main.c`, chamada da nova função `vitaEnsureGamepadActive` logo após `Runner_step(runner)`, garantindo `obj_gamecontroller.gamepad_active = true`, `gamepad_id = 0` e `gamepad_type = "Sony DualShock 4"` antes da etapa de Draw.
  - **Por quê**: `obj_gamecontroller_Step_1` resetava `gamepad_active = 0` para o slot 0 do Vita, fazendo com que o texto de ACT exibisse `[Z]` e `[X]` do PC.
  - **Pra quê**: exibir ícones e botões nativos do controle PS4 na interface e batalhas.
- **Prevenção Definitiva da Dupla Barra de TP no Floradinn**:
  - **O que foi feito**: em `src/butterscotch/src/runner.c`, `obj_sakurafubuki_new` detecta a presença de `obj_tensionbar` em qualquer sala; ao encontrar, reseta `surf_view1 = -4` e aborta `Draw_72` e `Draw_73`.
  - **Por quê**: `Draw_73` desenhava `surf_view1` em `(camerax(), cameray())`. A reciclagem do mesmo ID de surface pelo `healthbar_surf` gerava uma segunda barra duplicada no topo esquerdo da tela.
  - **Pra quê**: eliminar completamente a barra de TP duplicada.
- **Som de Inicialização (`AUDIO_INTRONOISE.ogg`) no Cold Boot do Chapter 5**:
  - **O que foi feito**: em `src/vita-probe/source/playable_main.c`, transição para `PLACE_LOGO` assegurada via flag persistente `s_has_shown_boot_logo` no primeiro carregamento de `PLACE_MENU`.
  - **Por quê**: Chapter 5 inicia por `room_intro_ch5` em vez de `INITIALIZE`, desviando da checagem anterior.
  - **Pra quê**: reproduzir a vinheta e o áudio `AUDIO_INTRONOISE.ogg` ao iniciar o jogo no Vita.

## v0.81-9 — Bordas animadas sem corte visível, VSync/estabilidade de frames a 30 FPS e intro noise `AUDIO_INTRONOISE.ogg` no boot

- **Bordas Animadas (eliminação do corte seco entre parte móvel e estática)**:
  - **O que foi feito**: em `src/vita-probe/source/vita_borders.c` (`drawBorderCurrent` e `drawBorderQuadFeathered`), renderização garantida da borda estática completa de fundo (`borderTexture`) e divisão das colunas animadas laterais (`frames_side.bani`) em faixa sólida externa e faixa de transição interna de 24 pixels com gradiente linear suave de alfa (`alpha -> 0.0` / `0.0 -> alpha`).
  - **Por quê**: os quads rígidos de 120 pixels criavam uma linha visível vertical de corte entre os pixels que movimentavam e a arte estática do fundo em qualquer zoom (especialmente no zoom de 88% de bordas de console).
  - **Pra quê**: eliminar a linha de emenda e mesclar a animação de forma suave e contínua com a arte estática de fundo.
- **VSync e Estabilidade de Quadros a 30 FPS**:
  - **O que foi feito**: inclusão de `settings.fpsTargetMode == 0` em `waitForVblank` quando `settings.vsyncEnabled` estiver ativo em `src/vita-probe/source/playable_main.c`, e avanço de prazo em múltiplos inteiros do período em `VitaFramePacer_plan` (`src/vita-probe/source/vita_frame_pacer.c`).
  - **Por quê**: 30 FPS estava com `waitForVblank` desativado, sofrendo trocas assíncronas sem sincronia com o LCD de 60 Hz do Vita, além de jitter do kernel e perda de fase do pacer a cada milissegundo de atraso.
  - **Pra quê**: travar o frame time de 30 FPS a exatos 2 ciclos de VBlank (33,333 ms constantes), acabando com os micro-engasgos e oscilações de cadência.
- **Áudio `AUDIO_INTRONOISE.ogg` antes do Menu Inicial**:
  - **O que foi feito**: redirecionamento de cold boot de `ROOM_INITIALIZE -> PLACE_MENU` para a sala `PLACE_LOGO` em `src/vita-probe/source/playable_main.c`, garantindo a execução de `PROCESS_LOGO`, volume SFX ativo e liberação imediata do áudio (`AlAudioSystem_setTransitionHold(audio, false)`).
  - **Por quê**: no código original, `menu_go == 1` pulava direto para `PLACE_MENU` em silêncio (o logo só tocava com capítulo zerado `menu_go == 2`), e o `transitionHold` mantinha o áudio silenciado.
  - **Pra quê**: tocar a vinheta clássica e o áudio `AUDIO_INTRONOISE.ogg` ao inicializar o jogo antes do menu em qualquer circunstância.

## v0.81-8 — Chapter 5 (dupla barra de TP, FPS antes/depois da transição de pétalas, travada de troca de sala, penhascos/Krákrá, glifos PS4, bordas de Hometown/floricultura e Room Navigator)

- **Jardim (`room_dw_garden_floradinnencounter` e `room_dw_garden_mushrooms`)**:
  - **O que foi feito**: limpeza de `surf_view1 = -4` ao atingir `transition_stage >= 2` em `Runner_executeEvent` (`src/butterscotch/src/runner.c`) e `glLegacyDrawSurface` (`src/butterscotch/src/gl_legacy/gl_legacy_renderer.c`), remoção do throttle de frames ímpares da `view 1` durante `transition_stage == 0`, supressão de `obj_blocktree_bg` na `view 0` enquanto coberta por `surf_view1`, frustum culling das árvores fora da câmera, preload de `gardenCore`, inclusão de `vitaSharedGardenTransition` / `vitaSharedCliffTransition` em `vitaSharedTransition` e agrupamento em lote (`vitaBatchRoomTrimActive`) de `glFinish()` / `vglForceGarbageCollection()` na troca de sala.
  - **Por quê**: `obj_sakurafubuki_new` liberava `surf_view1` sem zerar a variável, fazendo `Draw_73` desenhar a surface reciclada `healthbar_surf` (`96x250`) da barra de TP em `(0, 0)`; pular a `view 1` em frames ímpares durante `stage == 0` derrubava a tela visível para 15 FPS antes da transição; e a saída para `room_dw_garden_mushrooms` executava 68 `glFinish() + vglForceGarbageCollection()` síncronos (`2,24 s`).
  - **Pra quê**: eliminar a dupla barra de TP na batalha contra Floradinn, manter 30 FPS antes e depois da transição das pétalas e eliminar a travada ao mudar de sala.
- **Penhascos (`room_dw_cliff_kawkawdash` e `room_dw_cliff_twirlflowerwind`) e Glifos PS4**:
  - **O que foi feito**: `VitaGpuRules_ch5CliffSimpleMode` (`src/vita-probe/source/vita_gpu_rules.c`) e `vitaEnableChapter5CliffSimpleMode` (`src/butterscotch/src/runner.c`) agora cobrem todas as salas `room_dw_cliff_*` e a subclasse `obj_parallax_cliffs_moving_layers_back`; surfaces intermediárias dos penhascos usam `GL_RGBA4` (`~9,7 MiB` poupados); `visualCore` fixa todas as variantes `spr_kawkaw_tongue`, `spr_kawkaw_sparkle`, `spr_kawkaw_hurt*`, `spr_kawakaw_happy`, `spr_featherbullet`, grama e personagens; e `playable_main.c` força `obj_gamecontroller.gamepad_active = true` com `"Sony DualShock 4"`.
  - **Por quê**: `obj_parallax_cliffs_moving_layers_back` escapava do filtro por nome exato e consumia `~10 ms/frame` em GML; ataques múltiplos do Krákrá usavam sprites com grafia `spr_kawkaw_*` / `spr_kawakaw_*` ausentes do pin list; e `obj_gamecontroller.gamepad_active` permanecia `0` no slot `0`, exibindo `[Z]`/`[X]` do PC.
  - **Pra quê**: estabilizar o FPS nos penhascos, manter visíveis o chão, personagens e ataques múltiplos do Krákrá e exibir os glifos do PS4.
- **Bordas (`room_town_mid`, `room_flowershop_1f` e `room_flowershop_2f`) e Room Navigator**:
  - **O que foi feito**: adicionadas `room_flowershop_1f` e `room_flowershop_2f` em `VitaBorderRules_shouldBlankSurround` (`src/vita-probe/source/vita_border_rules.c`); ignorado o override estático diurno `border_lw_town_0.png` em salas de Hometown no Chapter 5 (`src/vita-probe/source/vita_borders.c`); e atualizados `VitaSettings_drawDevNavigatorHint` e `VitaSettings_drawDevQuickScenes` (`src/vita-probe/source/vita_settings.c`) para mostrar `232 - room_name` e os glifos `L`, `R` e `X` abaixo do título.
  - **Por quê**: `borders_config.txt` forçava a borda de dia em `room_town_mid` mesmo à noite, a floricultura não usa borda, e o Room Navigator precisava exibir o índice da sala atual e os glifos de botões na janela maior.
  - **Pra quê**: corrigir as bordas de Hometown/floricultura no Chapter 5 e melhorar a leitura do Room Navigator no modo Debug.

## v0.81-7 — transição diagonal de pétalas (`shd_petalcut` + cortina `pt_petal_whirl`) e persistência do `CRT Agressivo`

- **Transição de pétalas (`room_dw_garden_floradinnencounter`)**:
  - **O que foi feito**: `glLegacyDrawSurface` e `glGpuSetShader` (`src/butterscotch/src/gl_legacy/gl_legacy_renderer.c`) agora aplicam o recorte oblíquo de `shd_petalcut` diretamente ao desenhar `surf_view1` sobre a `view 0` em faixas horizontais de `4px` (`lineTop = 690 - pct*640` até `lineBottom = 1130 - pct*640`), mantendo `currentShader = -1` na `view 1`. Em `draw_ch5_sakura_host` (`src/vita-probe/source/playable_main.c`), foi adicionada a cortina diagonal densa de 96 pétalas (`pt_petal_whirl`) acompanhando `transition_percent` ao longo da linha de recorte dentro do viewport 4:3.
  - **Por quê**: no log `butterlog1-room_dw_cliff_kawkawdash_20261107-123708.log`, `shd_petalcut` caía em `reason=unsupported`, fazendo `surf_view1` (árvores vermelhas) cobrir 100% da tela até o fim da transição em vez de revelar gradualmente as árvores rosas, e `draw_ch5_sakura_host` apenas espalhava pétalas soltas pela tela inteira sem formar a faixa diagonal vista no PC.
  - **Pra quê**: reproduzir fielmente a varredura diagonal de pétalas rosas trocando as árvores vermelhas e a grama escura pelo jardim rosa como no PC, preservando o frustum culling e o batching na `view 1`.
- **Persistência do filtro `CRT Agressivo`**:
  - **O que foi feito**: `VitaSettings_save` e `VitaSettings_load` (`src/vita-probe/source/vita_settings.c`) passam a gravar `screen_filter=crt-aggressive` e aceitar tanto `crt-aggressive` quanto `crt-advanced` ao carregar, igualando `experiments/native-gxm/vita_settings_native.c`.
  - **Por quê**: evitar perda da opção selecionada ao reiniciar ou alternar entre os backends VitaGL e Native GXM.
  - **Pra quê**: manter a configuração do filtro `CRT Agressivo` consistente em `config.ini`.

## v0.81-4 a v0.81-6 — FPS e texturas nos penhascos, barra de TP, bordas animadas no Cap. 5 e filtros CRT (`Suave`, `Médio`, `Agressivo`)

- **Penhascos (`room_dw_cliff_kawkawdash` e `room_dw_cliff_twirlflowerwind`)**:
  - **O que foi feito**: removida a exigência de `__transition_yscale` em `Runner_tryNativeCliffLayersDraw` (`src/butterscotch/src/runner.c`) e aplicado `vitaPinSpritePagesByName` em `visualCore` (`spr_kakaw_*`, `spr_kawkaw_small`, `spr_cliff_crater`) e `windCore` (`spr_plat_twirlflower_vine`, `spr_plat_twirlflower_petals`) em `GLLegacyRenderer_prepareRoomTextureSet` (`src/butterscotch/src/gl_legacy/gl_legacy_renderer.c`).
  - **Por quê**: salas sem `obj_platswap` não possuíam `__transition_yscale`, derrubando o fundo do penhasco de volta para o Draw interpretado lento (`580 ms+` na entrada), enquanto as páginas de ataque do inimigo e das flores de vento eram apenas marcadas e sofriam evicção LRU sob o teto de 40–50 MiB, deixando o ataque invisível e sumindo com texturas.
  - **Pra quê**: ativar o fast-path nativo em C nos penhascos e manter residentes na VRAM os sprites de ataque e vento durante toda a sala.
- **Moldura duplicada na barra de TP (`obj_tensionbar`)**:
  - **O que foi feito**: adicionado `vitaQuadBatchFlush()` no início de `glGpuSetBlendMode` e `glGpuSetBlendModeExt` (`src/butterscotch/src/gl_legacy/gl_legacy_renderer.c`).
  - **Por quê**: a troca de modo de blend acontecia antes de descarregar os quads acumulados no lote anterior, fazendo o segundo sprite da barra de TP ser desenhado com o blend errado por cima do texto `TP %`.
  - **Pra quê**: preservar a máscara/recorte da barra de TP e eliminar a segunda borda sobreposta na batalha.
- **Bordas animadas no Chapter 5 (`vita_animated_borders.inc`)**:
  - **O que foi feito**: `animatedBordersGetTexture` agora seleciona `frames_side.bani` (`wantSide = 1`, apenas 3 texturas RGB565 de `255 KiB` para os pilares laterais de `120px`) quando `borderMemoryPressure` está ativo, em vez de desligar a animação.
  - **Por quê**: no Chapter 5 a pressão de memória (`> 44 MiB`) fica ativa durante quase todo o jogo, o que desabilitava totalmente as bordas animadas.
  - **Pra quê**: manter as bordas animadas funcionando em todo o Chapter 5 com consumo mínimo de VRAM (`< 768 KiB`).
- **Filtros de tela CRT (`CRT Suave`, `CRT Médio` e `CRT Agressivo`)**:
  - **O que foi feito**: renomeado `"CRT Mod (Experimental)"` para **`"CRT Suave"`** (`u_strength = 1.0f`), criado **`"CRT Médio"`** (`u_strength = 1.15f`, `+15%` de intensidade), criado **`"CRT Agressivo"`** (renomeado de `"CRT Avançado"` na `v0.81-6`, somando `u_strength = 1.15f` + scanlines de TV + vinheta + cantos curvos de tubo) e removidos `"CRT Tube"` e `"Scanlines"` avulsos da lista.
  - **Por quê**: organizar os filtros CRT em três níveis progressivos conforme pedido, sem manter opções antigas duplicadas.
  - **Pra quê**: entregar três perfis CRT graduados de passe único sem readback de framebuffer.

## v0.81-2 e v0.81-3 — atalhos nativos em C para o Chapter 5

- **O que foi feito**: implementados os 4 candidatos em C (`src/butterscotch/src/runner.c` e `src/butterscotch/src/vm_builtins.c`):
  1. `VMBuiltins_tryGrassStripDraw` (`obj_plat_vertical_floor_grass_strip`): reutiliza a surface estática da faixa de grama (`__vita_grass_ready`) sem recriar os tiles a cada quadro.
  2. `Runner_tryNativeCliffLayersDraw` (`obj_parallax_cliffs_moving_layers`): desenha em C as camadas móveis do penhasco no perfil Simple.
  3. `Runner_prepareFusumaCull` / `Runner_restoreFusumaCull` (`obj_plat_fusuma_mask`): oculta temporariamente durante o Draw as portas deslizantes fora da câmera, restaurando `is_visible` logo em seguida.
  4. `VMBuiltins_tryBluePlatformDraw` (`obj_blue_platform`): executa o `Draw_0` das plataformas azuis em C mantendo `Step`/colisão em GML.
- **Por quê**: reduzir o custo de GML interpretado nos objetos mais pesados identificados no perfilamento do Chapter 5 (`docs/candidatos-c-caps4-5-v0.80-59.md`).
- **Pra quê**: ganhar margem de CPU por frame nas salas de penhasco, vento e castelo do Chapter 5 sem alterar a lógica nem a colisão do jogo.

## v0.81-1 — abertura da versão v0.81 e pacote de bordas animadas

- **O que foi feito**: atualização da versão-base para `0.81` (`config/version.txt`) e integração do pacote de bordas animadas `.bani` (`src/vita-probe/source/vita_animated_borders.inc`).
- **Por quê**: iniciar a linha `v0.81` a partir da base validada `v0.80-59`.
- **Pra quê**: suportar bordas animadas em streaming RGB565 e versionamento automático `v0.81-<N>` no gerador de VPK.

## v0.79-5 — torre do Flowey e cache de texturas (teste)

- GXM: `draw_surface`/`draw_surface_ext` enviam largura e altura -1 para "a
  surface inteira". O GXM descartava esses desenhos, entre eles a cópia final
  da torre do Flowey (`NATIVE_GXM=tower_draw ... part=0,0,-1x-1`), que por
  isso nunca aparecia.
- GXM: o orçamento do cache de texturas pode crescer até 128 MiB enquanto
  sobrarem mais de 48 MiB de RAM livre. Na batalha final, o limite de 56 MiB
  do VitaGL causava 141 evicções e 17 s de recargas, sentidas nas trocas de
  cenário.
- A v0.79-4 confirmou a correção das cenas por quadro: `pre_us` caiu de
  ~33 ms para ~2,6 ms.

## v0.79-4 — espera da GPU entre cenas (teste)

- GXM: a tela era criada com 1 cena por quadro, mas é iniciada várias vezes
  em cada quadro (limpeza, composição depois do mundo e depois de cada troca
  de surface). Cada reinício fazia a CPU esperar a GPU terminar a cena
  anterior; no log isso aparecia como 25–33 ms em `draw_detail pre_us`
  (`room_dw_ranking_hub`, salas do Chapter 5). A tela passa a 8 cenas por
  quadro, a application surface também a 8 e as demais surfaces a 4.

## v0.79-3 — Chapter 0 durante o vídeo (teste)

- GXM: o `data.win` do Chapter 0 é lido numa thread enquanto o vídeo de
  abertura toca, e o Chapter 0 não mostra tela de loading. Só a troca para os
  Chapters 1–5 exibe o loading.
- GXM: a pré-carga das páginas de batalha fica limitada a 12 páginas e não
  roda no Chapter 5. No log, ela carregava 33 páginas em 3,3 s e enchia o
  orçamento de textura antes da sala do save.
- GXM: registros `NATIVE_GXM=tower_draw` e dos uniforms de `shd_tower` e
  `shd_depthmap` para investigar a torre do Flowey, que continua invisível.

## v0.79-2 — torre do Flowey e loading no GXM (teste)

- GXM: shaders nativos `shd_depthmap` (fatias do tronco) e `shd_tower`
  (mapeamento cilíndrico, cor da borda e face traseira) da torre do Flowey no
  Chapter 5. Sem eles só as folhas e flores apareciam.
- GXM: tela de loading (animação do Toby e rótulo "Carregando") enquanto o
  `data.win` do capítulo é lido numa thread.
- GXM: o Filtro de tela pode ser escolhido no menu Gráficos (a linha estava
  bloqueada como somente leitura).
- GXM: pré-carga das páginas comuns de batalha (grupo/UI) antes do jogo, como
  `GLLegacyRenderer_preloadChapterBattleCore`, contra o travamento ao iniciar
  a batalha.
- Áudio: registro de pausa, retomada e liberação de músicas
  (`AUDIO_STREAM=music_*`) para diagnosticar a música que não volta após
  batalhas no Chapter 1.

## v0.79-1 — unificação VitaGL + Native GXM (pre-release)

- Une a linha pública VitaGL com o trabalho Native GXM das builds v0.78-12 a
  v0.78-16 (detalhes abaixo). Notas em inglês: `docs/releases/v0.79.md`.
- GXM: o `texture-cache` `.r444` é gerado uma vez por capítulo/idioma, com a
  tela de geração (rótulos e barra de progresso do VitaGL), quando ainda não
  existe; `complete.vtc` marca o cache como completo.
- GXM: filtro de tela selecionável em Gráficos, máscaras de colisão com o
  inspetor de objetos (toque, L + direcional, log em `devlogs/chapter N/objetos`),
  captura do Debug DEV em `devlogs/chapter N/<seq>_<sala>_<data>.log`,
  analógico esquerdo para andar e saída de "Desistir" no Game Over, como no VitaGL.
- GXM: a `application_surface` fica opaca antes de ser exibida (o GameMaker
  ignora o alpha dela). Corrige a parede preta do green room do Chapter 3.
- GXM: o rótulo do Chapter 0 usa a versão-base da build.
- GXM: regras por sala do runner (Espanhol, torre do Flowey, reflexos, LUT)
  passam a ser compiladas também no GXM; shader nativo `shd_grayscalesand`
  (câmera do tabuleiro do Chapter 3); botões de PlayStation; regras de borda
  do VitaGL; `global.opti_mode` semeado no Chapter 5; troféus nativos (NP).
- Bandeira francesa do seletor de idiomas convertida de PNG de paleta 4 bits
  para RGB.

## v0.78-15 — Native GXM: minigame do Chapter 3, green room e Chapter 5 (teste)

- O crash C2-12828-1 ao abrir o minigame de videogame do Chapter 3 era um
  `abort()` proposital em `draw_vertex_colour` (`primitive_vertex_color`). As
  primitivas do GXM agora aceitam qualquer quantidade de vértices, cor por
  vértice, textura (mapeamento afim exato por triângulo) e
  listas/strips/fans de triângulos, linhas e pontos. `vertex_submit` usa o
  mesmo caminho.
- Estados que o GML alcança e o GXM ainda não reproduz (matriz de
  view/projeção, desenhar uma surface nela mesma, cópia de/para o alvo ativo,
  fator de blend desconhecido, GUI aninhada) passam a ser registrados uma vez
  (`NATIVE_GXM=runner_renderer skipped=`) e ignorados, em vez de abortar.
- Fundo preto no green room: a `application_surface` agora é composta sem
  que o alpha escureça a imagem, como no blit do GameMaker/VitaGL. A sala zera
  o alpha da parede para usar como máscara.
- Chapter 5: surfaces de até 4096 px por lado (antes 2048 px, o limite dos
  atlas). Uma surface de 2080x120 era recusada a cada quadro. O reflexo do
  castelo é atualizado a 10 Hz também em `room_dw_fcastle_final_save`, a sala
  que ficava em 4 FPS após carregar o save.
- Filtros de tela (scanlines, dithering, VHS e Sharp Bilinear) e a ligação dos
  troféus do jogo com o arquivo local e a notificação interna agora funcionam
  no GXM. Os troféus do sistema (NP) continuam só no VitaGL.
- A entrada de sala também é detectada quando a troca acontece dentro do Step.
  No Chapter 5 o log registra `opti_mode` e a presença da torre (`ch5_room_state`).

## v0.78-14 — Native GXM: GPU crashes em trocas de sala e correções visuais (teste)

- A v0.78-13 ainda tinha GPU crash ao entrar no green room (Cap. 3) e no
  Cybercampo (Cap. 2), e também depois de uma troca de cenário na batalha
  final do Cap. 5. O Cap. 2 travou sem nenhuma variante de blend nova, então o
  blend não é a causa comum.
- Causa 1: o NanoVG-GXM só libera uma imagem apagada depois de quatro
  `renderFlush`, mas cada troca de surface faz um flush. Uma borda ou atlas
  trocado na entrada da sala era liberado enquanto cenas de um ou dois quadros
  antes ainda o amostravam. A coleta agora roda uma vez por quadro
  apresentado (`nvgxmCollectGarbage`). As evicções que já esperam a GPU
  liberam a memória na hora.
- Causa 2: o heap de 256 MiB da v0.78-12 tirava 128 MiB das texturas e
  surfaces do GXM, que ficam fora do heap. Restavam 9 MiB livres na batalha
  final do Cap. 5, onde a torre não aparecia. O heap volta aos 128 MiB padrão
  do SDK; o parser lê os chunks em streaming quando falta memória. A pilha de
  4 MiB continua.
- Fonte rosa na transição do Chapter 0: a cópia de surface por GPU gravava
  4444 em ordem ABGR numa imagem 4444 RGBA. Os snapshots agora são RGBA8888,
  no mesmo layout do render target.
- Texto: o espaçamento de `draw_text_ext` substitui a altura da linha, em vez
  de se somar a ela, e `\r\n` conta como uma quebra só. A escala e o
  `ascenderOffset` da fonte seguem o renderer GL. Isso corrige a linha muito
  abaixo na primeira tela do Chapter 0 PT-BR.
- O vídeo de abertura usa cinco buffers de saída, a configuração já validada no
  VitaGL. Com dois, o decoder reescrevia o quadro que a GPU ainda lia.
- Falhas ao criar surfaces passam a ser registradas
  (`NATIVE_GXM=surface_create_failed`).

## v0.78-13 — Native GXM: green room do Chapter 3 (teste)

- A v0.78-12 teve GPU crash ao carregar um save em `room_dw_green_room`. O
  dump mostra a CPU esperando a GPU durante o Draw de
  `obj_dw_green_room_bg_fx`. Esse Draw grava só o alpha com o blend desligado
  e depois usa `bm_dest_alpha`/`bm_inv_dest_alpha`, um estado que o GXM nunca
  tinha executado no Chapter 3.
- Quando a máscara de escrita é só o alpha, só RGB ou nenhum canal, o GXM
  agora usa a máscara completa com fatores ZERO/ONE nos canais protegidos.
  O resultado na tela é idêntico e não depende mais de um programa de
  fragmento mascarado.
- Cada variante de blend criada é registrada (`NATIVE_GXM=blend_variant`),
  com os fatores e o resultado do shader patcher.
- `gxm-r444-cache.txt` com `0` desliga o cache r444 sem precisar recompilar,
  para teste A/B no Vita.

## v0.78-12 — Native GXM: cache r444, memória e travamentos (teste)

- O GXM passa a usar a mesma pilha da thread principal (4 MiB) e o mesmo heap
  (256 MiB) do VitaGL. A VM executa cada chamada GML de forma recursiva, e o
  GXM rodava com o padrão do SDK. Hipótese principal do congelamento na entrada
  da batalha do Tenna (Cap. 3) na v0.78-11; ainda falta confirmar no Vita.
- Atlas carregados do `texture-cache/page_NNN.r444` preparado pelo VitaGL,
  incluindo o cache das traduções. O PNG/QOI do `data.win` fica só como
  reserva, e o blob comprimido é liberado da RAM após o upload, como nos
  renderers GL. Novos campos `r444_cache_*` e `page_load_ms` em
  `render_metrics`.
- Watchdog em outra thread: se nenhum quadro terminar em 3, 10 ou 30 s, grava
  `NATIVE_GXM=watchdog` com a etapa do loop, a operação de GPU ou textura em
  curso, a sala, o script GML e a profundidade de chamadas.
- Telemetria `NATIVE_GXM=memory` (RAM, CDRAM, phycont, heap e blobs de
  textura), `room_enter`/`room_first_frame` em cada troca de sala e
  `draw_detail` com a divisão do tempo de desenho.
- Pré-carga limitada dos sprites de grupo/UI ao entrar em
  `room_dw_snow_zone_battle`, igual ao Legacy-GL.
- Cabeçalho de sessão do GXM no `butterscotch-probe.log` e após a rotação do
  `native-gxm-probe.log`, para não misturar linhas de builds diferentes.

## v0.78-3 — abertura compatível com SceAvPlayer

- O novo vídeo havia sido exportado em HEVC/H.265 1920×1080 com uma terceira
  faixa de timecode. O SceAvPlayer abria o MP4, mas não entregava nenhum frame.
- `NewDeltaruneLoadIntro.mp4` foi convertido para H.264/AVC Constrained
  Baseline nível 3.1, 960×544, 30 FPS e `yuv420p`, com AAC estéreo a 48 kHz e
  sem a faixa de timecode, seguindo o contrato do vídeo anterior validado.

## v0.78-2 — nova abertura e LiveArea limpa

- VitaGrafix/VGi deixa de bloquear o SceAvPlayer; a proteção de fallback fica
  restrita aos plugins USB UAC_UVC/UDCD_UVC.
- `NewDeltaruneLoadIntro.mp4` passa a ser o vídeo de abertura interno do VPK.
  A reprodução aguarda o EOF, com limite defensivo de 30 segundos, antes da
  retenção do último frame e do fade-out.
- O `sce_sys/pic0.png` foi removido para não sobrepor uma imagem ao iniciar o
  aplicativo pela LiveArea.

## v0.78-1 — inicialização segura com VitaGrafix

- O dump da abertura da v0.77-15 foi identificado como prefetch abort da thread
  `avPlayer Controller`, e não como GPU crash ou falha do experimento GXM.
- Quando VitaGrafix/VGi está ativo, o primeiro MP4 agora é substituído pela
  sequência PNG antes da criação do `SceAvPlayer`. A mesma proteção continua
  válida para UAC_UVC/UDCD_UVC.
- Um aviso explica o fallback; renderizador, shaders e gameplay permanecem
  disponíveis normalmente.

## v0.78-gxm1 — laboratório Native GXM

- Criado um VPK independente que inicializa e usa `sceGxm` diretamente, sem
  VitaGL e sem compilador de shaders em runtime.
- A prova exercita apresentação tripla, primitivas 2D, transparência, stencil
  e lotes animados, registrando o resultado em `native-gxm-probe.log`.
- A versão-base do projeto foi atualizada para v0.78. O jogo completo ainda usa
  os renderizadores anteriores; o Native GXM permanece uma opção de pesquisa
  até cobrir o contrato do Butterscotch.
- Arquitetura, limites e próximas etapas documentados em
  [`native-gxm-v078.md`](native-gxm-v078.md).

## v0.77-17 — estabilidade da Mansão do Chapter 2

- O Experimental-GL reserva memória para as surfaces e sprites dinâmicos
  criados durante as trocas de sala da Mansão.
- O cache de texturas da Mansão passa a ter teto próprio de 92 MiB e é reduzido
  a 88 MiB antes de portas internas. Isso evita o esgotamento observado em
  `room_dw_mansion_east_2f_a`, que criava uma surface de 1920×480 quando
  restavam apenas 6,5 MiB livres.
- Cyber World e Castle Town do Chapter 2 preservam seus limites anteriores.

## v0.77-16 — Loading antecipado e Flowey

- A tela de Loading dos capítulos aparece logo após a inicialização do VitaGL,
  antes da configuração de troféus e da leitura do `data.win`.
- A barra acompanha os 31 blocos processados durante a leitura do capítulo, em
  vez de deixar uma tela preta até o Runner estar pronto.
- O diálogo nativo de configuração de troféus mantém o Loading como fundo.
- A batalha final do Chapter 5 evita descartar novamente, por um segundo, um
  atlas que acabou de pagar o custo de recarga. Isso reduz o ciclo observado de
  454 descartes, incluindo 48 descartes da mesma página.

## v0.77-15 — Flowey e Room Navigator

- Reduz a troca repetida de atlases de 1024 px na batalha final do Chapter 5,
  mantendo uma reserva segura para páginas grandes e surfaces da torre.
- Reutiliza a surface de reflexos da Flower Castle a 10 Hz também no modo
  Automático/Experimental-GL; antes a otimização só era aplicada ao Modern-GL.
- O Room Navigator agora finaliza o trabalho pendente da GPU e coleta texturas
  aposentadas antes de saltar para uma sala não relacionada.
- Remove sincronizações duplicadas no descarte urgente de texturas do Vita.

## v0.77-14 — glifos completos nos diálogos de batalha

- Legacy/Experimental e Modern agora amostram os retângulos das fontes pelo centro do primeiro e do último texel. Isso impede o VitaGL de perder a linha inferior dos caracteres pequenos ou buscar a linha vizinha do atlas.
- A correção vale para `draw_text`, `draw_text_color`, fontes normais e fontes criadas por sprites em todos os capítulos. Foi removido o reparo anterior restrito a texto preto com `fnt_main`, que desenhava uma faixa extra fora do glifo.


## v0.77-13 — Castle Town GPU e chegada

- `room_dw_castle_area_1` passa ao preto imediatamente e prepara os atlas do grupo durante a transição; o primeiro quadro registrado gastava 318 ms carregando texturas dentro de `obj_mainchara`.
- Os interiores de Castle Town usam o desenho texturizado estável no lugar do pior caso de `shd_pal_swapper`, que podia executar 256 leituras dependentes por pixel. O dump terminou nesse passe em `room_dw_castle_cafe`; os demais capítulos preservam o shader.

## v0.77-11 — vértices dos shaders experimentais (teste)

Corrigido o bridge 2D do Vita para alimentar os atributos dos programas
Experimental-GL. O shader de sombra da cidade agora recebe posição, UV e cor;
Legacy sem shaders preserva seu caminho anterior. Regressão com o shader e o
`data.win` reais falhava antes e passou depois.
[Diagnóstico e validação](ch5-town-shadows-v07711.md).

## v0.77-10 — sincronização do ritmo do Tenna (teste)

Corrigido o relógio de áudio em loop e o seek fora da faixa, que divergiam
do áudio nas rodadas posteriores. Preparação limitada da interface/personagens
ao entrar na batalha do Tenna. [Evidências e testes](ch3-rhythm-clock-v07710.md).

## v0.77-9 — bordas e composição do finale (teste)

Bordas ajustadas para Snow Zone/estalo do Tenna, room_ed e fonte do Chapter 4.
Corrigidos os fatores de blend retornados/restaurados em Legacy e Modern e
o descarte de sprites sob projeções modificadas no Vita. Preparação antecipada
limitada de áudio/sprites para reduzir pausas de entrada em batalhas.
[Evidências, validação e limites](ch3-finale-v0779.md).

## v0.77-8 — retomada após derrota (teste)

Espera e fade de Continuar encurtados no fluxo comum dos Chapters 2–5,
preservando o checkpoint original. O escape da tela preta agora exige o estado
Desistir, evitando reiniciar o capítulo durante o fade de Continuar. Saves
atuais têm prioridade sobre cópias antigas em mods. Testes locais passaram nos
dois renderizadores; retomada da batalha no Vita ainda precisa de confirmação.
[Detalhes e limites](gameover-retry-v0778.md).

## v0.77-7 — TVs em PT-BR, quiz e profiling (teste)

Corrigido o bloqueio dos getters rápidos quando o profiler estava alocado
mas sem amostrar. Reconhecidas as assinaturas verificadas do Draw PT-BR e
ampliado o culling para os painéis normais/tesouro de maze_quiz. Testes com
coleta habilitada mantiveram pixels idênticos em Legacy e Modern nas duas
salas. Novos contadores no log principal permitem confirmar a ativação no
Vita. [Diagnóstico, medições e limites](ch3-tv-v0777.md).

## v0.77-6 — desempenho dos painéis de TV do Chapter 3 (teste)

Reduzido o custo de getters simples na VM e evitado o Draw completo de
painéis inteiramente fora da câmera em `room_dw_teevie_stealth_d`, preservando
timers, Step e animações. Inicialização do shader ativo corrigida no Legacy.
Replay local com câmera em movimento manteve pixels idênticos em Legacy e
Modern e reduziu o tempo mediano do cenário. Validação de FPS no Vita pendente.
[Evidência, limites e testes](ch3-stealth-v0776.md).

## v0.77-5 — monitores do Chapter 3 e paleta do quiz (teste)

Atualizações dos monitores distribuídas entre frames, com reuso da cópia e dos
passes intermediários e descarte de regiões cinzas fora do alvo. Corrigidos o
limite incompleto da paleta e a atualização da projeção de shaders ao trocar
superfícies em Legacy/Experimental. Contagens locais caíram de 12 para 6 cópias
e de 24 para 12 trocas de alvo em dois frames. FPS e cenário completo no Vita
aguardam confirmação. [Detalhes e testes](ch3-monitor-quiz-v0775.md).

## v0.75-8 — retorno ao caminho de vídeo/áudio da v0.75-5 (teste)

Após falhas repetidas de reserva GXM no vídeo da v0.75-7, a arena de uniforms
customizados voltou ao caminho da v0.75-5 com proteção de vida útil da GPU.
O áudio do vídeo passa a registrar seu estado real de reprodução, fila e ganho
em intervalos limitados. Resultado no Vita pendente.
[Detalhes](ch5-v0758-video-rebase.md).

## v0.75-7 — vida útil de recursos da GPU (teste)

Coleta de recursos encerra a cena antes da espera/liberação. Uniforms customizados
usam reservas nativas do GXM, eliminando a sobrescrita pelo pool circular próprio.
Falhas de reserva impedem desenhos inválidos inclusive no release. Correções
compartilhadas por todas as salas; shaders continuam habilitados. Testes locais
e VPK concluídos, confirmação no aparelho pendente.
[Detalhes](modern-gl-v075-7-gpu-lifetime.md).

## v0.74-17 — vídeo e iluminação do Chapter 5 (teste)

Corrigido o estado GL compartilhado pelo MP4, estendido o tratamento do marcador
de recuperação ao Chapter 5, implementados frames animados de tilemaps e adicionada
execução nativa equivalente da iluminação do jardim, restrita ao bytecode validado.
Testes locais de pixels/estado, equivalência de desenho, Town/PNG e fotografia
passaram; validação no Vita pendente. TP duplicado e pétalas não são considerados
resolvidos. [Detalhes](ch5-v074-17-fixes.md).

## v0.74 — retomada da base v0.74-5

Restaurado o runtime e recuperado o VPK original; descartada a abordagem de reiniciar apos a introducao. Correcoes retomadas com boot preservado e videos de teste recodificados em pacote separado. Confirmacao no Vita pendente. [Detalhes](modern-gl-v074-rebase-v5.md).

## v0.74-10 — contenção do GPU crash após a introdução

Modern-GL passa a abrir a seleção de capítulos em uma nova execução após o MP4, com marcador para impedir repetição da introdução. Teste no Vita pendente; a causa interna do GPU crash ainda não está confirmada. [Análise](modern-gl-v074-9-intro.md).

Mudanças importantes por versão. Itens de trabalho pendente ficam em [backlog.md](backlog.md); ao
concluir um item do backlog, registre-o aqui com a versão/data e remova-o de lá.

> Este arquivo é a fonte de verdade para "o que mudou". O `README.md` mantém uma tabela resumida
> para o usuário final — ao adicionar uma entrada aqui, considere se vale espelhar no README também.

## v0.74 — Modern-GL / Chapter 5 (2026-09-14)

- v0.74-4: diagnóstico persistente de renderização lenta, com etapas, evento Draw mais caro e contadores Modern-GL corrigidos. Basquete tem picos de carregamento; Flowery apresenta custo sustentado no bloco de renderização. Causa final e ganho de FPS ainda não confirmados; [análise](modern-gl-v074-4-performance.md).
- Corrigido `layer_destroy`: camadas do editor e suas instâncias agora são removidas, respeitando o estado do festival e retirando colisões que bloqueavam caminhos. 24 testes GL2/GL3 cobrem os quatro estados em `room_town_mid`, `room_town_south` e `room_town_north`.
- Cache usa alvo de retenção em vez de recusar texturas do quadro atual por um limite fixo; mantém salas vizinhas em memória quando há espaço.
- Reservas internas de uniforms são verificadas; build habilita compartilhamento de destinos VitaGL para reduzir recursos do driver em salas com muitas superfícies.
- Rota de teste ampliada para Castle Town e Flowery. Sombras aparecem no desktop; confirmação de sombras, desempenho e crash no Vita continua pendente. Detalhes em [modern-gl-v074-chapter5.md](modern-gl-v074-chapter5.md).

## v0.73-25 — Modern-GL (2026-09-14)

- Corrigida marca de uso GPU de superfícies no VitaGL e restauração do destino após criar sprite a partir de superfície.
- Fontes ganham prioridade no cache; Chapter 5 usa 24 MiB, consulta os três pools mapeados e evita tentativas/logs repetidos por letra e coleta forçada por RAM baixa em todo quadro.
- 22 casos desktop e teste C da política de cache aprovados. Combate completo, ganho de FPS e crash ainda exigem confirmação no Vita. Detalhes em [modern-gl-v073-25-fixes.md](modern-gl-v073-25-fixes.md).

## v0.73-24 — Modern-GL (2026-09-13)

- Corrigido encerramento da cena antes da coleta forçada do VitaGL e ajustada a expulsão de texturas sob pressão de memória após crash reportado no Chapter 5.
- LUTs preservadas em RGBA8888; corrigidos Z dos vértices 2D, estado de shader dos PNGs externos e apresentação das bordas no caminho GL2.
- Build Vita Release e 18 casos desktop aprovados. Confirmação no hardware pendente; detalhes em [modern-gl-v073-24-fixes.md](modern-gl-v073-24-fixes.md).

## v0.71

### Revisão local Modern-GL (2026-09-13, ainda sem validação no Vita)

- Corrigidos crash de reutilização de textura no desktop, consultas de uniforms antes da compilação adiada, samplers secundários/LUT e estado de atributos após vertex buffers. Revisados os PRs upstream #359, #330 e #460.
- Validação e limites em [modern-gl-review-2026-09-13.md](modern-gl-review-2026-09-13.md); suíte reproduzível no `local-test`.

- **Correções do Capítulo 2 (Batalhas, Backgrounds e Shaders)**:
  - **Correção de Crash ao iniciar o Capítulo 2 em Inglês**:
    - *Causa*: Na compilação dos shaders auditados (`shd_hue` e `shd_pal_swapper`), `glCompileAllShaders()` linkava os programas na GPU mas não chamava `glUseProgram(0)` ao final do loop. O último shader linkado (`shd_pal_swapper`) permanecia ativo no contexto do VitaGL, e as chamadas iniciais de desenho 2D na tela de loading passavam por `_glDrawArrays_CustomShadersIMPL` com uniforms não inicializados, disparando `Data abort exception` em `SceGxm` (`vglSetUniformData`).
    - *Solução*: Inserido `glUseProgram(0);` explicitamente ao final de `glCompileAllShaders()` em [gl_legacy_renderer.c](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/gl_legacy/gl_legacy_renderer.c).
  - **Troca de Cor dos Inimigos Swatchling (Corviçais)**:
    - *Causa*: `image_blend` realiza multiplicação de cores (`GL_MODULATE`). Como a roupa base do Swatchling é vermelha pura `(237, 28, 36)`, multiplicar por azul ou verde resultava em tons escuros/marrom, sem mudar a cor real do paletó e penas.
    - *Solução*: Injetados subframes coloridos nativos com a paleta oficial do jogo (Azul=1, Verde=2, Amarelo=3, Laranja=4, Vermelho=0) diretamente em `spr_swatchling_body`, `spr_swatchling_head`, `spr_swatchling_legs` e `spr_swatchling_hurt` nos `data.win` do Capítulo 2, e atualizado `obj_swatchling_enemy_Draw_0` para desenhar os subframes nativos a 60 FPS sem necessidade de shaders.
  - **Balões de Fala dos Inimigos (Speech Bubbles)**:
    - *Causa*: `obj_battleblcon_Draw_0` calculava a largura dinâmica do balão a partir de `stringmax * hspace + 10`. Para frases traduzidas em português (ex.: `"Parece com dor."`), a margem de 10px encostava ou cortava na borda direita do balão.
    - *Solução*: Atualizado o patch semântico `PatchChapter2BattleSpeechBubble` em [apply_vita_gml_optimizations.csx](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/data/deltarune_builder/rebuild_data_win_v1_6_bundle/source/apply_vita_gml_optimizations.csx), aumentando a margem para `+ 28px` e garantindo espaço visual perfeito para qualquer idioma.
  - **Música das Salas (BGM Retido na Transição)**:
    - *Causa*: Durante transições rápidas entre salas da mansão, streams recém-criados podiam entrar em `transitionHold` enquanto uma transição anterior ainda estava ativa. A propriedade não era liberada, mantendo músicas como `mansion.ogg` em `queued_transition_hold` indefinidamente até uma nova troca de sala.
    - *Solução*: O ciclo de espera e liberação (`AlAudioSystem_setTransitionHold(audio, false)`) agora é executado de forma determinística a cada frame no final de `Runner_step()` em [runner.c](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/runner.c), garantindo que todo áudio de sala comece a tocar pontualmente.
  - **Prédios 3D da Gigaqueen (`obj_3d_bg_effect`) e Chão da Batalha da Mansão (`room_dw_mansion_east_4f_d`)**:
    - *Causa*: No GameMaker 2.3+, backgrounds como `bg_cityscape` (2206) e pisos da mansão são armazenados internamente no chunk de sprites (`SPRT`), mas invocados via `draw_background_part_ext` e `draw_background_tiled_ext`. A rotina `Renderer_resolveBackgroundTPAGIndex` só consultava o chunk `BGND` (que possui apenas 97 entradas no Cap 2), retornando `-1` e ignorando a renderização.
    - *Solução*: Adicionado fallback automático para `SPRT` em `Renderer_resolveBackgroundTPAGIndex` em [renderer.h](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/renderer.h), restaurando o piso da Queen/Berdley e a perspectiva 3D da Gigaqueen.
  - **Pré-aquecimento de Batalha (Redução de Lag do 1º Combate)**:
    - Expandida a lista `battleCore` em [gl_legacy_renderer.c](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/gl_legacy/gl_legacy_renderer.c) para carregar antecipadamente todas as texturas de UI/combate do Capítulo 2 (`spr_tensionbar`, botões `spr_btact`/`fight`/`item`, balões, etc.) durante a tela de loading.

- **Correção de Crash ao iniciar o Capítulo 3 (Shader de Bootstrap do Video Player)**:
  - **Causa Raiz**: No bootstrap de reprodução de vídeo do Capítulo 3 (`VitaVideo_init()` em [vita_video.c](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/vita-probe/source/vita_video.c)), os shaders de apresentação eram carregados via `glShaderBinary()` usando os headers binários legados `movie_v.h` / `movie_f.h` (código GXP bruto pré-compilado). Na versão atual do VitaGL, `glShaderBinary()` roteia para `unserialize_shader()`, esperando o formato binário interno do cache do VitaGL. Ao passar o GXP bruto, os 4 primeiros bytes ("GXP\0") eram lidos como contagem de matrizes (`0x00505847`), causando estouro de ponteiro e crash imediato (`VIDEO=frame_handles_ready`).
  - **Solução**:
    - Substituída a chamada `glShaderBinary()` por compilação em tempo de execução via `glShaderSource()` + `glCompileShader()` com fontes GLSL ES `#version 100` e validação com `glGetShaderiv(..., GL_COMPILE_STATUS)` e `glGetProgramiv(..., GL_LINK_STATUS)`.
    - Removidos os includes legados de `movie_v.h` e `movie_f.h`.

- **Pipeline de Reconstrução de Mods de Idioma e Texturas (v1.6 / v0.3.6)**:
  - Corrigido travamento em segundo plano do `UndertaleModCli` no pipeline automatizado de compilação de idiomas (`build_language_mod.py`), configurando `stdin=DEVNULL`.
  - Reconstrução completa dos atlas com padding seguro e geração dos conjuntos de texturas em **RGBA4444 (`.r444`)** e **BC3 (`.bc3.pvr`)** para todos os capítulos de novos mods (incluindo Francês, Alemão, Espanhol e PT-BR).
  - Prevenção definitiva de estouro de memória (`stb_error.log: STB failed outofmem`) ao eliminar a necessidade de decodificação em runtime de PNGs 2048x2048 por software.

## v0.68

- **Correção: texturas erradas no mod PT-BR dos Capítulos 2 e 3 (páginas de atlas faltando na
  lista de variantes)**: `vitaVariantPageDiffers()` em
  [gl_legacy_renderer.c](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/gl_legacy/gl_legacy_renderer.c#L56-L78)
  assumia que só as páginas 22/25/26 (Cap. 2) e 17/19-22/24/26-29 (Cap. 3) diferem entre o
  `data.win` base e o PT-BR, servindo o `.pvr` da base pra qualquer página fora dessa lista. Um
  diff pixel a pixel (não por hash, que dá falso positivo por recompressão de PNG) contra os dois
  atlas exportados mostrou diferenças reais e não cobertas: Capítulo 2 páginas 16-24 (até 12% dos
  pixels) e Capítulo 3 páginas 13-25 (até 70% dos pixels, ex.: página 23) — resultado visível como
  "textura errada" relatado em `room_dw_mansion_darkbulb_1`. Além disso, o atlas PT-BR tem páginas
  extras só nele (repacking de texto mais longo): Cap. 2 páginas 27-30, Cap. 3 páginas 33-36 — sem
  contraparte na base, então nunca podem cair no fallback pra `chapter2/`/`chapter3/`.
  - `vitaVariantPageDiffers()` simplificado para `pageId >= 16` (Cap. 2) e
    `13 <= pageId <= 29 || pageId >= 33` (Cap. 3).
  - `$variantPages` em [build-2048-pvr-cache.ps1](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/scripts/build-2048-pvr-cache.ps1#L110-L145)
    expandido de 14 para 37 entradas; `.pvr` PT-BR gerados para as páginas antes faltantes.
  - Capítulos 1 e 4 não precisaram de mudança: já usam repack completo do atlas (toda página é
    tratada como diferente), não a lista curada — só Cap. 2/3 tinham essa lacuna.

- **Perfil de textura de cenário trocado de PVRTC1 4bpp para PVRTC2 4bpp**: as 28 páginas de
  Rooms super mapeadas (cenário puro, sem personagens/UI) que usavam PVRTC1 4bpp sRGB+BEST agora
  usam PVRTC2 4bpp sRGB+BEST — mesmo custo de VRAM (2 MiB/página), mas erro médio RGB ~7x menor
  (validado em `data/texture-test/hybrid-chapter5-page056/README.txt` e
  `data/texture-test/New Test/README.txt`: 11.745 → 1.714, próximo do perfil lossless RGBA4444
  usado nas páginas mistas).
  - Páginas afetadas (`$pvrtcPages` em [build-2048-pvr-cache.ps1](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/scripts/build-2048-pvr-cache.ps1#L30-L37)):
    Capítulo 2 (10, 11, 12), Capítulo 3 (17, 18, 19, 24-29), Capítulo 5 (32, 36, 38, 39, 42, 46,
    49-52, 54, 55, 58, 59, 68, 72).
  - Todas as demais páginas continuam RGBA4444/RGBA5551 (gerado pelo texture-cache), sem mudança.
  - `SCE_GXM_TEXTURE_FORMAT_PVRTII4BPP_ABGR` é um formato nativo do GXM (decodificado em hardware
    pela SGX543MP4+), assim como o PVRTC1 que já estava em produção — diferente de ASTC, que não
    tem suporte de hardware no Vita (ver `data/texture-test/hybrid-chapter5-page056/README.txt`).
  - Mudanças de código: novo pixel format ID `5` reconhecido em
    [gl_legacy_renderer.c](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/gl_legacy/gl_legacy_renderer.c#L15-L21)
    (`VITA_PVR_PVRTC2_RGBA_4BPP_FORMAT`, upload via `GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG`);
    encoder do pipeline trocado para `-f PVRTCII_4BPP,UBN,sRGB` em `build-2048-pvr-cache.ps1`;
    `validate-pvr-assets.ps1` atualizado para aceitar o novo format ID.

- **Correção de Crash ao carregar save no Capítulo 5 (`room_town_south`)**:
  - **Causa Raiz**: Transições não-compartilhadas (como transição de menu `PLACE_MENU` para mapa de jogo `room_town_south`) usavam meta de trim de textura residual `vitaTrimMiB = 44 MiB`. Como `PLACE_MENU` ocupava ~41.9 MiB, a verificação `residentTextureBytes <= targetBytes` retornava `true` sem desalocar nenhuma das páginas de textura do menu. Ao carregar `room_town_south`, as texturas da sala eram carregadas por cima dos 41.9 MiB obsoletos do menu, excedendo o teto de 80 MiB do Capítulo 5 e causando esgotamento de memória no Vita (crash kernel data abort/out-of-memory).
  - **Solução**:
    - Aumentada a reserva de RAM do VitaGL (`ram_reserve`) no Capítulo 5 de `24 MB` para `36 MB` em [playable_main.c](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/vita-probe/source/playable_main.c#L612), aproveitando a memória física livre do sistema para a máquina virtual do GameMaker.
    - Ajustado o teto de cache de textura do Capítulo 5 (`vitaTextureCacheLimit`) para `64 MiB` (16 páginas BC3) em [gl_legacy_renderer.c](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/gl_legacy/gl_legacy_renderer.c#L1305).
    - Ajustado `vitaTrimMiB` para `0ULL` em transições não-compartilhadas e `36 MiB` em transições compartilhadas em [runner.c](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/runner.c#L3719-L3725).
    - Ajustadas as metas de pressão e trim de borda no Capítulo 5 em [playable_main.c](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/vita-probe/source/playable_main.c#L1241-L1256) (`borderPressureMiB = 70ULL`, `borderTrimMiB = 64ULL`).
    - Isso libera mais de **18 MiB de CPU RAM física permanente** para a execução de código GML, eventos de objetos e movimentação do jogador sem estouro de memória (OOM).

- **Novo Recurso: Inspetor/Seletor de Objetos e Colisão**:
  - Quando a opção de visualização de máscaras de colisão (`debugCollisionMasks`) está ativa nas configurações:
    - O jogador/desenvolvedor pode **tocar diretamente em qualquer objeto/colisão na tela touch** do Vita ou pressionar **L-TRIGGER + D-Pad Esquerda/Direita (ou Quadrado)** para alternar a seleção entre os objetos ativos da sala.
    - O objeto selecionado é destacado visualmente com um **retângulo Amarelo brilhante e retículo Magenta (crosshair)** no centro de origem `(x, y)`.
    - Os detalhes completos do objeto (ID da Instância, Nome do Objeto GML, Nome da Sprite, ID da Página de Textura `pageId` e Coordenadas de Bounding Box) são gravados automaticamente nos logs de diagnóstico (`ux0:data/deltarune/deltarunevita/butterscotch-probe.log`), permitindo rastrear instantaneamente qual objeto ou textura precisa de ajuste.

- **Fim do rename `data.win` → `game.droid`**: engine (`playable_main.c`, `main.c`, `vitagl_probe.c`)
  passa a procurar `chapterN/data.win` diretamente, tanto no carregamento normal do capítulo quanto
  no override de mod (`mods/<nome>/chapterN/data.win`). `prepare-windows-data.ps1` e
  `prepare-vita-mods.ps1` pararam de renomear o arquivo. Motivo: simplificar o pipeline de mods — um
  `.zip` de tradução pode ser extraído sem transformação. **Requer teste no Vita real antes de
  release** (carregamento normal e com mod ativo).
- **Download automático de mods de idioma nos patchers** (desktop e Web Patcher): implementado o
  fluxo de baixar o `.zip` do mod (4 mirrors: Mediafire, Google Drive, Mega, Archive.org — ordem
  definida em `mods/mods_list.txt`) e extrair para `deltarunevita/mods/<nome>/`.
  - Corrigido bug no patcher desktop (`deltarune_vita_patcher.py`): `download_mod()` referenciava
    `urllib.request` antes do `import` local, o que sempre lançava `UnboundLocalError` e fazia o
    download automático falhar silenciosamente (caía no aviso genérico de erro).
  - Corrigido bug no Web Patcher (`index.html`, branch `gh-pages`): o `.zip` baixado (`modZip`) era
    carregado mas nunca gravado no pacote final gerado — a tradução baixada nunca chegava a
    `deltarunevita/mods/PTBR/`.
  - `DeltaruneVitaPatcher.spec` embute `mods/mods_list.txt` no `.exe` via caminho relativo a partir
    de `SPECPATH` (pasta que contém o `.spec`) — `root.parent.parent.parent`.
  - Extração do mod agora renomeia `game.droid` → `data.win` em cada arquivo do `.zip` (nos dois
    patchers), já que o `.zip` hospedado ainda usa a convenção antiga e o engine só reconhece
    `data.win` desde a mudança acima.
- **Correção: `ui/`, `sounds/` e `borders/` ausentes no zip gerado pelo Web Patcher.** Esses arquivos
  não têm equivalente na instalação Steam (`source: null` no manifest) e eram gerados como `mode:
  "patch"` (um bsdiff contra um arquivo vazio, só reaproveitando o mecanismo do desktop). O patcher
  desktop aplica esse bsdiff normalmente (`bsdiff4`), mas o Web Patcher nunca implementou aplicação de
  patch/bzip2 em JS — esses registros eram contados mas nunca escritos no zip, deixando `ui/`,
  `sounds/` e `borders/` de fora do resultado gerado pela Web. Como não há `source` pra diferenciar,
  o bsdiff não trazia ganho de tamanho nesses casos; `build_patch_data.py` passou a gerar um novo modo
  `"embed"` para eles (cópia direta, sem diff), bundled em `patch_data/embedded/` no desktop e em
  `embedded/` na branch `gh-pages` para o Web Patcher buscar via `fetch()`. `manifest` format bump
  3 → 4. Ver [api.md](api.md#manifest-do-patcher-manifestjsonmanifestjs) para o contrato completo dos
  modos `copy`/`patch`/`embed`. **Requer teste do Web Patcher real antes de release.**

- **Novo: Seam's Patcher (`SeamsPatcher_64bit.exe`/`_32bit.exe`)** — versão empacotada do Web Patcher
  que não depende do GitHub Pages estar no ar. `seams_patcher_launcher.py` sobe um servidor HTTP local
  (`http.server`, porta livre escolhida automaticamente) servindo uma cópia estática do Web Patcher
  (`patch_data`-style bundle em `src/webpatcher/`) e abre no navegador padrão via `webbrowser.open()`.
  A File System Access API (usada para ler a instalação Steam sem upload) funciona normalmente em
  `http://127.0.0.1`, tratado como contexto seguro pelos navegadores assim como `https`. Testado de
  ponta a ponta localmente: servidor sobe, navegador abre, todos os assets (incluindo `embedded/` e o
  `jszip.min.js` agora vendorizado) respondem HTTP 200. `Build_Patcher.bat` atualizado para compilar
  os dois patchers (desktop + Seam's) nas duas arquiteturas no mesmo fluxo. **Requer teste real do
  usuário antes de release** (o smoke test cobriu só o carregamento inicial da página).
- **Correção: bug de sintaxe em `choose_language()`** (`deltarune_vita_patcher.py`) que quebrava
  qualquer build do patcher desktop — o loop `while True: / choice = input(...)` tinha sumido,
  deixando um `if` órfão com indentação inválida. Causa não identificada com certeza (não foi uma
  edição da IA nesta sessão); corrigido restaurando o loop e a opção `[0] Exit`.
- **Correção: `SteamFiles`/`VitaFiles` não encontrados ao separar os `.exe` de 32/64-bit em pastas
  próprias.** Não era bug de código — `app_dir()` já resolve para a pasta do `.exe` em execução
  (nunca um diretório acima), confirmado via simulação direta. O problema era o `.exe` ter sido
  movido para uma subpasta sem levar `SteamFiles`/`VitaFiles` junto (ficavam pra trás na pasta
  compartilhada criada pelo `Build_Patcher.bat`). Documentado nas notas internas do projeto com a dica de usar
  `mklink /J` para não duplicar a instalação Steam por pasta.
- **`jszip.min.js` vendorizado** em `assets/jszip.min.js` na branch `gh-pages` (era carregado via CDN
  `cdnjs.cloudflare.com`) — remove uma dependência externa do Web Patcher e do Seam's Patcher.

## v0.53 – v0.63 (não documentado)

O build atual embute `PORT_BUILD_VERSION "v0.63"` (`src/vita-probe/source/playable_main.c`), mas o
changelog público (README) só cobre até v0.52. Há pelo menos uma mudança conhecida nesse intervalo
(migração do diretório de save mixed-case usado em v0.53–v0.56, ver `playable_main.c:357`), mas o
restante não está registrado. **Pendente**: reconstruir esse intervalo a partir do histórico de
desenvolvimento disponível, ou passar a registrar cada versão aqui a partir de agora.

## v0.52 e anteriores (histórico, migrado do README)

| Versão | Principais mudanças |
|---|---|
| v0.08 | Primeiro VPK de prova de conceito integrando Butterscotch e VitaGL. |
| v0.08 – v0.22 | Verificações de viabilidade, testes de assets, renderização de texturas e checagens de áudio. |
| v0.23 | Capítulos 1 e 2 jogáveis pela primeira vez. |
| v0.24 – v0.34 | Ajustes diversos, correções de bugs e funcionalidades baseadas no runner Android. |
| v0.35 | Última atualização usando assets derivados do port Android. |
| v0.36 | Início da migração de dados Android para arquivos nativos Windows/Steam. |
| v0.37 | Ajustes no pipeline de carregamento do runner Windows, fontes customizadas e áudio externo. |
| v0.38 | Reversão para uma build estável do VitaGL; correção de diagnóstico de renderização do primeiro frame. |
| v0.39 | Correção de crash crítico causado pelo overlay de controles touch. |
| v0.40 | Suporte a música externa, Game Settings redesenhado, menu de seleção de capítulo, cache de texturas e bordas de console. |
| v0.41 | Streaming de áudio, bordas dinâmicas sensíveis ao contexto, log de quedas de performance. |
| v0.42 | Correção de caminho de arquivos de áudio e redução de thrashing/recarregamento de texturas no Capítulo 2. |
| v0.43 | Acesso direto à biblioteca de músicas do Vita; correção de bug de sincronização de faixa no logo do Capítulo 5. |
| v0.44 | Otimização do atlas de texturas e aumento do buffer de streaming de áudio. |
| v0.45 | Reformulação da interface do Game Settings. |
| v0.46 | Correção de regressão que impedia capítulos de iniciar corretamente. |
| v0.47 | Confirmação adicionada ao Chapter Select, ícone de configurações restaurado, cache de texturas expandido. |
| v0.48 | Culling de tiles fora de câmera para melhorar performance na área da cidade do Capítulo 5. |
| v0.49 | Correção de renderização de fonte no Capítulo 5, fade de tela ao carregar save states, primeiro patcher público. |
| v0.50 | Melhorias em cache de áudio/textura, transições de sala, bordas dinâmicas, padrões de touch, estabilidade dos Capítulos 2/5. |
| v0.51 | Preparação de texturas gerada pelo patcher, cache de capítulos, mais diagnósticos de performance em runtime. |
| v0.52 | Carregamento animado de capítulo, capturas Debug Dev, cache de texturas em RAM, otimização de textura font-safe, perfis gráficos Original/Medium/Low selecionáveis. |

## Consolidação da documentação (2026-07-24)

- `docs/` reduzido para conter apenas material válido: `PROGRESS.md` (histórico narrativo) e a nova
  estrutura (`arquitetura.md`, `roadmap.md`, `backlog.md`, `api.md`, `changelog.md`).
- Removidos: `BASELINE-INSTRUCTIONS.md`, `DLTVITA-0001-REPORT.md`, `PORT-SOLOADER-TESTE.md`,
  `INSTALL-PT-BR.txt`, `CONTEXTO_MESTRE_PORTS_PSVITA_PARA_OUTRA_IA.txt` — todos da era YoYo
  Loader/SoLoader, abordagem abandonada no início do projeto.
- Conteúdo técnico de `detalhamento_projeto.txt` e `pipeline_build_tecnico.txt` (raiz) incorporado a
  `docs/arquitetura.md`; os `.txt` originais foram removidos para evitar duplicação.
- Criadas notas internas com visão geral e convenções do projeto.

## v0.74-5 — 2026-09-14
Struct opcional aplicada antes de Create: corrigido abort reproduzido na grama. Reserva PHYCONT do Chapter 5 alinhada ao Chapter 3 para vídeo. Validação no Vita pendente; FPS, lojas e GPU crash não declarados corrigidos. Ver [análise](modern-gl-v074-5-fixes.md).

## Seguimento v0.74 — testes de 14/09 à noite
Corrigidas dimensões de tilemap ausente (-1), reserva física do vídeo Chapter 5, pausa dos sons criados durante startup e estado gráfico do loading. Validação no Vita pendente. Ver [relatório](modern-gl-v074-6-followup.md).

Entrega v0.74-8: pacote final dessa rodada; 18 testes locais aprovados. v0.74-6 e v0.74-7 foram intermediários. Vídeo, áudio e loading precisam de reteste no Vita.
# Revisão em andamento — Chapter 3 / v0.74

Rodada posterior à v0.74-15: rótulos principais do Game Settings reduzidos para
escala 1,60; espelhamento dos cantos corrigido em Modern-GL. O teste de vídeo
anterior usou somente o VPK: os MP4 convertidos devem ser instalados também.

Correção posterior à v0.74-14: tamanho e alinhamento dos glifos acentuados do
Game Settings agora usam a área visível da letra, sem ampliar pela altura
transparente do atlas. Comparação com a fonte do Chapter 5 confirmou pixels
idênticos para o A em 6x9; prévia local das palavras conferida.

Diagnóstico repetitivo do ritmo limitado a uma mensagem por segundo; recorte de
fotografias corrigido para respeitar a orientação das superfícies; shaders de
cinza incluídos na lista compacta do Legacy; acentos do menu passam a usar glifos
completos da fonte estendida. Evidências e limites de validação em
[ch3-v074-review.md](ch3-v074-review.md). Testes no Vita ainda pendentes.
