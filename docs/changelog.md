# Changelog

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
