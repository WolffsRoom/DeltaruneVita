## 2026-10-17 — v0.78-1 / crash na abertura normal

O dump `psp2core-1792123813` registra prefetch abort na thread
`avPlayer Controller` logo após a primeira alocação PHYCONT. VitaGrafix, VGi e
pngshot estavam carregados; o laboratório Native GXM não participou do processo.
A proteção de vídeo passou a reconhecer VitaGrafix/VGi e selecionar a sequência
PNG antes de iniciar o AvPlayer. Validação no aparelho pendente.

## 2026-10-12 — v0.78 / laboratório Native GXM

Criado e compilado um VPK separado que apresenta uma cena 2D animada usando
`sceGxm` diretamente. O binário não contém VitaGL nem `libshacccg`; usa shaders
GXP incorporados e testa buffers triplos, blend, stencil e fila de apresentação.
O backend completo do Butterscotch ainda não foi substituído. Detalhes em
[`native-gxm-v078.md`](native-gxm-v078.md).

## 2026-10-12 — v0.77-17

O GPU crash do Chapter 2 foi localizado na transição da Mansão para
`room_dw_mansion_east_2f_a`: a sala criava uma surface 1920×480 e uma cópia
dinâmica com somente 6,5 MiB livres. O Experimental-GL agora limita os atlas
da Mansão a 92 MiB e retém no máximo 88 MiB nas portas internas. Teste local
de contratos incluído; validação de sessão longa no Vita pendente.

VPK v0.77-17 compilado com 171 entradas e CRC íntegro:
`artifacts/current/Deltarune-v0.77-17.vpk`, 5.646.904 bytes, SHA-256
`0f32dfd7baecf549ab02805878a1720d52ee466d761fa387a6c32289f9aec4b5`.

## 2026-10-11 — v0.77-11

Identificada a causa das sombras ausentes do Chapter 5: programas experimentais
eram ativados, mas o bridge enviava vértices somente ao fixed-function interno
do VitaGL. Atributos genéricos 0/1/2 implementados; regressão real passou.
VPK v0.77-11 gerado: 171 entradas, CRC íntegro, 5.645.275 bytes, SHA-256
`f4cec87108da1e182c1ca2af2243bd3a5d287649a44e715d5fa3a92d1a4fd222`.
Validação visual no Vita pendente. Detalhes em
[ch5-town-shadows-v07711.md](ch5-town-shadows-v07711.md).

## 2026-10-11 — v0.77-10

Relógio em loop e seeks corrigidos; teste com as duas faixas reais passou.
Preparação limitada de texturas na entrada de Tenna; validação Vita pendente.
VPK v0.77-10 gerado e validado: 171 entradas, CRC íntegro, 5.645.489 bytes,
SHA-256 `2794b769fa88a093c485604185dba89f8c43ba70a814af0019671fc9c7fa5206`.
Detalhes: [ch3-rhythm-clock-v07710.md](ch3-rhythm-clock-v07710.md).

# Progresso do port

## 2026-10-11 — finale do Chapter 3 / v0.77-9

Tratadas as bordas solicitadas; corrigidos contratos de blend e culling por
projeção. Testes locais de pixels Legacy/Modern e regras de borda passaram.
Prewarm limitado da entrada de batalha incluído. Elementos musicais, bunker,
ataques do Cavaleiro e ganho real de fluidez ainda requerem confirmação no Vita.
VPK v0.77-9 compilado, backup concluído e integridade ZIP verificada.
[Registro técnico](ch3-finale-v0779.md).

## 2026-10-10 — retomada após derrota / v0.77-8

Investigado o fluxo original de Continue do Chapter 3. Corrigida a condição
do escape de tela preta, encurtada a apresentação após Continuar e corrigida
a prioridade dos saves sobre arquivos de mods. Testes locais Legacy/Modern
passaram, incluindo eventos GML reais até o ponto de carregar o checkpoint.
Teste completo de derrota/retomada contra Tenna pendente no Vita.
VPK v0.77-8 compilado e pacote verificado em `artifacts/current/`.
[Registro técnico](gameover-retry-v0778.md).

## 2026-10-09 — Chapter 3 / v0.77-7

Retorno v0.77-6: maze_quiz a cerca de 16 FPS e stealth_d a 26 FPS.
Corrigidos o bloqueio pelo profiler ocioso e a compatibilidade do culling
com a cópia PT-BR auditada. Incluídos os painéis de tesouro e a sala do quiz.
Replay PT-BR com profiling passou em Legacy/Modern, com imagens idênticas.
VPK v0.77-7 compilado, com integridade ZIP verificada, em `artifacts/current/`.
[Registro técnico](ch3-tv-v0777.md).

## 2026-10-07 — Chapter 3 / v0.77-6

O log v0.77-5 aponta queda contínua em stealth_d (20–22 FPS no Vita).
Aplicados getters simples sem alocação e culling conservador dos painéis
de TVs fora da câmera. Replay local mantém as nove capturas idênticas por
renderer, com redução de trabalho; falta medir no hardware.
VPK v0.77-6 compilado e integridade verificada; disponível em `artifacts/current/`.
[Registro técnico](ch3-stealth-v0776.md).

## 2026-10-06 — Chapter 3 / v0.77-5

Aplicados reuso e distribuição dos monitores de puzzlecloset_2, correção da
paleta truncada do quiz e atualização da matriz do shader nas trocas de alvo.
Testes locais de operações e pixels passaram. A foto completa do quiz e o
ganho de FPS ainda precisam de validação no Vita.
[Registro técnico](ch3-monitor-quiz-v0775.md).

## v0.75-8 — vídeo do Chapter 5 após retorno à base v0.75-5

O teste da v0.75-7 abriu/decodificou o MP4, mas não o exibiu e registrou 1.153
falhas de reserva GXM. Código e scripts da v0.75-5 foram conferidos contra o
backup; o VitaGL voltou a usar a arena de uniforms customizados, agora com
espera da GPU antes de sua reutilização. Acrescentado diagnóstico limitado da
faixa de áudio do vídeo. Testes locais passaram; imagem e som no aparelho
aguardam validação. [Diagnóstico](ch5-v0758-video-rebase.md).

## Publicação da pré-release v0.75

Publicada em 2026-09-18 às 09:30 UTC:
https://github.com/WolffsRoom/DeltaruneVita/releases/tag/v0.75

VPK v0.75-7, renomeado para `Deltarune-v0.75.Internal-Build-Test.vpk`, com bytes
preservados e SHA-256 remoto conferido. Inclui `SHA256SUMS.txt`. Notas em inglês
seguem a v0.74 e distinguem implementações, validação pendente e bugs abertos.
A publicação é pré-release e não substitui a versão estável como Latest.
Notas locais: `artifacts/releases/v0.75/release-notes.md`. A tag usa o commit
remoto `2ec956d76ab4436dc581cbc1e8afc755017b785d`; o código local não foi enviado.
As notas esclarecem que os arquivos-fonte automáticos do GitHub não representam
o snapshot completo do VPK.

## v0.75-7 — GPU / Modern-GL (2026-09-18)

Corrigida a coleta com cena GXM pendente e substituído o pool circular de uniforms
por reservas gerenciadas pelo GXM, com rejeição de desenhos em falhas de reserva
também no release. Regressões C passaram após falharem com o código anterior;
18 cenários desktop passaram. VPK recompilado com VitaGL limpo e validado.
Estabilidade e custo no Vita ainda aguardam teste; não há garantia de ausência
de todos os GPU crashes. [Evidências e pacote](modern-gl-v075-7-gpu-lifetime.md).

## Correções locais após v0.74-16

Vídeo com estado GL restaurado e marcador de recuperação do Chapter 5 tratado;
tilemap_get_frame/animação implementados; iluminação do jardim acelerada com
comparação de equivalência e assinatura exata do bytecode. Nove regressões novas,
12 cenários Town/PNG e três de fotografia passaram no desktop. VPK v0.74-17
compilado e verificado; confirmação no aparelho pendente. TP/pétalas aguardam reprodução com o save;
diagnóstico CH5_SCENE_STATE registra instâncias de TP, views e presença de pétalas
somente quando esse estado muda. [Detalhes](ch5-v074-17-fixes.md).

## Publicação da pré-release v0.74

Publicada em 2026-09-16 UTC (2026-09-15 no horário local):
https://github.com/WolffsRoom/DeltaruneVita/releases/tag/v0.74

Build binária v0.74-16, notas em inglês seguindo v0.72, dois prints da Town,
instruções de conversão de vídeo e SHA256SUMS. Cinco anexos conferidos pelos
digests do GitHub; marcada como pré-release, sem substituir a latest estável.
Nenhum MP4 de Chapter ou data.win foi publicado. A tag aponta para o main remoto
`2ec956d76ab4436dc581cbc1e8afc755017b785d`; a árvore local de desenvolvimento não
foi commitada ou enviada nesta operação de release.

## v0.74-16 — Game Settings e pacote de vídeo

Rótulos principais em escala 1,60 e cantos espelhados corretamente em Modern-GL.
Doze regressões locais passaram. Pacote de teste inclui VPK e os dois MP4;
o usuário confirmou que o teste anterior instalou apenas o VPK. Reprodução
dos arquivos convertidos no aparelho continua pendente.

## 2026-09-15 — revisão Chapter 3 sobre v0.74-5

VPK local v0.74-14 gerado. Redução de logs do ritmo, recorte de superfícies,
shaders de fotografia no Legacy e fonte de acentos revisados. Seis execuções de
salas, três testes de pixels e doze regressões Modern passaram. A sequência
exata de `Bugado.png`, desempenho e shaders compilados no Vita continuam
pendentes de reteste. Ver [ch3-v074-review.md](ch3-v074-review.md).

## Primeiras tentativas

O trabalho começou com YoYo Loader e uma base de SoLoader. O jogo chegava à LiveArea, mas congelava o Vita com uso alto de CPU e GPU. Foram adicionados logs por etapa para separar problemas do carregador, do parser e da renderização.

O YoYo Loader serviu para validar a organização dos arquivos e os requisitos do Vita, mas não foi suficiente para executar esta versão do runner do GameMaker. A partir daí o port mudou para o Butterscotch.

## Leitura dos capítulos

Os arquivos `chapter0.wad` até `chapter5.wad` foram analisados e o `assets/game.droid` de cada capítulo passou a ser preparado separadamente. O parser do Butterscotch conseguiu ler os principais chunks do GameMaker, incluindo salas, objetos, scripts, fontes, páginas de textura e bytecode.

O carregamento passou a ser preguiçoso para salas e texturas. Isso evitou colocar todos os dados dos capítulos maiores na memória de uma vez.

## Runner jogável

Foi criado um executável Vita com:

- VM do Butterscotch;
- loop de step e draw;
- filesystem separado para dados e saves;
- áudio temporariamente substituído por um backend vazio;
- teclado virtual mapeado para os botões do Vita;
- log persistente em `ux0:data/deltarune/deltarunevita`.

O seletor chegou a criar a primeira sala, mas os primeiros builds caíam antes do primeiro quadro.

## Diagnóstico dos crashes

Os dumps mostraram três problemas principais:

1. Pool gráfico pequeno para as primeiras páginas de textura.
2. Stack padrão de 256 KiB insuficiente durante a preparação dos shaders.
3. Uso do modo imediato de OpenGL pelo renderer legado.

O pool do VitaGL passou para 64 MiB e a stack principal para 4 MiB. O compilador runtime de shaders foi colocado em modo conservador durante os testes.

## VitaRenderer

O ponto decisivo foi substituir as chamadas `glBegin/glEnd` do renderer legado por client arrays e `glDrawArrays`. Primeiro foram migrados sprites e retângulos. Depois foi criada uma ponte para converter também fontes, tiles, linhas, triângulos e blits de surfaces.

Na versão `00.17`, o seletor de capítulos apareceu e funcionou no Vita real. O menu respondeu aos controles e as páginas de textura de 256×256 e 2048×2048 foram carregadas com sucesso.

## Troca de capítulos

O seletor usa a função `game_change` do GameMaker. O Butterscotch registrava a solicitação, mas o main do Vita não tratava a mudança, por isso a tela ficava em `Initializing Chapter`.

A versão `00.18` grava o capítulo solicitado, reinicia o próprio eboot e abre diretamente o `game.droid` correspondente. O reinício libera os recursos do seletor antes de carregar capítulos que passam de 200 MB.

O `param.sfo` também passou a usar `ATTRIBUTE2=12`. O campo foi conferido no VPK final.

## Próximos pontos

- validar a entrada nos capítulos 1 a 5;
- corrigir builtins ainda não implementados conforme aparecerem no log;
- medir o uso de memória nos capítulos maiores;
- implementar áudio;
- revisar surfaces, shaders e efeitos usados durante o gameplay;
- testar saves e transições entre capítulos.
## 2026-10-11 — v0.77-13

O GPU crash da v0.77-11 foi isolado a `room_dw_castle_cafe`, com `shd_pal_swapper` ativo e sem esgotamento global de VRAM. Castle Town recebeu fallback localizado para esse passe; a chegada prepara Kris, Susie e Ralsei e mantém a borda preta desde o início. Contratos locais cobrem a política; a estabilidade GXM final depende do teste no Vita.

VPK: `artifacts/current/Deltarune-v0.77-13.vpk`, 171 entradas, CRC íntegro, 5.647.114 bytes, SHA-256 `52f6fb08b9d245e3f8e5cce806d8dc5ca4a4ab77e219fa30e363d003a3f63fea`.
## 2026-10-11 — fonte de batalha em todos os capítulos

O recorte horizontal dos diálogos foi localizado nas UVs de glifos: os dois renderizadores usavam as bordas exatas do retângulo do atlas. O VitaGL pode resolver o último fragmento para a linha vizinha, removendo um pixel da base. Legacy/Experimental e Modern passaram a usar centros de texel sem alterar tamanho, posição, avanço ou distância entre linhas. A validação percorre os `data.win` dos Chapters 1–5 nos dois renderizadores locais.

VPK: `artifacts/current/Deltarune-v0.77-14.vpk`, 171 entradas, CRC íntegro, 5.647.060 bytes, SHA-256 `f6b33bd9c8abdad5740445d46841b696e7092c1a65ae4856de2fc89c75f0efdc`.
## 2026-10-11 — v0.77-15

O log real da batalha de Flowey mostrou 36–38 surfaces, cerca de 22 MiB em
targets de composição e somente 12–16 MiB livres na VRAM. Páginas de 1024 px
eram descartadas e recarregadas apesar de caberem nesse espaço. A reserva foi
recalibrada para aceitar essas páginas pequenas sem permitir uma nova página
grande quando não houver margem. A atualização de reflexos a 10 Hz agora vale
também para o Experimental-GL.

O dump do Room Navigator registrou saltos diretos de Flowery para duas salas
do penhasco. O navegador passa a inserir uma barreira de GPU antes de entregar
a troca ao Runner, evitando que o destino reutilize recursos ainda referidos
pela sala anterior. A confirmação definitiva continua dependente do Vita.
## 2026-10-12 — v0.77-16

O log `butterlog3-PLACE_MENU_20261011-231142.log` mostrou que o primeiro Loading
só era desenhado depois de troféus, parsing, VM, renderer, áudio e criação do
Runner. A apresentação foi movida para logo após o VitaGL e agora progride
durante os 31 chunks do `data.win`.

Em `butterlog4-room_dw_fcastle_flowery_20261011-231448.log`, a primeira parte da
batalha chegou a médias de 24–30 ms, mas as fases posteriores acumularam 454
descartes de textura e voltaram a 59–137 ms. Páginas recém-recarregadas agora
recebem uma janela curta de estabilidade, mantendo o limite de VRAM existente.

## 2026-10-17 — v0.78-1

O dump de inicialização `psp2core-1792123813-0x0001244161-eboot.bin.psp2dmp`
não registra GPU crash. A falha é um `prefetch abort` na thread de controle do
SceAvPlayer, com salto para a área de dados do aplicativo. VitaGrafix e VGi
estavam carregados no processo durante a falha.

O protetor de vídeo inicial passou a reconhecer também VitaGrafix/VGi, além dos
plugins UAC_UVC/UDCD_UVC. Nessa combinação, o launcher informa o conflito e usa
os 133 PNGs de fallback, sem iniciar o SceAvPlayer. O restante do jogo continua
com o renderizador selecionado.

VPK normal: `artifacts/current/Deltarune-v0.78-1.vpk`, 171 entradas, leitura ZIP
íntegra, 5.647.200 bytes, SHA-256
`567dafb0066ce0e6aacfa53a48d18edd2893b2f6e5ea57d7aa2f64a6b479a1c8`.

## 2026-10-18 — v0.78-2

O bloqueio anterior era amplo demais: VitaGrafix/VGi não é usado pelo jogo e
agora pode permanecer ativo. Somente os plugins USB UAC_UVC/UDCD_UVC continuam
desativando o AvPlayer. O novo `NewDeltaruneLoadIntro.mp4` foi incorporado como
`app0:first_load.mp4`, com término por EOF e limite defensivo de 30 segundos.
O `sce_sys/pic0.png` foi removido para deixar a LiveArea sem essa sobreposição.

VPK normal: `artifacts/current/Deltarune-v0.78-2.vpk`, 170 entradas, leitura ZIP
íntegra, 7.962.691 bytes, SHA-256
`81ab2cce70cb9a54595aa6df1cad1c3979390a01c316f9a24c610dff9ef31679`.
O MP4 empacotado corresponde byte a byte ao arquivo novo (SHA-256
`51c5bb27dd5ca05e9209ab545f8c54c21c134e6aba4d58b8c896a5ebdcb157a1`).

## 2026-10-18 — v0.78-3

O log da v0.78-2 mostrou `open_ok_native_local`, seguido de zero frames e timeout
de preparação. A comparação dos contêineres confirmou a causa: o novo vídeo
era HEVC/H.265 1920×1080 e continha uma faixa `tmcd`, enquanto o vídeo validado
era H.264 Baseline 3.1. O arquivo novo foi convertido para AVC Constrained
Baseline 3.1, 960×544, 30 FPS, yuv420p, AAC estéreo 48 kHz e duas faixas.

VPK normal: `artifacts/current/Deltarune-v0.78-3.vpk`, 170 entradas, leitura ZIP
íntegra, 7.069.902 bytes, SHA-256
`b1f2bb0ff5619df8d7a70111d0c5576269d1e0341dc17e723ce921f9475cbe90`.
O MP4 extraído do VPK passou novamente pelo contrato e possui SHA-256
`f195732e90d1480bb4190de96f216d3297582cd25b44f957e620df7763082121`,
igual ao arquivo-fonte convertido. A reprodução real ainda requer confirmação
no Vita.

## 2026-10-18 — publicação da pré-release v0.78

Publicada em `https://github.com/WolffsRoom/DeltaruneVita/releases/tag/v0.78`
como pré-release. As notas consolidam v0.76–v0.78, os ajustes mantidos da v0.75
e o laboratório Native GXM até GXM44, distinguindo-o do VPK jogável VitaGL.

O build publicado é v0.78-4, com os assets de LiveArea em
`data/legacy-assets/Newest`. Anexos remotos verificados:

- `Deltarune-v0.78.Internal-Build-Test.vpk`: 7.045.005 bytes, SHA-256
  `436abb856595de6b99a2e3ede274c33d356a0d5061c328a80d608001a76c4249`;
- `SHA256SUMS.txt`: 107 bytes, SHA-256
  `6df6a5f8733e53be060b3f217433fd94b18de41d1ad6e3a1a9ff43da1bc02928`.
