# Documentação Técnica de Builds — Linha `v0.81` (`v0.81-1` a `v0.81-11`)

O script de build (`scripts/build-butterscotch-probe.ps1`) calcula automaticamente a sequência incremental `v0.81-<N>` a partir de `config/version.txt`, sincroniza o rótulo de versão no `data.win` do Chapter 0, empacota `artifacts/current/Deltarune-v0.81-<N>.vpk` e gera um snapshot local completo em `artifacts/Backups/v0.81-<N>_<timestamp>/`.

Abaixo está o registro detalhado de **o que foi feito**, **por quê** (causa raiz nos logs/código) e **pra quê** (objetivo prático em jogo) em cada build da série `v0.81`.

---

## `v0.81-11` — Resolução Definitiva da Dupla Barra de TP no Floradinn, Eliminação do Thrashing de Memória em Castle Town/Hometown (Trim de 56 MiB) e VSync Incondicional a 30 FPS

### 1. Eliminação Definitiva da Dupla Barra de TP na Batalha do Floradinn
- **O que foi feito**:
  - Em [`src/butterscotch/src/runner.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/runner.c) (`Runner_executeResolvedEvent` e `Runner_drawViews`):
    - Em `Runner_executeResolvedEvent`, quando `obj_sakurafubuki_new` detecta `stage >= 2`, `pct >= 2.0` ou qualquer indício de batalha ativa (`obj_tensionbar` ou `obj_battlecontroller`), a variável de instância `surf_view1` é redefinida para `-4` e o executor retorna imediatamente de **qualquer** subtipo de evento de desenho (`DRAW_NORMAL`, `DRAW_BEGIN`, `DRAW_END`, `DRAW_PRE`, etc.).
    - Em `Runner_drawViews`, quando a floricultura/encontro atinge `stage >= 2`, a renderização da `view 1` é permanentemente desativada (`runner->views[1].enabled = false; runner->views[1].surfaceId = -1; continue;`), impedindo que a câmera secundária continue desenhando e sobrescrevendo o alvo de renderização.
  - Em [`src/butterscotch/src/gl_legacy/gl_legacy_renderer.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/gl_legacy/gl_legacy_renderer.c) (`glLegacyDrawSurface`):
    - Removido o loop global que buscava instâncias arbitrárias de `obj_sakurafubuki_new` na sala quando outro objeto realizava chamadas de desenho. A interceptação de `surf_view1` agora é estritamente restrita a quando `ctx->currentInstance` for o próprio `obj_sakurafubuki_new`.
    - Quando `ctx->currentInstance == sakura`, se a superfície não possuir dimensões de 640x480 (como a `healthbar_surf` de 96x250) ou a transição estiver encerrada (`stage >= 2 || pct >= 2.0f`), o desenho é bloqueado e `surf_view1` é resetado para `-4`.
- **Por quê**:
  - Nas capturas enviadas pelo usuário (`media_1794090902281_f00964bf.jpg` e `media_1794090902277_39d047f4.jpg`), eram exibidas duas barras de TP: uma duplicada no canto esquerdo (`x ≈ 4, y ≈ 0`, sem texto de porcentagem nem ícone TP) e a barra legítima (`x ≈ 42, y ≈ 40`, com texto e logo).
  - A causa raiz estava na interação de dois fatores:
    1. Ao término da transição de pétalas, `obj_sakurafubuki_new` liberava sua surface com `surface_free(surf_view1)`, mas mantinha o ID antigo na variável de script. Ao entrar na batalha, `obj_tensionbar` alocava `healthbar_surf` (96x250), que recebia o ID recém-liberado pelo gerenciador de superfícies. Como `surface_exists(surf_view1)` tornava-se verdadeiro, `obj_sakurafubuki_new_Draw_73` desenhava a surface em `(camerax(), cameray())` = `(0, 0)`.
    2. Além disso, no renderizador `glLegacyDrawSurface`, a rotina de interceptação anterior buscava qualquer `obj_sakurafubuki_new` existente na sala e descartava a chamada de desenho se a superfície tivesse 96x250; como resultado, quando a verdadeira barra de TP (`obj_tensionbar`) tentava se desenhar, sua chamada legítima chegava a ser suprimida em determinados quadros!
- **Pra quê**:
  - Eliminar por completo a barra espelhada em `(0, 0)` e garantir que a barra oficial de TP renderize com perfeição na sua coordenada correta (`x ≈ 42`) com todos os textos e logotipos intactos.

### 2. Prevenção de Thrashing de Memória e Quedas em Castle Town / Hometown no Chapter 5
- **O que foi feito**:
  - Em [`src/butterscotch/src/runner.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/runner.c) (`Runner_handlePendingRoomChange`):
    - Adicionadas as regras de orçamento compartilhado `vitaSharedDarkCastleTransition ? 56ULL :` e `vitaSharedTownTransition ? 56ULL :` na árvore de transições do Chapter 5.
  - Em [`src/butterscotch/src/gl_legacy/gl_legacy_renderer.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/gl_legacy/gl_legacy_renderer.c) (`GLLegacyRenderer_prepareRoomTextureSet`):
    - Estendida a condição `vitaCh4SchoolCastleTransition` para incluir o Chapter 5 e todas as salas `room_dw_castle_*` (`room_dw_castle_town`, `room_dw_castle_cafe`, `room_dw_castle_tv`, `room_dw_castle_dojo`), pré-aquecendo os atores dinâmicos do Dark World (`spr_krisd`, `spr_susied`, `spr_ralseid`, `spr_kris_dw`, `spr_susie_dw`, `spr_ralsei_dw`).
- **Por quê**:
  - No log `butterlog12-room_dw_castle_town_20261108-021257.log`, ao navegar entre salas do castelo ou da cidade no Chapter 5, a transição caía no valor default de 32 MiB de VRAM (`vitaTrimMiB = 32`). Como Castle Town e Hometown demandam entre 44 e 52 MiB de atlas residentes, a cada porta atravessada o motor realizava um expurgo agressivo de 12 a 20 MiB de texturas, para em seguida recodificar e fazer upload das mesmas páginas de atlas no quadro inicial da sala de destino, gerando engasgos de 90 a 195 ms e quedas de FPS.
- **Pra quê**:
  - Manter um pool aquecido de 56 MiB em transições de Castle Town e Hometown no Chapter 5, eliminando descartes desnecessários e garantindo travessia instantânea entre portas e lojas.

### 3. VSync Incondicional a 30 FPS (`waitForVblank = true`)
- **O que foi feito**:
  - Em [`src/vita-probe/source/playable_main.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/vita-probe/source/playable_main.c) (linha ~6686):
    - Configurado `waitForVblank` para ser incondicionalmente verdadeiro quando `settings.fpsTargetMode == 0` (30 FPS), da mesma forma que já era feito para o modo 3 (Unlocked):
      ```c
      bool waitForVblank =
          settings.fpsTargetMode == 3 ||
          settings.fpsTargetMode == 0 ||
          (settings.vsyncEnabled && settings.fpsTargetMode == 2);
      ```
- **Por quê**:
  - Anteriormente, o modo 30 FPS só ativava `waitForVblank` se `settings.vsyncEnabled` estivesse explicitamente ligado nas configurações do usuário. Quando desativado, o motor disparava `vglSwapBuffers(GL_FALSE)` em momentos arbitrários fora do intervalo vertical do LCD (60 Hz), causando desalinhamento de fases com o display do Vita, tearing e quedas abaixo de 30 FPS devido a frames que perdiam a janela de scanout.
- **Pra quê**:
  - Garantir cadência rigorosa de 33,333 ms (apresentação a cada exatamente 2 VBlanks) em 30 FPS em qualquer configuração do usuário.

---

## `v0.81-10` — Resolução da Queda a 30 FPS (Pacer Bugfix), Fastpaths Nativos em C para Ch. 4/5 (Floortex & Sunlayer), Glifos DualShock 4 na Batalha do Krákrá e Prevenção Definitiva da Dupla Barra de TP

### 1. Resolução da Queda a 30 FPS na Cidade e Causa Raiz do Frame Pacer
- **O que foi feito**:
  - Em [`src/vita-probe/source/vita_frame_pacer.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/vita-probe/source/vita_frame_pacer.c) (`VitaFramePacer_setRate`, `VitaFramePacer_reset`, `VitaFramePacer_plan`):
    - O prazo inicial (`deadlineUs`) é inicializado como `nowUs + period`.
    - Eliminado o adiantamento prematuro incondicional no topo de `VitaFramePacer_plan`.
    - Quando um quadro sofre atraso (`nowUs >= pacer->deadlineUs`), o pacer agora avança o prazo em passos inteiros (`while (pacer->deadlineUs <= nowUs) pacerAdvanceDeadline(pacer);`) e retorna imediatamente `coarseSleepUs = 0` (sem penalizar o quadro atual com sleep redundante).
    - Em quadros dentro do prazo, o avanço do prazo ocorre ao final do plano, mantendo a fase travada em 33.333 µs.
- **Por quê**:
  - Na cidade (`room_town_north`, `room_town_mid`), o jogo rodava a 46 FPS em modo Unlocked/60 FPS, mas caía abruptamente para ~15 FPS ao travar em 30 FPS. A causa raiz era um bug de double-advance no pacer: na v0.81-9, `plan()` chamava `pacerAdvanceDeadline()` no topo e, quando um quadro passava 1 µs do prazo, o laço avançava para o próximo intervalo; no quadro subsequente, o topo chamava `pacerAdvanceDeadline()` novamente, jogando o prazo 2 períodos à frente (~66 ms) e induzindo uma pausa forçada de mais de 55 ms via `sceKernelDelayThread`, colapsando o framerate pela metade.
- **Pra quê**:
  - Garantir que o modo 30 FPS mantenha uma cadência de exatamente 33.333 ms sem introduzir sleeps artificiais em quadros atrasados, proporcionando fluidez contínua na cidade e em todas as salas do jogo.

### 2. Fastpaths Nativos em C para Salas Pesadas do Chapter 4 e 5 (`obj_plat_floortex_FLOOR` e `obj_parallax_cliffs_sunlayer`)
- **O que foi feito**:
  - Em [`src/butterscotch/src/vm_builtins.h`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/vm_builtins.h) e [`src/butterscotch/src/vm_builtins.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/vm_builtins.c):
    - Implementada a função nativa `VMBuiltins_tryFloortexDraw(VMContext* ctx, Instance* target, int32_t codeId)`.
    - Intercepta os eventos de desenho de plataformas de grama/chão (`gml_Object_obj_plat_floortex_FLOOR_Draw_0` e `gml_Object_obj_plat_floortex_FLOOR_AlignBottom1TILE_Draw_0`).
    - Ignora instâncias fora da visão (`is_culled == true`) ou não geradas (`!gen`).
    - Quando as superfícies estão assadas e sem refresh pendente (`front_surf`, `surf`, `back_surf`), pula a execução do script GML de 9,1 KB com dezenas de laços interpretados e desenha diretamente via `builtin_draw_surface_ext`.
  - Em [`src/butterscotch/src/runner.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/runner.c):
    - Integrado o dispatcher de `VMBuiltins_tryFloortexDraw` na cadeia de desenho nativo.
    - Em `Runner_tryNativeCliffLayersDraw`, adicionado caching de superfície para `obj_parallax_cliffs_sunlayer`: uma vez assada a surface de 400x300, seu desenho é feito diretamente em C via `runner->renderer->vtable->drawSurface`, evitando que o evento GML `OTHER_USER0` re-construa a surface a cada frame (economia de 15 a 20 ms por frame no Vita).
- **Por quê**:
  - O profiling das salas `room_dw_cliff_twirlflowerwind` e penhascos revelou que `obj_plat_floortex_FLOOR` acumulava 1.788.644 µs em apenas 53 frames (~33,7 ms/frame só em scripts GML de chão). Além disso, cada plataforma alocava até 3 superfícies dinâmicas, levando a VRAM a 96,5 MB (quase o teto de 100 MB). Com isso, o LRU de texturas do VitaGL era forçado a desalocar páginas de sprites residentes, fazendo com que Kris, o chão e os ataques dos inimigos sumissem da tela.
- **Pra quê**:
  - Reduzir drasticamente o tempo de CPU por frame nas salas de penhasco e castelo, mantendo o consumo de VRAM sob controle e garantindo que todos os projéteis, chão e personagens permaneçam visíveis com FPS substancialmente maior.

### 3. Glifos PS4/DualShock 4 na Batalha do Krákrá (`room_dw_cliff_kawkawdash`)
- **O que foi feito**:
  - Em [`src/vita-probe/source/playable_main.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/vita-probe/source/playable_main.c):
    - Criada a rotina `vitaEnsureGamepadActive(runner)` chamada imediatamente após `Runner_step(runner)`.
    - Assegura que `obj_gamecontroller.gamepad_active = true`, `gamepad_id = 0` e `gamepad_type = "Sony DualShock 4"` sejam mantidos tanto na instância quanto no escopo global antes de qualquer evento de Draw.
- **Por quê**:
  - No código decompilado de `obj_gamecontroller_Step_1`, a checagem `i != gamepad_id` resetava `gamepad_active = 0` para o slot 0 do Vita (`gamepad_id == 0`), fazendo com que os scripts de texto (`scr_get_input_name`) revertessem para os colchetes de teclado de PC (`[Z]`, `[X]`) na batalha contra o Krákrá.
- **Pra quê**:
  - Garantir que todos os botões de ação e dicas de batalha exibam os ícones nativos do DualShock 4 / PS4 em vez de teclas de computador.

### 4. Prevenção Definitiva da Dupla Barra de TP na Batalha do Floradinn
- **O que foi feito**:
  - Em [`src/butterscotch/src/runner.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/butterscotch/src/runner.c) (`Runner_executeResolvedEvent`):
    - Generalizada a verificação de `obj_sakurafubuki_new` para checar a existência de `obj_tensionbar` ativo na sala (`hasTensionBar`).
    - Quando uma barra de TP existe (ou `stage >= 2` ou `pct >= 2.0`), `surf_view1` é redefinido para `-4` e os eventos de desenho `DRAW_BEGIN` (`Draw_72`) e `DRAW_END` (`Draw_73`) são abortados.
- **Por quê**:
  - `obj_sakurafubuki_new_Draw_73` desenha `surf_view1` em `(camerax(), cameray())`. Como o GameMaker liberava a surface na transição sem zerar `surf_view1 = -4`, a alocação subsequente da `healthbar_surf` pelo `obj_tensionbar` reciclava o mesmo ID de surface, fazendo com que `Draw_73` desenhasse uma segunda barra de TP espelhada no canto superior esquerdo da tela.
- **Pra quê**:
  - Eliminar por completo a barra de TP fantasma/duplicada na batalha do Floradinn e em qualquer outra transição de batalha.

### 5. Som de Inicialização (`AUDIO_INTRONOISE.ogg`) e Logo em Qualquer Início a Frio
- **O que foi feito**:
  - Em [`src/vita-probe/source/playable_main.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/vita-probe/source/playable_main.c):
    - Utilizada a flag persistente `static bool s_has_shown_boot_logo` para interceptar a primeira transição para `PLACE_MENU`, independente do nome da sala anterior (`INITIALIZE`, `room_intro_ch5`, etc.).
- **Por quê**:
  - Capítulos como o Chapter 5 não vêm de `INITIALIZE`, mas sim de `room_intro_ch5`, o que desviava do desvio anterior e entrava no menu em silêncio.
- **Pra quê**:
  - Garantir que a intro clássica e o áudio toquem sempre ao carregar o jogo a frio.

---

## `v0.81-9` — Suavização de Bordas Animadas (Sem Corte Visível), VSync/Estabilidade de Frame Pacing a 30 FPS e Áudio `AUDIO_INTRONOISE.ogg` no Boot

### 1. Borda Animada: Eliminação do corte visível entre colunas animadas e arte estática
- **O que foi feito**:
  - Em [`src/vita-probe/source/vita_borders.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/vita-probe/source/vita_borders.c) (`drawBorderCurrent` e novo helper `drawBorderQuadFeathered`):
    - A borda estática (`borderTexture`) agora é sempre renderizada por baixo em tela cheia (960x544) antes de compor as colunas animadas laterais (`frames_side.bani`).
    - Cada coluna lateral animada (120 pixels) foi dividida em duas faixas: uma faixa sólida externa (0–96 px na esquerda; 864–960 px na direita) com alfa total e uma faixa de transição interna de 24 pixels (96–120 px na esquerda; 840–864 px na direita) com gradiente linear de alfa (`alpha -> 0.0` na esquerda; `0.0 -> alpha` na direita).
- **Por quê**:
  - No modo `frames_side.bani`, o runtime desenhava quads retangulares rígidos de 120 px em cada lateral da tela (`-1.0` a `-1.0 + edge` e `1.0 - edge` a `1.0`). Em qualquer nível de zoom (notadamente no zoom de 88% padrão das bordas de console ou no 100% onde o jogo cobre a partir de 117 px), a transição entre os pixels animados e os pixels estáticos da borda gerava uma emenda vertical visível de corte seco.
- **Pra quê**:
  - Eliminar completamente a linha de corte visível, mesclando de forma suave e contínua os 24 pixels internos da animação com a borda de fundo estática.

### 2. VSync e Estabilização dos Quadros a 30 FPS (Eliminação de Micro-Engasgos e Oscilação de Frame Time)
- **O que foi feito**:
  - Em [`src/vita-probe/source/playable_main.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/vita-probe/source/playable_main.c) (linha ~5912):
    - Incluiu `settings.fpsTargetMode == 0` (30 FPS) em `waitForVblank` quando `settings.vsyncEnabled` estiver ativo:
      `bool waitForVblank = settings.fpsTargetMode == 3 || (settings.vsyncEnabled && (settings.fpsTargetMode == 2 || settings.fpsTargetMode == 0));`
  - Em [`src/vita-probe/source/vita_frame_pacer.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/vita-probe/source/vita_frame_pacer.c) (`VitaFramePacer_plan`):
    - Quando um quadro sofre atraso (`nowUs >= pacer->deadlineUs`), o pacer agora avança o prazo em múltiplos exatos do período de 33.333 µs (`while (pacer->deadlineUs <= nowUs) pacerAdvanceDeadline(pacer);`) em vez de resetar a fase com `pacer->deadlineUs = nowUs`.
- **Por quê**:
  - No código anterior, `waitForVblank` excluía intencionalmente o modo 30 FPS, forçando a troca de buffers (`vglSwapBuffers`) a ocorrer de forma assíncrona sem sincronização com o VBlank do LCD do Vita (60 Hz). Isso causava tearing e fazia o scanout de hardware amostrar quadros em intervalos alternados (33 ms, 50 ms, 16 ms) devido ao jitter de agendamento de threads do kernel (`sceKernelDelayThread`). Ao mesmo tempo, cada atraso de 1 ms redefinia o prazo do software pacer para `nowUs`, quebrando a cadência regular de 30 FPS.
- **Pra quê**:
  - Travar as apresentações a 30 FPS exatamente a cada 2 pulsos de VBlank do LCD (33,333 ms constantes), acabando com as oscilações e engasgos visíveis e proporcionando uma experiência de jogo sólida e estável.

### 3. Reprodução do Som de Intro `AUDIO_INTRONOISE.ogg` antes do Menu Inicial
- **O que foi feito**:
  - Em [`src/vita-probe/source/playable_main.c`](file:///c:/Users/wolff/Documents/SDKVita/DeltaruneVita/src/vita-probe/source/playable_main.c) (linhas ~6070–6140):
    - Ao iniciar o jogo a partir de `ROOM_INITIALIZE` (cold boot) com destino direto para `PLACE_MENU` (situação padrão em capítulos não finalizados com save existente), o motor busca o índice da sala `PLACE_LOGO` e redireciona a transição para ela antes de entrar em `PLACE_MENU`.
    - Ao entrar em `PLACE_LOGO`, assegura o ganho da categoria SFX (`(float)settings.sfxVolume / 10.0f`) e desativa o `transitionHold` de áudio (`AlAudioSystem_setTransitionHold(audio, false)`).
- **Por quê**:
  - No código original do GameMaker (`obj_initializer2_Step_0`), quando o capítulo já havia sido zerado (`menu_go == 2`), o jogo transitava por `room_legend` e `PLACE_LOGO`, onde `PROCESS_LOGO` chamava `snd_play(AUDIO_INTRONOISE)` antes de ir para `PLACE_MENU`. Porém, quando o capítulo não estava zerado (`menu_go == 1`), o código saltava diretamente para `PLACE_MENU` em silêncio. Além disso, o muting de transição de salas mantinha as trilhas congeladas até a apresentação da sala de destino.
- **Pra quê**:
  - Garantir que a vinheta e o áudio clássico do logo (`AUDIO_INTRONOISE.ogg`) toquem ao iniciar o jogo antes de alcançar a tela de seleção/menu inicial em qualquer situação, exatamente como solicitado pelo usuário.

---

## `v0.81-8` — Correções do Chapter 5 (Dupla Barra de TP, FPS Pré-Transição e Percurso no Jardim, Travada de Troca de Sala, Penhascos/Krákrá, Glifos PS4, Bordas Noturnas/Floricultura e Room Navigator)

### 1. Dupla barra de TP na batalha contra Floradinn e FPS antes/depois da transição de pétalas (`room_dw_garden_floradinnencounter`)
- **O que foi feito**:
  - Em `src/butterscotch/src/runner.c` (`Runner_executeEvent` e `Runner_drawViews`) e `src/butterscotch/src/gl_legacy/gl_legacy_renderer.c` (`glLegacyDrawSurface`):
    - Quando `obj_sakurafubuki_new` atinge `transition_stage >= 2` (`transition_percent >= 2.0`), `surf_view1` é imediatamente limpo para `-4` e os eventos `Draw_0`, `Draw_72` e `Draw_73` são ignorados; além disso, `glLegacyDrawSurface` bloqueia o desenho se `surf_view1` apontar para uma surface reciclada de `96x250` (`healthbar_surf`).
    - Em `Runner_drawViews`, a `view 1` (`surf_view1`) não é mais pulada em frames ímpares durante `transition_stage == 0`, e `obj_blocktree_bg` em `view 0` é suprimido enquanto `surf_view1` cobre 100% da tela (`stage == 0`), além de ganhar frustum culling por câmera (`instance->x ± 260`, `instance->y ± 280`) e pré-aquecimento (`gardenCore`) em `GLLegacyRenderer_prepareRoomTextureSet`.
    - Em `src/vita-probe/source/vita_animated_borders.inc` (`animatedBordersLogPerf`), foi removida a escrita síncrona em arquivo (`sceIoOpen`/`sceIoWrite`/`sceIoClose`) dentro do loop de renderização da borda animada.
- **Por quê**:
  - **Dupla barra de TP** (`butterlog4-room_dw_garden_floradinnencounter_20261107-190339.log`): `obj_sakurafubuki_new_Step_0` chamava `surface_free(surf_view1)` ao encerrar a transição (`transition_stage = 2`), mas mantinha o ID numérico antigo em `surf_view1`. Quando a batalha começava, `obj_tensionbar` criava `healthbar_surf` (`96x250`) recebendo esse mesmo ID, e `obj_sakurafubuki_new_Draw_73` desenhava `surf_view1` em `(camerax(), cameray())` = `(0, 0)` por cima do texto `TP %`.
  - **FPS baixo antes da transição e no percurso**: durante `transition_stage == 0`, `surf_view1` (`view 1`) cobre 100% da tela visível; pular a `view 1` em frames ímpares (`(frameCount & 1) != 0`) limitava a atualização visual da tela a **15 FPS** antes da transição, enquanto desenhava todas as árvores da sala duas vezes (`view 0` + `view 1`) sem culling de câmera e sem preload de `spr_blocktree_parts*` / `spr_floradinn_overworld`.
- **Pra quê**:
  - Eliminar definitivamente a segunda barra de TP sobreposta em `(0, 0)`, manter **30 FPS** antes, durante e após a transição de pétalas, e evitar engasgos de leitura ao caminhar por `room_dw_garden_floradinnencounter`.

### 2. Eliminação da travada de `2,2 s` ao trocar de sala no Jardim (`room_dw_garden_*`)
- **O que foi feito**:
  - Em `src/butterscotch/src/runner.c` (`Runner_handlePendingRoomChange`), transições entre salas `room_dw_garden_*` (`vitaSharedGardenTransition`, meta de `44 MiB`) e entre salas `room_dw_cliff_*` (`vitaSharedCliffTransition`, meta de `36 MiB`) foram incluídas em `vitaSharedTransition`.
  - Em `src/butterscotch/src/gl_legacy/gl_legacy_renderer.c` (`GLLegacyRenderer_trimTextureCacheForRoomChange` e `vitaEvictSpecificTexturePage`), `glFinish()` e `vglForceGarbageCollection()` passaram a ser executados **uma única vez por lote** (`vitaBatchRoomTrimActive`) em vez de dentro do loop de cada página descartada.
- **Por quê**:
  - No log `butterlog5-room_dw_garden_mushrooms_20261107-190512.log`, a transição `room_dw_garden_floradinnencounter -> room_dw_garden_mushrooms` não era reconhecida como área compartilhada (`vitaTrimMiB = 0`), descartando 68 páginas e executando 68 barreiras síncronas `glFinish() + vglForceGarbageCollection()` seguidas (`texture_trim_complete` levou `2,24 s` de `total_us = 2,40 s`).
- **Pra quê**:
  - Reduzir o tempo de troca de sala entre áreas adjacentes do Jardim e dos Penhascos para poucos milissegundos e preservar na VRAM os atlas compartilhados de Kris/Susie/Ralsei/vegetação.

### 3. Penhascos (`room_dw_cliff_kawkawdash` e `room_dw_cliff_twirlflowerwind`): FPS, texturas e ataques múltiplos do Krákrá (`Kawkaw`)
- **O que foi feito**:
  - Em `src/vita-probe/source/vita_gpu_rules.c` (`VitaGpuRules_ch5CliffSimpleMode`) e `src/butterscotch/src/runner.c` (`vitaEnableChapter5CliffSimpleMode` e `Runner_tryNativeCliffLayersDraw`), o modo Simple nativo em C foi estendido para **todas** as salas `room_dw_cliff_*` e para a subclasse `obj_parallax_cliffs_moving_layers_back` (`strncmp(objectName, "obj_parallax_cliffs_moving_layers", 33)`).
  - Em `src/butterscotch/src/gl_legacy/gl_legacy_renderer.c`:
    - Surfaces intermediárias nos penhascos (`vitaCh5CliffGpuReserve`, exceto `application_surface`) agora usam `GL_RGBA4` (`GL_UNSIGNED_SHORT_4_4_4_4`), economizando `~9,7 MiB` de VRAM (`19,5 MiB -> 9,8 MiB`).
    - A reserva física de VRAM em `vitaTextureUploadHasPhysicalHeadroom` foi ajustada de `12–20 MiB` para `6 MiB` (uploads `<= 2 MiB`) / `8 MiB` (`> 2 MiB`) em `vitaCh5CliffAttackOrWind` e `10 MiB` nos demais penhascos.
    - Foram adicionados e fixados (`vitaPinSpritePagesByName`) todos os sprites com grafia mista de ataque/inimigo/chão/equipe em `visualCore`: `spr_kawkaw_tongue`, `spr_kawkaw_sparkle`, `spr_kawkaw_hurt`, `spr_kawkaw_hurt2`, `spr_kawakaw_happy`, `spr_featherbullet`, `spr_kawkaw_feather`, `spr_dw_cliffs_tallgrass_lightbake`, `spr_dw_cliffs_tallgrass_lightbake2`, `spr_krisd_dark`, `spr_susie_walk_down_dw` e `spr_ralsei_walk_down_dw`.
- **Por quê**:
  - Nos logs `butterlog7-room_dw_cliff_kawkawdash_20261107-191749.log` e `butterlog9-room_town_north_20261107-193025.log`:
    1. O objeto colocado na sala era `obj_parallax_cliffs_moving_layers_back` (filho de `obj_parallax_cliffs_moving_layers`), que não batia no `strcmp` exato e continuava rodando o Draw GML completo de 6 camadas (`topDraw=obj_parallax_cliffs_moving_layers_back:0/0(9984us)`).
    2. Quando múltiplos Krákrá atacavam juntos (ataque `type == 148`, `obj_kawkaw_shooter` + `obj_bullet_kawkaw_sparkle`), os objetos usavam `spr_kawkaw_tongue` (com `w`), `spr_kawkaw_sparkle`, `spr_kawkaw_hurt2` e `spr_kawakaw_happy`, enquanto `visualCore` listava apenas a grafia `spr_kakaw_tongue` / `spr_kakaw_hurt2` (sem `w`).
    3. As 13 surfaces RGBA8 consumiam `19,5 MiB` de VRAM e a reserva de `12 MiB` barrava uploads com `11 MiB` livres (`preflight_deferred_oom`), fazendo o chão, personagens e ataques sumirem.
- **Pra quê**:
  - Garantir que o fundo dos penhascos sempre rode no fast-path em C, liberar `~10 MiB` de VRAM nas surfaces e manter visíveis todos os ataques (individuais ou múltiplos), personagens e chão em `room_dw_cliff_kawkawdash` e `room_dw_cliff_twirlflowerwind`.

### 4. Glifos do PS4 na batalha contra o Krákrá (`obj_gamecontroller.gamepad_active`)
- **O que foi feito**:
  - Em `src/vita-probe/source/playable_main.c`, ao sincronizar as instâncias ativas, `obj_gamecontroller` tem `gamepad_active = true`, `gamepad_id = 0` e `gamepad_type = "Sony DualShock 4"` (além de `global.gamepad_type`) garantidos no estado ativo.
- **Por quê**:
  - O texto de ação do Krákrá (`gml_Object_obj_kawkaw_enemy_Step_0.gml`) chama `scr_get_input_name(4)` e `scr_get_input_name(6)`, que verificam `if (global.is_console || obj_gamecontroller.gamepad_active)`. Em `obj_gamecontroller_Create_0`, `gamepad_active` iniciava em `0` e `Step_1` testava `if (gamepad_is_connected(i) && i != gamepad_id)`, condição sempre falsa para o slot `0` quando `gamepad_id == 0`, fazendo `scr_get_input_name` cair no fallback de teclado do PC (`[Z]`, `[X]`, `[C]`).
- **Pra quê**:
  - Exibir sempre os glifos de botões do PlayStation (Cruz / Quadrado / Triângulo) nos prompts de batalha e tutoriais em vez de letras do teclado do PC.

### 5. Bordas do Chapter 5 (`room_town_mid` à noite e `room_flowershop_1f` / `2f` sem borda)
- **O que foi feito**:
  - Em `src/vita-probe/source/vita_border_rules.c` (`VitaBorderRules_shouldBlankSurround`), `room_flowershop_1f` e `room_flowershop_2f` passaram a retornar `1` (`room_blank_surround`).
  - Em `src/vita-probe/source/vita_borders.c` (`roomBorder`), no Chapter 5, quando `getCustomBorder(room)` retorna a borda genérica diurna `border_lw_town_0.png` para uma sala de Hometown (`room_town_*`, etc.), essa entrada estática é ignorada (`custom = NULL`) para que `VitaBorderRules_resolve` aplique a variante correta do horário (`morning` / `sunset` / `night`).
- **Por quê**:
  - No log `butterlog9-room_town_north_20261107-193025.log`, `room_town_mid` possuía uma entrada estática `room_town_mid=border_lw_town_0.png` em `borders_config.txt` (`source=custom`), que sobrescrevia a resolução dinâmica noturna (`border_lw_town_night_0.png`) usada por `room_town_north` e `room_town_south`. Já `room_flowershop_1f` e `2f` não estavam na lista de interiores sem borda de Hometown.
- **Pra quê**:
  - Exibir a borda noturna correta em `room_town_mid` à noite e manter a floricultura (`room_flowershop_1f` e `2f`) sem borda, igual ao comportamento original.

### 6. Melhorias de Debug no Room Navigator (Número + Nome da Sala e Glifos na Janela Maior)
- **O que foi feito**:
  - Em `src/vita-probe/source/vita_settings.c` (`VitaSettings_drawDevNavigatorHint`, `VitaSettings_drawDevQuickScenes`) e `src/vita-probe/source/playable_main.c`:
    - O painel compacto do Room Navigator agora exibe `<índice> - <nome_da_sala>` (ex.: `232 - room_dw_teste`).
    - Na janela maior do Room Navigator (`L + Triângulo`), a linha abaixo do título exibe `ROOM XX / XXX - [Glifo L] / [Glifo R] PAGE` com os glifos reais `button_psv_L` e `button_psv_R`, e o aviso de confirmação direta exibe `DIRECT ROOM: MAY NEED STORY FLAGS - [Glifo X] AGAIN` com o glifo `button_ps4_cross_0`.
- **Por quê**:
  - Facilitar a identificação imediata do índice numérico e nome da sala durante os testes de capítulos e substituir as abreviações em texto puro (`L/R: PAGE` e `X AGAIN`) pelos glifos visuais do PlayStation abaixo do título.
- **Pra quê**:
  - Tornar a navegação e o relato de bugs por número/nome de sala mais rápidos e legíveis no PS Vita.

---

## `v0.81-7` — Recorte diagonal da transição de pétalas (`shd_petalcut` + cortina `pt_petal_whirl`) e persistência do `CRT Agressivo`

### 1. Recorte diagonal de `surf_view1` (`shd_petalcut`) em `room_dw_garden_floradinnencounter`
- **O que foi feito**:
  - Em `src/butterscotch/src/gl_legacy/gl_legacy_renderer.c` (`glGpuSetShader` e `glLegacyDrawSurface`), `shd_petalcut` (shader `#12`) passou a ser tratado como adaptado (`EXPERIMENTAL_SHADER=adapted index=12 name=shd_petalcut reason=surface_diagonal_clip`), mantendo `renderer->currentShader = -1` durante o desenho da `view 1` e aplicando o recorte oblíquo diretamente ao compor `surf_view1` sobre a `view 0` em faixas horizontais alinhadas de `4px` ao longo da reta `lineTop = (690 - transition_percent * 640, 0)` até `lineBottom = (1130 - transition_percent * 640, 480)`.
- **Por quê**:
  - No log `butterlog1-room_dw_cliff_kawkawdash_20261107-123708.log` (linha 908), o Vita registrava `EXPERIMENTAL_SHADER=fallback index=12 name=shd_petalcut reason=unsupported`. Sem `shd_petalcut`, `surf_view1` (que contém o cenário escuro com árvores vermelhas e grama roxa-escura) era desenhada 100% opaca por cima da `view 0` (árvores e grama rosa) durante todo o `transition_stage == 1`, mudando de cor abruptamente apenas no final quando `surf_view1` era destruída. Além disso, deixar `currentShader = 12` ativo durante toda a `view 1` desativava o frustum culling (`vitaClassifyCameraQuad`) e o batching de quads (`queueQuad`).
- **Pra quê**:
  - Reproduzir fielmente o efeito do PC onde a cena escura/vermelha à esquerda é varrida em diagonal para revelar o jardim rosa à direita conforme as pétalas passam, mantendo o culling de câmera e o batching ativos durante a `view 1`.

### 2. Cortina diagonal de pétalas (`pt_petal_whirl`) em `draw_ch5_sakura_host`
- **O que foi feito**:
  - Em `src/vita-probe/source/playable_main.c` (`draw_ch5_sakura_host`), além das pétalas de ambiente (`ambientCount`), o host agora lê `transition_percent` de `obj_sakurafubuki_new` e desenha a faixa diagonal densa (`whirlCount = 96` pétalas de `spr_spin_petal_particlefps`) acompanhando exatamente a fronteira `edgeX(y) = topX + (botX - topX) * (y / 480.0f)`, restrita ao viewport 4:3 (`gx` entre `8` e `632`).
- **Por quê**:
  - O Butterscotch não implementa a API legada `part_*` do GameMaker. Nas builds `v0.81-3` a `v0.81-6`, `draw_ch5_sakura_host` apenas espalhava 52 pétalas uniformemente pela tela inteira, sem formar o redemoinho/cortina diagonal (`ps2` / `pt_petal_whirl`) que acompanha a linha de transição das árvores vista no PC.
- **Pra quê**:
  - Exibir a cortina diagonal densa de pétalas rosas varrendo a tela da direita para a esquerda exatamente sobre a linha de troca de cor das árvores e da grama, sem vazar sobre a borda lateral do console.

### 3. Consistência da chave `screen_filter=crt-aggressive` entre VitaGL e Native GXM
- **O que foi feito**:
  - Em `src/vita-probe/source/vita_settings.c` (`VitaSettings_save` e `VitaSettings_load`), o modo `6` (`CRT Agressivo`) agora grava `screen_filter=crt-aggressive` e aceita tanto `screen_filter=crt-aggressive` quanto `screen_filter=crt-advanced` ao carregar, igualando o contrato já usado em `experiments/native-gxm/vita_settings_native.c`.
- **Por quê**:
  - A `v0.81-6` havia renomeado o rótulo para `CRT Agressivo` e a chave no GXM para `crt-aggressive`, mas `vita_settings.c` ainda salvava/lia apenas `crt-advanced`.
- **Pra quê**:
  - Garantir que a escolha do filtro `CRT Agressivo` persista em `config.ini` ao reiniciar o jogo ou alternar entre VitaGL e Native GXM.

---

## `v0.81-4`, `v0.81-5` e `v0.81-6` — Correções de FPS/Texturas nos Penhascos, Barra de TP, Bordas Animadas no Cap. 5 e Perfis CRT (`Suave`, `Médio`, `Agressivo`)

> **Nota sobre a sequência de builds**:
> - `v0.81-4`: compilação intermediária durante a validação da suíte do Chapter 5.
> - `v0.81-5`: build completa com as correções de penhascos, barra de TP, bordas animadas sob pressão de memória e os três novos perfis CRT (ainda com o nome `"CRT Avançado"` no terceiro nível).
> - `v0.81-6`: renomeação do terceiro nível de filtro de `"CRT Avançado"` para `"CRT Agressivo"` em VitaGL e Native GXM conforme solicitado.

### 1. Ativação real do fast-path em C das camadas do penhasco (`obj_parallax_cliffs_moving_layers`)
- **O que foi feito**:
  - Em `src/butterscotch/src/runner.c` (`Runner_tryNativeCliffLayersDraw`), foi removida a exigência da variável `__transition_yscale` (`transitionId >= 0`) e a varredura de `obj_platswap`, além de registrar `CH5_CLIFF_NATIVE=enabled` no log.
- **Por quê**:
  - Nos logs `butterlog1-room_dw_cliff_kawkawdash_20261107-123708.log` e `butterlog2-room_dw_cliff_twirlflowerwind_20261107-123800.log`, `obj_parallax_cliffs_moving_layers` continuava caindo no interpretador GML (`draw_max_us` acima de `580 ms` na entrada e `~1.83 ms/frame` contínuos) porque salas sem `obj_platswap` não inicializavam `__transition_yscale`, embora o perfil Simple multiplicasse `platformScale` por `0.0`.
- **Pra quê**:
  - Eliminar o gargalo de Draw do fundo de penhasco em `room_dw_cliff_kawkawdash` e `room_dw_cliff_twirlflowerwind`, estabilizando o FPS na exploração.

### 2. Fixação (Pinning) em VRAM das páginas de ataque (`kawkaw`) e flores de vento (`twirlflower`)
- **O que foi feito**:
  - Em `src/butterscotch/src/gl_legacy/gl_legacy_renderer.c` (`GLLegacyRenderer_prepareRoomTextureSet`), os grupos `visualCore` (`spr_kakaw_idle`, `spr_kakaw_tongue`, `spr_kakaw_dig`, `spr_kakaw_eye`, `spr_kawkaw_small`, `spr_kakaw_bobhead`, `spr_kakaw_hurt2`, `spr_cliff_crater`) e `windCore` (`spr_plat_twirlflower_vine`, `spr_plat_twirlflower_petals`) passaram a usar `vitaPinSpritePagesByName` em vez de apenas marcação preferencial.
- **Por quê**:
  - Em `butterlog1` (`room_dw_cliff_kawkawdash`) e `butterlog2` (`room_dw_cliff_twirlflowerwind`), o orçamento de 40–50 MiB de atlas sofria evicções LRU constantes (`evict_lru` e `preflight_deferred_*` nas páginas `69, 91, 127, 130, 147, 152, 154`), descartando as páginas dos projéteis/língua do inimigo (deixando o ataque invisível) e das pétalas/cipós de vento (fazendo texturas sumirem e causando stutters de recarga).
- **Pra quê**:
  - Impedir que ataques inimigos e elementos críticos de gameplay desapareçam ou causem travadas de decodificação de textura nos penhascos, sem aumentar o teto global de VRAM do Chapter 5.

### 3. Flush de lote antes de troca de Blend Mode (Correção da borda dupla de TP em batalha)
- **O que foi feito**:
  - Em `src/butterscotch/src/gl_legacy/gl_legacy_renderer.c` (`glGpuSetBlendMode` e `glGpuSetBlendModeExt`), foi adicionado `vitaQuadBatchFlush()` antes de alterar `gl->currentBlendMode` e o estado de blend do OpenGL.
- **Por quê**:
  - O objeto `obj_tensionbar` desenha a barra e a máscara/recorte de TP alternando modos de blend (`bm_subtract` / `bm_normal`). Como os quads ficavam retidos em `vitaQuadBatch` durante a troca de `gpu_set_blendmode`, os sprites enfileirados antes da troca eram descarregados depois já com o novo blend mode, desenhando uma segunda moldura de TP sólida por cima do texto `TP %`.
- **Pra quê**:
  - Garantir que cada operação de máscara/recorte da barra de TP seja enviada à GPU com o modo de blend exato em que foi emitida, eliminando a borda duplicada em cima de `TP %`.

### 4. Preservação das Bordas Animadas (`frames_side.bani`) sob pressão de memória no Chapter 5
- **O que foi feito**:
  - Em `src/vita-probe/source/vita_animated_borders.inc` (`animatedBordersGetTexture`), quando `borderMemoryPressure` está ativo, o sistema agora força `wantSide = 1` (usando `frames_side.bani`, que aloca apenas as faixas laterais visíveis de `120px` em 3 slots RGB565 de `255 KiB`) em vez de abortar/desligar a borda animada.
- **Por quê**:
  - No Chapter 5, o limite `borderPressureMiB` é `44 MiB`, valor atingido quase permanentemente durante o gameplay (`residentTextureBytes ~ 40–50 MiB`). A regra anterior desativava completamente as bordas animadas sempre que `borderMemoryPressure` era verdadeiro.
- **Pra quê**:
  - Permitir que as bordas animadas funcionem normalmente durante todo o Chapter 5 consumindo menos de `768 KiB` de memória total de textura.

### 5. Reestruturação dos Filtros CRT (`CRT Suave`, `CRT Médio` e `CRT Agressivo`)
- **O que foi feito**:
  - Em `src/vita-probe/source/vita_settings.c`, `experiments/native-gxm/vita_settings_native.c`, `src/vita-probe/source/playable_main.c`, `src/vita-probe/source/vita_crt_mod_gl.inc` e `experiments/native-gxm/main.c`:
    - `"CRT Mod (Experimental)"` foi renomeado para **`"CRT Suave"`** (`screenFilterMode = 1`, `u_strength = 1.0f`).
    - Foi criado o **`"CRT Médio"`** (`screenFilterMode = 5`, `u_strength = 1.15f`, aumentando em `+15%` a intensidade da máscara de fósforo, scanlines e vinheta do shader zero-readback).
    - Foi criado o **`"CRT Agressivo"`** (`screenFilterMode = 6`), que combina o passo do shader CRT em `+15%` (`u_strength = 1.15f`) com a sobreposição de scanlines de TV, vinheta de borda de tubo, reflexo de vidro superior e cantos curvos de tubo CRT.
    - Os modos antigos separados **`"CRT Tube"`** e **`"Scanlines"`** foram removidos da lista do seletor (migrando configurações legadas automaticamente para os novos perfis).
- **Por quê**:
  - Atender à reorganização visual solicitada pelo usuário para unificar a família CRT em três níveis progressivos claros (`Suave`, `Médio` e `Agressivo`) sem manter opções antigas redundantes na lista.
- **Pra quê**:
  - Oferecer controle graduado do efeito CRT (original, `+15%`, e tubo completo com scanlines/curvatura de canto) mantendo custo de GPU de passe único sem readback de framebuffer.

---

## `v0.81-2` e `v0.81-3` — Candidatos em C de Desempenho para o Chapter 5

> **Nota sobre a sequência de builds**:
> - `v0.81-2`: primeira compilação dos 4 atalhos nativos em C para objetos quentes de CPU/Draw do Chapter 5 (`docs/candidatos-c-caps4-5-v0.80-59.md`).
> - `v0.81-3`: ajuste da assinatura de `VMBuiltins_tryGrassStripDraw` / `VMBuiltins_tryBluePlatformDraw` e validação de contrato (`scripts/test-v081-ch5-c-candidates.py`), usada nos testes em hardware que geraram os logs de penhasco e jardim.

### 1. Cache de superfície das faixas de grama (`obj_plat_vertical_floor_grass_strip`)
- **O que foi feito**:
  - Em `src/butterscotch/src/vm_builtins.c` (`VMBuiltins_tryGrassStripDraw`) e `src/butterscotch/src/runner.c`, o `Draw_0` de `obj_plat_vertical_floor_grass_strip` foi portado para C: uma vez construída a surface estática da faixa de grama (`grass_surf`), ela é marcada como pronta (`__vita_grass_ready`) e desenhada diretamente com `draw_surface` sem reconstruir tile por tile a cada quadro.
- **Por quê**:
  - Nos logs da linha `v0.80-59`, `obj_plat_vertical_floor_grass_strip_Draw_0` reconstruía a surface inteira em GML toda vez que entrava na câmera, custando `1.62 ms` a `5.04 ms` por frame (`~5600` a `18500` ops GML).
- **Pra quê**:
  - Reduzir em mais de 90% o tempo de CPU gasto desenhando faixas verticais de grama nas salas de escalada do Chapter 5.

### 2. Desenho nativo em C das camadas móveis de penhasco (`obj_parallax_cliffs_moving_layers`)
- **O que foi feito**:
  - Em `src/butterscotch/src/runner.c` (`Runner_tryNativeCliffLayersDraw`), o evento `Draw_0` de `obj_parallax_cliffs_moving_layers` ganhou implementação nativa em C para o perfil Simple do Vita, disparando o `event_user(0)` de `obj_parallax_cliffs_sunlayer` e desenhando as camadas 2 e 3 diretamente pelo renderer.
- **Por quê**:
  - O script GML interpretado iterava matrizes e structs de camadas em todo quadro nas salas de penhasco.
- **Pra quê**:
  - Diminuir o custo de CPU do parallax dos penhascos mantendo a iluminação solar e o posicionamento idênticos ao GML.

### 3. Culling temporário de portas deslizantes fora da câmera (`obj_plat_fusuma_mask`)
- **O que foi feito**:
  - Em `src/butterscotch/src/runner.c` (`Runner_prepareFusumaCull` e `Runner_restoreFusumaCull`), instâncias de `obj_plat_fusuma_mask` totalmente fora da visão da câmera têm `is_visible = 0` durante a fase de Draw da view e restaurado imediatamente ao final da view.
- **Por quê**:
  - Em `room_dw_fcastle_foyer` e salas do castelo, dezenas de portas `obj_plat_fusuma_mask` fora da tela executavam loops de `draw_sprite_part_ext` em GML (`~2.08 ms` e `~3398 ops` por frame).
- **Pra quê**:
  - Pular o trabalho de Draw das portas fora da câmera sem alterar `is_visible` durante o `Step` (preservando 100% da colisão e lógica de jogo).

### 4. Desenho nativo em C de plataformas azuis (`obj_blue_platform`)
- **O que foi feito**:
  - Em `src/butterscotch/src/vm_builtins.c` (`VMBuiltins_tryBluePlatformDraw`), o evento `Draw_0` de `obj_blue_platform` (incluindo o tremor `shake_Range`, fatias de borda de 4px e flor central `spr_plat_blueflower`) foi movido para C, mantendo o `Step_0` (física e colisão) intacto no GML.
- **Por quê**:
  - Nas salas com múltiplas plataformas azuis (`room_dw_cliff_twirlflowerwind`, `room_dw_cliff_blueplat1`), `obj_blue_platform_Draw_0` somava centenas de chamadas `draw_sprite_part_ext` interpretadas por quadro.
- **Pra quê**:
  - Aliviar o custo de CPU por frame nas salas de vento e plataformas do Chapter 5 sem qualquer risco à física ou colisão.

---

## `v0.81-1` — Abertura da Linha `v0.81` e Integração do Pacote de Bordas Animadas

- **O que foi feito**:
  - Atualização da versão-base em `config/version.txt` e `data/assets/build_version.txt` para `0.81`.
  - Integração e empacotamento do conjunto de bordas animadas `.bani` (`Deltarune-Animated-Borders-v0.81.zip`) com suporte a streaming de quadros RGB565 (`frames.bani` e `frames_side.bani`) em `src/vita-probe/source/vita_animated_borders.inc`.
- **Por quê**:
  - Iniciar o ciclo `v0.81` sobre a base consolidada da `v0.80-59`, separando os novos recursos visuais (bordas animadas, perfis CRT e otimizações em C do Chapter 5) em uma nova versão principal.
- **Pra quê**:
  - Fornecer a base `v0.81` com numeração automática de VPK e suporte nativo às bordas animadas por capítulo.

