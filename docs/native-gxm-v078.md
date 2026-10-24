# v0.78 — experimento Native GXM

## Objetivo

Avaliar um renderer para o Vita que envie comandos diretamente ao `sceGxm`,
sem a tradução de estado e o gerenciamento de recursos do VitaGL. A hipótese é
que um backend 2D explícito possa eliminar classes de GPU crash observadas em
trocas de sala e reduzir CPU/VRAM em cenas com muitas surfaces.

## Primeira etapa

O VPK `Deltarune-NativeGXM-v0.78-gxm1.vpk` é um laboratório separado do jogo.
Ele valida no hardware:

- inicialização e encerramento do GXM;
- contexto, render target e três buffers de apresentação;
- programas GXP incorporados, sem compilação em runtime;
- primitivas 2D, alpha blending, stencil e lotes pequenos;
- fila de apresentação e sincronização contínua.

O programa não liga VitaGL, vitashark ou `libshacccg`. START encerra o teste e
grava `ux0:data/deltarune/deltarunevita/native-gxm-probe.log`.

Artefato compilado: `artifacts/experiments/v0.78-native-gxm/` com 181.851
bytes, duas entradas, CRC íntegro e SHA-256
`0e66fc6b4084a7a38d0969781ad71cdd9dbab65fb4c5afbe6816755615f90ee0`.

## O que ainda separa o laboratório do jogo

O `RendererVtable` do Butterscotch tem mais de sessenta operações. Para iniciar
o Chapter 0 com GXM nativo ainda são necessários: atlas e subimagens, matrizes,
blend/scissor, fontes, render targets, cópia e leitura de surfaces, sprites
dinâmicos, cache/eviction e equivalentes dos shaders usados pelo jogo.

O plano de integração é manter o backend isolado e avançar nesta ordem:

1. quads texturizados e atlas do `data.win`;
2. implementação mínima do `RendererVtable` para o Chapter 0;
3. surfaces e composição de views;
4. shaders GXP previamente compilados e fallbacks equivalentes;
5. Chapters 1–5, medindo VRAM e capturando Razor em hardware compatível.

## Referências e licenças

- `psp2-tooling` é usado como referência de captura Razor e análise, não como
  renderer nem dependência do VPK (GPL-3.0).
- O runtime do MH3 demonstra a arquitetura de um renderer GXM e shaders
  compilados previamente; nenhum código GPL foi copiado.
- A primeira prova usa `nanovg-gxm` fixado no commit
  `ffbe2a0d4938bd5d90bba424d9f28b57bcb49cd5`, sob licença zlib.
- `libvita2d` e os samples VitaSDK servem como referências adicionais para
  alocação, surfaces e apresentação.

## Segunda etapa: v0.78-gxm2 — atlas texturizado (12/10/2026)

Artefato: `artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm2.vpk`.
Title ID: `DLTAGXM02`, separado de gxm1 (`DLTAGXM01`) e do jogo.

Este teste gera em RAM um atlas RGBA8888 de 256x128 pixels (4 colunas,
2 linhas), carrega-o por `nvgCreateImageRGBA` e o desenha com GXP embutido
do nanovg-gxm. Verifica oito subimagens do mesmo atlas, nearest sampling,
transparencia, recorte de 32x32, escala, rotacao, animacao e scissor.

**X** alterna a cena de atlas e a cena geometrica do gxm1; **START** encerra.
O log permanece em `ux0:data/deltarune/deltarunevita/native-gxm-probe.log`.
Periodicamente, `NATIVE_GXM=fps scene=atlas ... fps_x100=...` fornece FPS
em centesimos: 5990 = 59,90 FPS. Para comparar as cenas, deixar cada
uma exibida por pelo menos 10 segundos e preservar todo o log.

O gxm2 compilou em Docker/VitaSDK e passou pelo teste estatico e ZIP.
**Ainda requer teste no Vita fisico**; os 748 quadros de log sem crash
correspondem ao gxm1, nao ao gxm2.

### Arquitetura e limite do gxm2

O gxm2 e um teste de upload e sampling em sceGxm atraves do adaptador
NanoVG. **Nao e ainda o backend RendererVtable**, nao processa `data.win`
e nao possui views, surfaces e shaders especificos de Deltarune.
Ele nao liga VitaGL nem usa compilacao de shaders em runtime.

Proxima etapa: usar `TexturePageItem` com `sourceX/Y/Width/Height` e
`targetX/Y/Width/Height` (que podem diferir em sprites recortados),
importar atlas reais do jogo, depois adicionar render targets e
um backend opcional no runner; manter Modern-GL como fallback.

#### Contrato TPAG implementado no gxm2

`experiments/native-gxm/tpag_sprite.{h,c}` define uma primitiva
`native_gxm_draw_tpag` com `sourceX/Y/Width/Height`,
`targetX/Y/Width/Height`, origem, escala, rotacao GameMaker e alpha.
O laboratorio exercita inclusive uma regiao 32x32 desenhada como
64x48, com deslocamento `targetX/Y` e origem nao zero.
Esse helper ainda desenha por NanoVG; precisa receber atlas reais
e ser migrado para um pipeline dedicado de quads sceGxm.

## Terceira etapa: v0.78-gxm3 — sprites reais do Chapter 0 (12/10/2026)

Novo VPK: `artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm3.vpk`.
Title ID separado: `DLTAGXM03`. Builds gxm1 e gxm2 preservadas.

O log do teste gxm2 no Vita registrou 300 frames em 5.005013 segundos
na cena de atlas, equivalente a 59,93 FPS. Nao apareceu encerramento
do gxm2 nem janela FPS da cena geometrica no arquivo recebido.

Novo teste: `real_tpag_probe.c` le no Vita os TPAG #6, #11 e #20
do `chapter0/data.win` e recorta so as linhas correspondentes
do `chapter0/texture-cache/page_001.r444`. A conversao RGBA4444 para
RGBA8888 ocorre na RAM; o upload e pelo nanovg-gxm/GXP embutido.
Assim, evita carregar o atlas de 2048x2048 inteiro e preserva a
instalacao jogavel. Nenhum recurso original e embutido no VPK.

TPAG verificadas no preparado local:
- TPAG 6: fonte x1546,y1030, 256x256, 9956 texels com alpha.
- TPAG 11: fonte x1938,y1030, 26x23, 598 texels com alpha.
- TPAG 20: fonte x1678,y1449, 10x9, targetX/Y 1,1, bounding 12x11, 38 texels com alpha.

Controles: TRIANGLE alterna sprite real/atlas sintetico;
X alterna geometria/atlas sintetico; START encerra e grava o log.
Com os dados instalados, esperar `real_tpag status=ready code=3 loaded=3`.
Para medir FPS, deixar a cena real por pelo menos 5 segundos.
Se faltar data.win ou cache R444, a cena nao e habilitada e o log
registra `real_tpag status=unavailable` com codigo de erro.

O gxm3 ainda usa NanoVG como adaptador sceGxm, nao e o renderer
de producao. Falta implementar quads GXM proprios, render targets,
surfaces, cameras, fontes, matriz e o RendererVtable do Butterscotch.
Preservar Modern-GL como fallback ate paridade comprovada.

## Quarta etapa: v0.78-gxm4 — Butterscotch parser no processo GXM

Status: compilado com VitaSDK/Docker. Requer validacao no Vita fisico.
O log de gxm3 confirmou `real_tpag status=ready code=3 loaded=3`,
59.94 FPS por 5 segundos na cena `real_tpag`, e shutdown apos 1206 frames.

Novo binario `DLTAGXM04` inclui diretamente o codigo original do
parser do Butterscotch (`data_win.c`, `binary_reader.c`,
`image_decoder.c`, bzip2 e STB DS). O parser le o Chapter 0 em
`ux0:data/deltarune/deltarunevita/chapter0/data.win`, usando as
mesmas opcoes de lazy load para salas, texturas e audio do
`playable_main.c`. Depois gera um resumo de salas, sprites, TPAG,
bytecode e texturas no log. A liberacao e feita com `DataWin_free`.

Marcadores esperados:
`NATIVE_GXM=butterscotch parse_begin chapter=0`
`NATIVE_GXM=butterscotch status=datawin_ready code=0 rooms=... sprites=... tpag=... code_entries=... textures=...`

Em caso de erro, `status=parse_failed` registra codigo negativo;
o laboratorio continua no modo grafico, sem executar bytecode.
Apos iniciar, usar TRIANGLE para a cena de texturas reais,
X para geometria/atlas, START para sair.

Este marco comprova a coexistencia do parser original do Butterscotch
com GXM no mesmo eboot, sem VitaGL e sem compilar shaders em runtime.
**Ainda NAO executa VM_create, Runner_create ou Runner_step**:
nao deve ser anunciado como Deltarune jogavel no backend GXM.

Proximas etapas dependentes da validacao no Vita:
1. Inicializar VMContext e criar a camada de filesystem/audio sem VitaGL;
2. Implementar o `RendererVtable` nativo (mais de 60 operacoes),
   priorizando primeiro quads/atlas, fontes, blend e projecao;
3. Render targets / surfaces, copies, GUI, camera e tela inicial;
4. Executar Runner_initFirstRoom / Runner_step em Chapter 0;
5. Paridade nos Chapters 1-5, stress de memoria e sync.

Compilacao GXM4 isolada de `src/vita-probe`, preservando
o backend Modern-GL e o Title ID da versao jogavel.

## Quinta etapa: v0.78-gxm5 — VM original do Butterscotch

O teste gxm4 foi confirmado no Vita fisico: `DataWin_parse` do Chapter 0
em 247 ms, com 3 salas, 8 sprites, 28 TPAG, 283 entradas CODE, 3 TXTR;
encerrou com `shutdown frames=986 result=pass`.

Build gxm5: `artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm5.vpk`
Title ID independente: `DLTAGXM05`. Preservadas as builds anteriores e
o jogo `DELT00001` com VitaGL/Modern-GL.

Foi integrado ao mesmo binario nativo GXM o codigo original da VM:
`vm.c`, `vm_builtins.c`, `runner.c` e dependencias auxiliares do
Butterscotch. Bibliotecas STB continuam com uma implementacao de cada.
O build usa `ENABLE_WAD17` e elimina secoes nao referenciadas.

Inicializacao em etapas separadas:
1. `DataWin_parse()` com lazy loading do Chapter 0;
2. log `NATIVE_GXM=butterscotch status=datawin_ready ...`;
3. log `NATIVE_GXM=butterscotch vm_begin`;
4. `VM_create(DataWin*)` completo, incluindo o registro dos builtins;
5. log `NATIVE_GXM=butterscotch vm_status=vm_ready code=0 elapsed_ms=... funcs=... builtins=1`;
6. apos START, `VM_free()` antes de `DataWin_free()`.

Os simbolos `VM_create`, `VM_free`, `VMBuiltins_registerAll` e
`DataWin_parse` foram confirmados no ELF ARM do gxm5. VPK compilou
no VitaSDK/Docker, passou nos contratos e CRC de ZIP.
**Ainda falta validar esta etapa no Vita fisico.**

Se gxm5 falhar, enviar o log e o psp2dmp gerado. Os marcadores de
parse e VM indicam em qual fase parou. Se `vm_status=vm_failed`,
o laboratorio continua apenas com a parte grafica e registra
`graphics_only_vm_failed` no shutdown.

Limite: gxm5 **nao executa** `Runner_create`, `Runner_initFirstRoom` ou
`Runner_step`, e ainda usa NanoVG como camada de comandos GXM.
Para o Chapter 0 jogavel, proxima etapa e conectar AudioSystem/FileSystem
e implementar `RendererVtable` GXM; nao usar placeholders silenciosos
para surfaces, texto e render targets que poderiam mascarar bugs.

## Sexta etapa: v0.78-gxm6 — Runner_create (somente construcao)

O log de gxm5 recebido do Vita fisico confirma `vm_status=vm_ready`
em 6 ms, `funcs=332 builtins=1`, e `shutdown frames=831 result=pass`.

O gxm6 foi compilado com o `Runner_create()` e as estruturas originais
de eventos, objetos, salas e instancia do Butterscotch. Ele utiliza
`NoopFileSystem_create` e `NoopAudioSystem_create` **apenas no laboratorio**
para testar a montagem estrutural do Runner: nenhum deles deve substituir
os sistemas de arquivo e audio da versao jogavel.

Implementado `BSContractRenderer_create()` com todos os 73 callbacks
tipados conforme `renderer.h`, gerados automaticamente por
`scripts/generate-native-gxm-contract.py`. `init` liga o DataWin e
`destroy` libera a estrutura. TODOS os demais callbacks falham
explicitamente, escrevendo `NATIVE_GXM=runner_renderer unsupported=...`
e interrompendo a execucao caso sejam chamados. Nao sao operacoes GXM
de renderizacao, e nao mascaram superficies/shaders/fontes ausentes.

Marcadores para verificar gxm6 no Vita:
`NATIVE_GXM=butterscotch runner_begin stage=construct_only`
`NATIVE_GXM=butterscotch runner_status=constructed code=0 elapsed_ms=... room_index=-1`
`NATIVE_GXM=shutdown frames=... result=pass`

Title ID `DLTAGXM06`, VPK em
`artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm6.vpk`.
Build e testes de contrato aprovados; TESTE FISICO PENDENTE.

Esta milestone valida somente Runner_create/Runner_free e lifecycle.
Ela nao executa `Runner_initFirstRoom`, `Runner_step` nem desenha
qualquer objeto de jogo via RendererVtable. Proxima etapa deve
substituir os callbacks com implementacoes reais usando sceGxm,
iniciando por gerenciador de texturas/TPAG, quads e projecao,
depois superfices, fontes e compatibilidade com shaders GXP.
Manter Modern-GL do Deltarune jogavel preservado.

## Setima etapa: v0.78-gxm7 — inicio de renderer real e Chapter 0 opt-in

Resultado recebido do Vita gxm6: parse em 248 ms, VM em 6 ms,
`Runner_create` em 1 ms, `shutdown frames=518 result=pass`.

O gxm7, construido e linkado com VitaSDK, evolui as 73 assinaturas do
`RendererVtable` para implementacoes nativas sceGxm via NanoVG-GXM
(sem vitaGL, sem libshacccg). A geracao do vtable usa
`scripts/generate-native-gxm-contract.py` e a implementacao reside em
`experiments/native-gxm/bs_renderer_impl.inc`.

Implementacoes relevantes: leitura lazy de cada pagina TXTR do proprio
`data.win` pelo ImageDecoder, crop de todas as entradas TPAG e upload RGBA;
sprites comuns/partes/tint, formas geometricas, fontes de bitmap GameMaker,
blend normal/add/com fatores, handle de texturas, superficies reais de
offscreen (sceGxm via gxmCreateFramebuffer + compartilhamento de textura),
copia e leitura linear de surface, tiled sprite/parts e drawTile BGND.
Na cena GXM, ambos os sistemas usam o mesmo contexto de NanoVG.

O fluxo do Runner utiliza OverlayFileSystem real para chapter0, mas
o save path de teste fica ISOLADO em
`ux0:data/deltarune_native_gxm_saves/` para proteger saves da versao
jogavel. Audio usa NoopAudioSystem e portanto NAO reproduz efeitos/musicas.
Existe mapeamento inicial de teclado e gamepad Vita.

**IMPORTANTE: o backend ainda e experimental e NAO esta completo!**
Varios casos de shaders GameMaker, gradients de quatro cantos, vertices
complexos, render de sprite font, efeitos alpha e criacao de sprites
dinamicos abortam intencionalmente com
`NATIVE_GXM=runner_renderer unsupported=<operacao>`.
Alem disso, cameras/projecao e superficies ainda carecem de validacao
visual. Compilar o vtable de 73 callbacks nao prova equivalencia de
semantica nem que todas as funcionalidades funcionem.

Controles do laboratorio:
- X: alterna atlas sintetico e geometria;
- TRIANGLE: alterna cena de 3 TPAG reais;
- SQUARE: ativa tentativas de `Runner_initFirstRoom`, Runner_step,
  Runner_drawPre, Runner_beginFrame, Runner_drawViews,
  Runner_drawPost e Runner_drawGUI no Chapter 0;
- START: encerra o laboratorio;
- com Chapter 0 ativo, direcional, face buttons e gatilhos chegam
  ao Runner via gamepad/teclado.

O log do primeiro frame identifica a ultima etapa concluida e, em caso
de operacao ausente, registra seu nome antes do aborto. Isso e para
testar progressivamente sem confundir gxm7 com uma release jogavel.
VPK independente: `artifacts/experiments/v0.78-native-gxm/`
`Deltarune-NativeGXM-v0.78-gxm7.vpk`, Title ID `DLTAGXM07`.

Proximos requisitos para concluir o port:
1. validar init das surfaces e primeiro frame no Vita fisico;
2. implementar shader GXP custom e blend/color masks, vertices, dinamicas;
3. audio OpenAL, composicao/cameras e compatibilidade do filesystem;
4. testes completos de menus/Chapter 0, demais chapters, memoria e FPS.

## Oitava etapa: v0.78-gxm8 — correcao do crash e black screen (12/10/2026)

Relatorio real do gxm7 no Vita:
- Gxm7 iniciou Room 0, completou `Runner_step`, `Runner_drawViews` e
  `Runner_drawGUI`, registrando `gameplay_first_frame=pass`.
- Primeiro teste: apos centenas de frames (60 FPS em janelas estaveis),
  o renderer abortou com
  `NATIVE_GXM=runner_renderer unsupported=createSpriteFromSurface_dynamic_tpag`.
  Dump correspondente na pasta `logs/chapter-log/geral/psp2core-1791841786-0x0000b43d41-eboot.bin.psp2dmp`.
- Segundo teste: cerca de 3093 frames em execucao normal,
  mas a composicao do Chapter 0 ficou preta. Nao houve crash nesse teste.

Causa encontrada no codigo: `bs_beginFrame()` sobrescrevia
`activeTarget=-1` e nao ligava o `applicationSurfaceId` da VM,
portanto a renderizacao mundial ia ao buffer de display, enquanto
`bs_endFrameEnd()` compunha uma application_surface vazia por cima.

Correcao no gxm8: `bs_beginFrame()` muda para a `application_surface`
por `bs_switch_target()` e preserva o alvo ativado no `Runner_drawPre`.
World -> app surface; endFrameInit -> display; endFrameEnd ->
composite/letterbox; drawGUI -> display. O ciano 12x12 no canto superior
esquerdo indica que o framebuffer de display foi apresentado.

Implementado `bs_createSpriteFromSurface` nativo com snapshot dos
pixels RGBA do surface CDRAM, sincronizacao do GXM ao terminar a cena,
restauracao do alvo e do estado de NanoVG, criacao de textura RGBA
e registro de sprite dinamico no DataWin (SPRT/TPAG).
Implementado `bs_deleteSprite`, liberando imagens e slots.
`bs_surfaceGetPixels` usa o mesmo mecanismo de sincronizacao.
Limites de crop: dimensoes positivas, ate 1024x1024, dentro da surface.
Parametros removeback/smooth seguem as limitacoes do runner GL existente.

Novas linhas: `NATIVE_GXM=render_metrics room=... app=... app_valid=...`
`app_size=... active_target=... world_draws=... total_draws=...`
`tex_loaded=... missing=... surfaces=... dynamic=... snapshots=...`
e `NATIVE_GXM=dynamic_sprite created=...`.
As metricas sao registradas nos cinco primeiros frames e a cada 300 frames.

Build `Deltarune-NativeGXM-v0.78-gxm8.vpk`, Title ID `DLTAGXM08`.
Compilacao, linkagem e teste estatico aprovados, teste NO VITA FISICO
AINDA PENDENTE. Este nao e um port completo: outros callbacks ainda
podem falhar com `unsupported`, shaders e audio estao incompletos.
Versoes anteriores preservadas; projeto Modern-GL jogavel nao alterado.

## Nona etapa: v0.78-gxm9 — audio real OpenAL/OGG (12/10/2026)

**Vita gxm8 validado por teste do usuario:** tanto o indicador ciano de
composicao quanto o inicio visual do Chapter 0 aparecem corretamente.
Em `native-gxm-probe.log`, Room 0 renderiza 67 draws por frame,
640x480 em `application_surface` valida, 25 texturas iniciais
(28 depois), `missing=0`. Dinamica `sprite=8,tpage=28` criada.
FPS caiu de cerca 55 para 35-40 em cenarios posteriores. Otimizacao
deve seguir separada do audio.

Causa do silencio: `bs_bootstrap_runner_create` usava
`NoopAudioSystem_create()`, com callbacks que nunca produzem audio.

**gxm9:** usa a implementacao real de audio do Butterscotch
`src/butterscotch/src/audio/openal/al_audio_system.c` e `wave.c`,
compilando somente esses fontes com `PLATFORM_VITA`. Inclui headers
OpenAL/STB Vorbis e linka `openal`, `pthread`, `SceAudio_stub`,
`SceAudioIn_stub`, `SceAppUtil_stub`. A renderizacao continua GXM puro,
sem vitaGL, vitashark ou libshacccg.

AudioSystem real agora e entregue ao `Runner_create`, permitindo
`audio_play_sound`, `audio_create_stream`, controle de gain/fade,
streaming OGG externo e cache de efeitos WAV. No loop de gameplay,
`audio->vtable->update(audio,deltaTimeSeconds)` e chamado logo depois
de `Runner_step` para realimentar filas OpenAL e efeitos de fade.
Como no runner jogavel existente, deltaTime e convertido de microssegundos
para segundos.

Diagnosticos: `NATIVE_GXM=audio_metrics` registra status do dispositivo
e contexto OpenAL, fontes ativas/tocando, musica, efeitos, streams
e buffers enfileirados. `NATIVE_GXM=audio_stream` registra caminhos
externos resolvidos. `g_vitaProbeLoggingEnabled=1` habilita logs
detalhados da implementacao original em
`ux0:data/deltarune/deltarunevita/butterscotch-probe.log`.

**Validacao:** compilacao/link CMake VitaSDK e testes estaticos
aprovados. AUDIO NO VITA FISICO AINDA NAO FOI TESTADO. Efeitos reais
dependem dos arquivos `data.win`, `audiogroup1.dat` e musicas `.ogg`
do jogo instalados na estrutura de diretorios usual.

VPK de laboratorio isolado:
`artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm9.vpk`
Title ID `DLTAGXM09`; gxm8 e Modern-GL jogavel preservados.


## Decima etapa: v0.78-gxm10 — troca de capitulos nativa (12/10/2026)

Relatorio do usuario: gxm9 reproduz AUDIO real no Vita com Chapter 0
visivel e funcional. O log gxm9 confirma device=1/context=1, surface
640x480 e 0 texturas ausentes, com frame 1458 shutdown=pass; amostras
audio_metrics mostraram active=0 nos instantes registrados, o que
NAO contradiz o som ouvido durante a sessao. O menu do Chapter 0
apresenta acesso aos outros capitulos, mas gxm9 estava limitado a
carregar chapter0/data.win, sem processar game_change.

Nova versao gxm10:
- bs_bootstrap_load(&bs,chapter) carrega chapter0..5/data.win;
  o OverlayFileSystem usa o diretorio do chapter selecionado, preservando
  saves EXPERIMENTAIS em ux0:data/deltarune_native_gxm_saves/.
- O runner usa osType=OS_WINDOWS e o mesmo vetor gameArgs do
  playable Vita: Chapter0/Launcher ou chapter>N/returning launcher.
  Requisicoes game_change gravadas pela VM agora sao reconhecidas,
  seus alvos sao validados (somente 0..5) e iniciam
  sceAppMgrLoadExec("app0:eboot.bin") APOS a apresentacao do frame.
- Marcador de relaunch isolado:
  ux0:data/deltarune_native_gxm_saves/next_chapter.txt
  O marcador e consumido e excluido na inicializacao; nao interfere em
  ux0:data/deltarune/deltarunevita/next-chapter.txt usado pelo port
  playable Modern-GL.
- Reinicio selecionado nos capitulos 1–5 inicia Runner_initFirstRoom
  automaticamente; no Chapter 0 frio ainda e necessario Quadrado.
- Gxm10 remove colisao de controles de cena durante gameplay.
  No jogo, START entra como GP_START/VK_ESCAPE; SELECT entra como
  GP_SELECT/VK_TAB. No laboratorio, SELECT+START encerra
  durante gameplay; START sozinho encerra somente as cenas de teste.
- Logs novos: chapter_boot selected=, game_change request cwd=,
  game_change relaunch_begin chapter=, target_missing,
  e relaunch_returned se falhar.
- real_tpag_probe CH0 so inicializa para o capitulo 0; os outros usam
  os TPAGs do DataWin respectivo na renderizacao do jogo.
- Linkagem SceAppMgr_stub, sem VitaGL ou runtime shacccg.
- VPK de laboratorio: Deltarune-NativeGXM-v0.78-gxm10.vpk,
  TitleID DLTAGXM10.

TESTES: compilacao Docker VitaSDK concluida e contratos estaticos
aprovados. Troca de capitulos no Vita fisico AINDA NAO VALIDADA.
Os demais 5 chapters podem acionar callbacks GXM ainda incompletos,
shaders/efeitos, superficies complexas ou problemas de memoria.
Preservar gxm9 que ja reproduz audio e o Modern-GL estavel.


## v0.78-gxm11 — renderer parity & Ch3 (12/10/2026)

Physical test from gxm10: Chapter1 FPS fell 9–18 after hundreds of TPAG images were uploaded (tex_loaded 43 -> 2016). Chapter3 parsed 246 rooms, 4992 sprites, 14640 TPAG and 397 TXTR and then stopped on "unsupported=gpu_color_mask". Dump: psp2core-1791849430-0x00000d2cb3-eboot.bin.psp2dmp.

gxm11 native-only changes:
- Isolated nanovg_gxm_native.h, never touches shared NanoVG or stable VitaGL.
- Native sceGxm fragment-program variants per draw RGB/alpha mask and actual source/dest blend factors; cache limit 96 combinations.
- gpuSetColorWriteEnable applies channel mask instead of abort.
- Lazy individual TPAG uploads instead of uploading every sprite in a decoded texture page; last decoded page reused.
- Logs pages_decoded, items_uploaded, partial_masks and frame_cost step/draw/swap microseconds (300-frame sampling).
- First native Game Settings adapter reads existing config.ini, applies master/music/SFX volume to OpenAL; SELECT opens overlay, D-pad adjusts, CIRCLE closes. 30/40/60 cap and screen stretch supported; saves only edited setting keys.
- Not a full Game Settings port: native controls editor, border graphics, full filters, shaders, trophy UI, mod/renderer selectors still pending.
- Vita physical validation of gxm11 pending, particularly shader variant allocation/alpha and Chapter3. gxm10 and stable ModernGL preserved.


## v0.78-gxm12 — Chapter0 directo, bordas e Chapter2 (2026-10-12)

Vita gxm11 log showed: Chapter2 successfully loaded 278 rooms, 3382 sprites,
9558 TPAG, 415 texture pages, but FPS dropped to 10–30. The high 300-frame
render-cost sample was step_us=5604, draw_us=53068, swap_us=1925. The
last log line before its new .psp2dmp was
NATIVE_GXM=runner_renderer unsupported=drawTextColor_gradient.

gxm12 corrections, isolated to experimental native GXM:
- Starts automatically in Chapter0 from app launch; synthetic demo, manual
  Square activation, cyan heartbeat removed. Square still maps as gamepad.
- drawTextColor with four distinct colors no longer aborts. Bilinear
  interpolation by glyph center retains the directional gradient; exact
  per-pixel GXM interpolation still requires its own vertex-color pipeline.
  First occurrence logged as NATIVE_GXM=text_gradient.
- Chapter2 performance: fast sprite path for untransformed sprites, avoid
  unnecessary NanoVG state save/restores and pixel-art antialiasing/stencil
  stroke passes. Device measurements pending; no 60FPS claim.
- Native border subsystem reuses portable VitaBorderRules_resolve and
  VitaBorderRules_filename, reads PNG files in original
  ux0:data/deltarune/deltarunevita/borders, draws full-screen border behind
  application surface, supports Better Borders from mods/Borders, plus
  simple white side frame. Detects live border controller sprite name
  including existing _0 suffix and suppresses menu/transition surrounds.
  Path/failure log NATIVE_GXM=border.
- Extended native Game Settings: SELECT toggles, D-pad navigate/change,
  CIRCLE closes. Nine settings: master/music/SFX, mute, target FPS,
  stretch, border Off/Dynamic/Simple, Better Borders, brightness.
  Shared config.ini remains common with VitaGL playable.
  Advanced shader/filter options, trophies, touch editor, complete
  controller crossfade effects and mod switching are not yet ported.
- Previous gxm11 and playable ModernGL preserved.
- Requires actual PS Vita testing of Chapter2 performance, border sprites,
  gradient rendering and launch flow. Build and static tests are not
  physical-runtime validation.


## v0.78-gxm13 — Original VitaSettings UI native adaptation (2026-10-12)

Source of truth: src/vita-probe/source/vita_settings.c and vita_settings.h.
The playable VitaGL version stays unchanged. The experiment ships a native
copy under experiments/native-gxm/vita_settings_native.c (about 148 KB).
Replaces ONLY immediate-mode VitaGL icon/quads with NanoVG/GXM operations;
retains original menu layout, categories, navigation, translations, modal
confirmations, Game Settings state, INI parsing/writing, touch-control
positions/editor, trophy list and local notifications, zoom calibration,
trophy/descriptive strings, brightness and volume controls.
The old simplified native_settings.inc has been removed.

Original UI resources included inside GXM13 VPK:
- PlayStation controller buttons, joystick/touch assets, dialog corners,
  heart/trophy art and flag icons packaged at app0:assets/ui/*.png and
  app0:assets/ui/Lang/*.png.
- 7 original Lang INI translations packaged at app0:assets/ui/Lang/.
- Native icon loading resolves app0 first, ux0:data/deltarune/deltarunevita/ui
  fallback; decodes PNG with ImageDecoder_decodeToRgba and allocates NanoVG
  GPU textures. These are PNG resources, not BC3/PVR.

Input bridge: SceTouch_stub / sceTouchSetSamplingState(front) initialized.
Original VitaSettings_handleInput runs with SceCtrlData; nativeTouchPoll
implements same categories and slider tapping, trophy scrolling, touch-editor
drag and gameplay touch buttons/stick. Touch overlay uses the same original
spr_joybase/spr_joystick/face-button graphics.

Native compositing now follows original displayOffsetX/Y + displayZoom and
stretch setting. Original brightness and touch visuals are called each frame.
Original menu color, border frame, typography, footers, language and prompts
are reused rather than rewritten as a separate six/nine-item menu.

IMPORTANT BACKEND LIMITS:
- The GXM game sprite/text/icon pipeline still decodes to RGBA8888.
  There is currently NO native BC3/PVR/PVRTC texture upload on this renderer.
- The Graphics submenu keeps the original five entries and language/text
  placement but reports RGBA8888, fixed GXM, and filter/shader unavailable.
  Unsupported BC3/PVR, GPU shaders, filters and GL renderer switches are
  intentionally read-only, to prevent corrupting the shared config.ini.
  FPS target remains live and writable. Do NOT imply a working BC3 switch.
- Physical Vita NP trophies are not initialized by native GXM13. The trophy
  list and local notification UI source are present but NP unlock events
  still require native integration. Developer Wi-Fi log upload reports
  unsupported; local log rotation has native implementation. Some advanced
  Developer tools (room navigation, exact GPU budget and overlays) need GXM
  backend plumbing.
- VitaGL border fade/controller effects still need porting; existing native
  border rules and installed PNG border art remain.
- Compiled with Docker VitaSDK. Static callbacks and VPK asset packaging
  validated; original UI appearance and interactions NEED physical Vita
  test. Keep gxm12 VPK and stable VitaGL playable build as fallbacks.

Native implementation intentionally isolates GXM-specific code under
experiments/native-gxm/ and NEVER changes original src/vita-probe source.


## v0.78-gxm14 — Chapter3 alpha-test crash (13/10/2026)

Physical Vita regression from gxm12:
- Chapter0 auto-start, game_change target Chapter3, data.win and Runner parsed and initialized.
- Ch3 first 5 frames had no missing textures, then renderer deliberately invoked
  unsupported=alpha_test_enable. The contract's bs_unsupported ends with abort(),
  matching the user's psp2core-1791854085-0x0000293029-eboot.bin.psp2dmp.
- Chapter3 border is located and decoded from
  ux0:data/deltarune/deltarunevita/borders/border_dw_tv_black_0.png;
  log reports image=4, not a file-loading error.

Native-only correction:
- gpuSetAlphaTestEnable(false/true) and gpuSetAlphaTestRef(uint8_t) are now
  non-fatal. Both update explicit renderer state and produce bounded diagnostic
  log lines NATIVE_GXM=alpha_test and NATIVE_GXM=alpha_test_ref.
- Original VitaGL behavior is GL_GREATER: alpha > ref/255. Uniform-alpha
  operations with alpha<=ref now skip drawing. For binary-alpha sprites with
  ordinary source-over blending, transparent texels already have no visible
  contribution; this is the native compatibility path.
- IMPORTANT: the native NanoVG-GXM renderer currently embeds precompiled GXP
  fragment shaders. True arbitrary per-texel GPU alpha discard for complex
  blend/color masks and nonzero reference thresholds is NOT implemented.
  Shaders require separately compiled native GXP. Current correction prevents
  the demonstrated explicit abort; it is NOT a claim of pixel-perfect effect
  parity or that no later callback can fail. Review new alpha_ref diagnostics.
- Previous gxm13 full UI remains. Previous VPKs and playable VitaGL unchanged.
- Need real Vita Chapter3 test, specifically same room and transition plus
  borders, alpha, no unexpected nonzero-ref effects or subsequent crashes.
