# Arquitetura

Visão dos componentes do DeltaruneVita e como eles se encaixam. Para o histórico narrativo do
desenvolvimento (o que foi tentado, o que falhou, o que funcionou), veja [PROGRESS.md](PROGRESS.md).

## Visão geral do pipeline

```
Arquivos oficiais Steam/PC (data.win por capítulo)
           ↓  scripts/prepare-windows-data.ps1
data/prepared/deltarune/deltarunevita/  (data.win por capítulo + áudio + mods)
           ↓  scripts/build-butterscotch-probe.ps1 (Docker + VitaSDK)
Butterscotch (VM GameMaker) + VitaGL + OpenAL + controles do Vita
           ↓  vita-elf-create + vita-make-fself + vita-pack-vpk
Deltarune.vpk (eboot.bin + param.sfo + dados)
```

Não há dependência de APK Android nem de emulação — os dados são lidos diretamente do formato
Windows/Steam (`data.win`), copiado como `data.win` por capítulo durante a preparação (sem rename).

## Componentes principais

### `src/butterscotch/` — motor GameMaker (fork vendored)

Fork local de [ButterscotchRunner/Butterscotch](https://github.com/ButterscotchRunner/Butterscotch),
uma reimplementação open source do runner de bytecode do GameMaker: Studio (compatível com WADs
versão 8–17, base Undertale v1.08 / GMS 1.4.1804), estendida e corrigida para rodar Deltarune no Vita.

- `src/data_win.c` — leitor de chunks do `data.win`.
- `src/vm.c`, `src/vm_builtins.c` — VM de bytecode e funções builtin do GML (arquivo mais extenso do
  projeto; é onde builtins faltantes são implementados conforme aparecem nos logs).
- `src/runner.c` — loop principal do jogo (step/draw, eventos).
- `src/instance.c`, `src/spatial_grid.c`, `src/event_table.c`, `src/collision.h` — instâncias de
  objetos, colisão e agendamento de eventos.
- `src/gl_common/`, `src/gl_legacy/` — backends de renderização. `gl_legacy_renderer.c` é o renderer
  usado no Vita (ver seção VitaRenderer abaixo).
- `src/audio/openal/` — backend de áudio via OpenAL (usado pelo `vita-probe`).
- `vendor/` — bibliotecas de terceiros vendored (bzip2, md5, sha1, base64, stb, miniaudio, mojoal).
- Sem subpasta dedicada para Vita — a integração específica de plataforma vive em `src/vita-probe/`.

### `src/vita-probe/` — ponte com o hardware do Vita

Alvo de build ativo (`CMakeLists.txt`, target `PLAYABLE_RUNNER`, ligado por padrão).

- `source/playable_main.c` — entrada principal; inicialização do VitaGL, ajuste de RAM, cache de
  texturas, tratamento de NPOT, integração com o loop do Butterscotch.
- `source/vita_settings.c`/`.h` — menu de configurações in-game (vídeo, áudio, controles, PT-BR/EN).
- `source/vita_video.c`/`.h` — inicialização de vídeo/resolução (`vglInitExtended`).
- `source/vita_borders.c`/`.h` — bordas/overlay do console por capítulo e área.
- `source/vitagl_probe.c` — alvo de build separado (`VITAGL_PROBE`, opcional) para diagnóstico
  isolado do VitaGL, sem o runner completo.
- `source/main.c` — terceiro alvo, parser mínimo de `data.win` com `debugScreen.c`, sem runner.

O CMake usa `file(GLOB ...)` para juntar as fontes do Butterscotch (`../butterscotch/src/*.c`,
`gl_common`, `gl_legacy`, `image`, `audio/openal`) com as fontes do `vita-probe` e libs vendored
(bzip2, md5, sha1, base64), linkando estaticamente contra `libvitaGL.a` e as stubs oficiais da Sony
(`SceGxm`, `SceCtrl`, `SceDisplay`, `SceAudio`, etc.) e `kubridge`.

Detalhes fixos de build atuais (`src/vita-probe/CMakeLists.txt`):
- `TITLE ID`: `DLTVITA01`
- `param.sfo`: `ATTRIBUTE2=12` (necessário para o VPK final funcionar corretamente)
- Versão de build embutida: ver `PORT_BUILD_VERSION` em `playable_main.c` e `VERSION` no
  `vita_create_vpk` do CMake — **mantenha os dois sincronizados** ao lançar uma versão.

### VitaRenderer — decisão arquitetural chave

O renderer legado original do Butterscotch usava modo imediato do OpenGL (`glBegin`/`glEnd`), o que
gera um comando por sprite. O Parameter Buffer do Vita é limitado, e salas cheias de objetos (ex.:
áreas com muitas árvores) estouravam esse buffer e faziam objetos sumirem.

**Solução**: batching de vértices/índices com buffers pré-alocados e `glDrawElements`/`glDrawArrays`
em vez de chamadas imediatas por primitiva. Sprites e retângulos foram migrados primeiro; depois
fontes, tiles, linhas, triângulos e blits de surfaces. Essa migração foi o ponto decisivo que permitiu
o seletor de capítulos rodar em hardware real (v00.17).

```c
// Buffer de índices pré-alocado, usado para descarregar milhares de sprites de uma vez na GPU
static uint16_t vitaIndices[65536];
glDrawElements(GL_TRIANGLES, vitaIndexCount, GL_UNSIGNED_SHORT, vitaIndices);
```

### Gestão de memória e cache de texturas

Texturas 2048×2048 vindas do PC enchem a VRAM do Vita rapidamente. Existe um cache com limite
dinâmico que monitora o uso e despeja páginas de textura antigas quando o limite é atingido
(`vitaEvictTexturePage()`). O limite global atual do cache de texturas é **192 MiB**.

O pool gráfico do VitaGL foi ajustado ao longo do projeto (começou maior, foi reduzido para liberar
RAM para o próprio Deltarune) e a stack principal foi aumentada para 4 MiB (o padrão de 256 KiB não
era suficiente durante a preparação de shaders).

### Suporte a mods e NPOT

Mods (ex.: tradução PT-BR, gerados com UndertaleModTool) costumam ter texturas NPOT (Non-Power-Of-Two,
ex.: 2000×2000). O hardware do Vita não suporta `GL_REPEAT` para texturas NPOT — a textura fica
invisível. A correção detecta se a textura é POT e força `GL_CLAMP_TO_EDGE` quando não é:

```c
bool is_pot = ((w & (w - 1)) == 0) && ((h & (h - 1)) == 0);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
```

### Troca de capítulos

O seletor usa a função `game_change` do GameMaker. O Butterscotch registra a solicitação, mas quem
trata a troca é o `main` do `vita-probe`: ele grava o capítulo solicitado, **reinicia o próprio
eboot** e abre diretamente o `data.win` correspondente. O reinício libera os recursos do seletor
antes de carregar capítulos que passam de 200 MB — isso evita fragmentação/uso excessivo de memória
que ocorreria tentando trocar de capítulo sem reiniciar o processo.

### Convenção de nome de arquivo: `data.win` (sem rename para `game.droid`)

Até a v0.63, o pipeline renomeava `data.win` para `game.droid` em cada capítulo (herança da época em
que o projeto ainda mirava o formato usado pelo port Android/APK). Essa renomeação foi removida: o
engine (`playable_main.c`, `main.c`, `vitagl_probe.c`) agora procura `chapterN/data.win` diretamente,
tanto para o carregamento normal do capítulo quanto para o override de mod
(`mods/<nome>/chapterN/data.win`). Os scripts de preparação (`prepare-windows-data.ps1`,
`prepare-vita-mods.ps1`) e os patchers (desktop e web) foram atualizados para copiar/extrair os
arquivos como `data.win`, sem nenhum passo de rename. Isso simplifica o pipeline de mods: o
conteúdo de um `.zip` de tradução pode ser extraído sem transformação antes de cair em
`deltarunevita/mods/<nome>/`.

### `third_party/vitaGL-nosplash/`

Fork vendored de [Rinnegatamante/vitaGL](https://github.com/Rinnegatamante/vitaGL) (variante
"no splash"), com `libvitaGL.a` pré-compilado no repositório. Não é um git submodule — é código
vendored com `.git` próprio. Compilado via `Makefile` próprio (`NO_SPLASHSCREEN=1 NO_DEBUG=1
SOFTFP_ABI=1`) como etapa do build Docker.

## Pipeline de dados

### `scripts/prepare-windows-data.ps1` (atual)

1. Lê a instalação Steam/Windows legítima em `SteamFiles/DELTARUNE`.
2. Copia o `data.win` de cada capítulo (sem rename).
3. Reestrutura o diretório de áudio (streaming via OpenAL a partir do `ux0:`).
4. Mescla mods preparados (ex.: PT-BR) se presentes.
5. Gera a saída em `data/prepared/deltarune/deltarunevita/`.

### `scripts/prepare-vita-mods.ps1`

Empacota mods (ex.: tradução PT-BR de [Teiarruma/deltarune-ptbr](https://github.com/teiarruma/deltarune-ptbr))
a partir de `mods/PTBR`, copiando `data.win` por capítulo sem rename.

### `scripts/build-butterscotch-probe.ps1` (build atual)

1. Roda um container Docker com a imagem `atamanenko/vitasdk-softfp` (VitaSDK pré-instalado).
2. Compila `third_party/vitaGL-nosplash` (`libvitaGL.a`).
3. Roda CMake + `arm-vita-eabi` toolchain sobre `src/vita-probe` (que também compila o
   Butterscotch via `file(GLOB ...)`).
4. Linka estaticamente contra as libs oficiais da Sony (`libSceGxm`, `libSceDisplay`, `libSceCtrl`, etc).
5. `vita-elf-create` converte o `.elf` para `.velf`; `vita-make-fself` assina/empacota como `eboot.bin`.
6. `vita-pack-vpk` junta `eboot.bin`, `param.sfo`, dados e ícones da LiveArea em
   `artifacts/current/Deltarune-v<versão>.vpk`.

Os arquivos `Dockerfile`/`compose.yaml`/`compose.debug.yaml` na raiz **não fazem parte** desse
pipeline — são um stub genérico (`FROM gcc:latest`) não conectado ao build real. O build de VitaSDK
roda a imagem Docker diretamente via `docker run` dentro do script PowerShell.

## Código legado (não usado no build ativo)

- `src/legacy/soloader/` — abordagem anterior via SoLoader (wrapper de `.so` Android). Ainda referenciado
  por `scripts/build-deltarune.ps1`, que está desatualizado/não é o pipeline atual.
- `src/legacy/yoyoloader/` — abordagem ainda mais antiga via YoYo Loader (launcher/patcher de APK).
  Não referenciado por nenhum script de build atual.
- `config/yyl.cfg` — configuração residual do YoYo Loader, obsoleta.
- `tools/yoyoloader-builder/` — ferramentas de empacotamento VPK da era YoYo Loader, git-ignorado,
  mantido apenas por arqueologia.

Esses caminhos existem por motivos históricos (ver [PROGRESS.md](PROGRESS.md)) e não devem ser usados
como referência para novo trabalho, exceto se o objetivo for entender por que a abordagem foi abandonada.

## Ferramentas auxiliares (`tools/`)

- `UndertaleModTool/`, `UndertaleModToolCLI/` — GUI e CLI headless do UndertaleModTool, usadas para
  inspecionar/editar `data.win` durante investigação de bugs e preparação de mods.
- `nxrune-analysis/` — scripts de análise (`ExportRoomIndex.csx`) para engenharia reversa de GML.
- `nxrune-chapter-1-5/` — patches xdelta por capítulo + utilitário `xdelta.exe`.
- `vita-parse-core/` — utilitário Python (`elf.py`, `core.py`) para analisar `psp2core`/ELF ao
  investigar crashes no hardware real.

## Formato dos dados no Vita

```text
ux0:data/deltarune/
├── config.ini
├── save/
└── deltarunevita/
    ├── chapter0/ … chapter5/   (data.win + assets por capítulo)
    ├── music/
    ├── borders/
    └── mods/
```

Log persistente: `ux0:data/deltarune/deltarunevita/butterscotch-probe.log`.
