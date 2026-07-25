# CLAUDE.md

Guia para trabalhar neste repositório com assistência de IA. Mantenha este arquivo atualizado:
sempre que uma decisão importante for tomada, ou algo novo for implementado/mudado, atualize a
seção relevante aqui (e o arquivo correspondente em `docs/`, se aplicável).

## O que é este projeto

Port não-oficial de **DELTARUNE (Capítulos 1–5)** para PlayStation Vita. Desde a v0.36, o projeto
executa diretamente os dados do GameMaker da versão Windows/Steam usando uma implementação própria
do [Butterscotch](https://github.com/ButterscotchRunner/Butterscotch) (reimplementação open source
do runner do GameMaker: Studio), com renderização via
[VitaGL](https://github.com/Rinnegatamante/vitaGL). Não há dependência de Android/APK — essa via foi
avaliada e abandonada no início do projeto.

Este repositório **não contém** assets ou dados comerciais de Deltarune. O usuário final precisa
possuir a versão Steam legítima do jogo.

## Documentação

| Arquivo | Conteúdo |
|---|---|
| `CLAUDE.md` (este arquivo) | Visão geral, convenções, como colaborar no projeto |
| `docs/arquitetura.md` | Componentes, pipeline de dados/build, decisões técnicas (VitaRenderer, cache de texturas, NPOT, troca de capítulo) |
| `docs/roadmap.md` | Direção de médio/longo prazo |
| `docs/backlog.md` | Tarefas concretas pendentes |
| `docs/api.md` | Superfícies de integração: CLI dos scripts, opções de CMake, contrato de dados no device, builtins do GML |
| `docs/changelog.md` | Histórico de mudanças por versão |
| `docs/PROGRESS.md` | Narrativa do desenvolvimento — o que foi tentado, o que falhou, o que funcionou (YoYo Loader → SoLoader → Butterscotch nativo) |

`README.md` é a documentação voltada ao usuário final (instalação, controles, screenshots). Não
duplique conteúdo técnico lá — o README deve linkar para `docs/` quando precisar de profundidade.

## Regra de manutenção deste arquivo

Sempre que você (IA ou humano) tomar uma decisão arquitetural, mudar de abordagem, ou implementar
algo novo relevante:

1. Atualize `docs/arquitetura.md` se mudou algo estrutural (componente, pipeline, decisão técnica).
2. Atualize `docs/roadmap.md` se mudou a direção planejada.
3. Mova o item concluído de `docs/backlog.md` para `docs/changelog.md`.
4. Atualize este `CLAUDE.md` se mudou uma convenção, comando, ou fato que outra IA precisaria saber
   antes de mexer no projeto.

Não crie novos arquivos `.md` soltos na raiz ou em `docs/` para documentar features pontuais — use a
estrutura existente. Se um documento novo genuinamente não se encaixa em nenhum arquivo acima,
pergunte ao usuário onde ele deveria viver antes de criar um novo.

## Histórico resumido (contexto que evita retrabalho)

O projeto começou com **YoYo Loader** e depois **SoLoader**, tentando rodar o APK Android no Vita.
Essa abordagem chegava à LiveArea mas travava o Vita com uso alto de CPU/GPU — o hardware (512MB RAM,
ARM Cortex-A9) não aguentava a camada de emulação do ambiente Android. **Essa via foi abandonada.**
O código ainda existe em `src/legacy/` e `tools/yoyoloader-builder/` apenas como referência histórica
— não é o pipeline ativo e não deve ser usado como base para novo trabalho.

A abordagem atual lê `data.win` diretamente da instalação Windows/Steam, sem intermediário Android.
Ver `docs/arquitetura.md` para os detalhes técnicos completos e `docs/PROGRESS.md` para a narrativa.

## Estrutura do repositório (visão rápida)

```
src/butterscotch/    Motor GameMaker (fork vendored, VM + parser + renderer)
src/vita-probe/       Ponte com o hardware do Vita (entrypoint, settings, vídeo, bordas)
src/legacy/           Código morto (soloader, yoyoloader) — não usado no build ativo
third_party/vitaGL-nosplash/  VitaGL vendored (fork "no splash")
tools/                Utilitários de análise/modding (UndertaleModTool, xdelta, parse-core)
scripts/              Pipeline de preparação de dados e build (.ps1)
docs/                 Documentação técnica (ver tabela acima)
```

Detalhes completos em `docs/arquitetura.md`.

### Onde fica o código dos patchers (não é óbvio)

Os patchers (a ferramenta que o usuário final roda para gerar `VitaFiles/deltarune` a partir da
instalação Steam) **não vivem em `src/` nem em `main`**:

- **Patcher desktop** (32/64-bit, Python + PyInstaller): fonte em
  `artifacts/Patcher/Build_Patch/src/` (`deltarune_vita_patcher.py`, `build_patch_data.py`,
  `DeltaruneVitaPatcher.spec`). Fica dentro de `artifacts/`, que é gitignorado — é um diretório de
  trabalho local, não algo versionado em `main`. `build_patch_data.py` gera `patch_data/manifest.json`
  a partir de `data/prepared/...` (saída do `prepare-windows-data.ps1`) comparado com
  `SteamFiles/v0.0.250/DELTARUNE`; roda-se via `Build_Patcher.bat`.
- **Web Patcher**: fonte é a branch `gh-pages` do próprio repositório (`index.html`, `manifest.js`,
  `mods_list.json`), não `main`. Se precisar editá-lo, dê `git fetch` e cheque essa branch — não
  procure em `src/`.
- Ambos os patchers leem `mods/mods_list.txt` (JSON com os links de download por mirror, hoje só
  `PTBR`) como fonte única para a lista de mirrors de mods. Ver `docs/api.md` para o contrato.

## Build e desenvolvimento

Pipeline ativo (não use `Dockerfile`/`compose.yaml` da raiz — são stubs não conectados ao build real):

```powershell
# 1. Preparar dados a partir de uma instalação Steam legítima em SteamFiles/DELTARUNE
powershell -ExecutionPolicy Bypass -File .\scripts\prepare-windows-data.ps1

# 2. Build do VPK (Docker + VitaSDK, imagem atamanenko/vitasdk-softfp)
powershell -ExecutionPolicy Bypass -File .\scripts\build-butterscotch-probe.ps1
```

Saída: `artifacts/current/Deltarune-v<versão>.vpk`.

Ao lançar uma versão nova, mantenha sincronizados:
- `PORT_BUILD_VERSION` em `src/vita-probe/source/playable_main.c`
- `VERSION` em `vita_create_vpk` (`src/vita-probe/CMakeLists.txt`)
- A entrada correspondente em `docs/changelog.md` (e no README, se for release pública)

## Fluxo de contribuição e release

Repositório: [github.com/WolffsRoom/DeltaruneVita](https://github.com/WolffsRoom/DeltaruneVita).

Fluxo de trabalho (a partir desta decisão — antes disso, o projeto publicava apenas Pre-Releases
avulsas, sem PR):

1. Commits em branch de feature/fix.
2. Abrir Pull Request — o diff completo fica visível no GitHub.
3. Gerar o VPK local (`scripts/build-butterscotch-probe.ps1`) e testar no PS Vita real: performance,
   gráficos, áudio, texto, regressões. Isso não substitui, é a mesma regra de "não validar correção
   sem teste no Vita real" já descrita abaixo.
4. Feedback no PR; ajustes; novo VPK; novo teste. Repete até aprovar.
5. Merge do PR na `main`.
6. Cria-se uma Pre-Release (pode agrupar vários PRs/ajustes de uma vez).

### Convenção de nomes de VPK e release

- **Build local durante iteração** (`artifacts/current/`): sufixo de letra depois da versão para
  diferenciar builds da mesma versão numérica, ex.: `Deltarune-v0.63b.vpk`,
  `Deltarune-v0.63c.vpk`, `Deltarune-v0.63d.vpk`. É só para uso local, não sobe assim.
- **Pre-Release/Release publicada**: sem a letra — ex.: `Deltarune-v0.63.vpk`.
- **Título da Pre-Release**: `DeltaruneVita v0.XX (Internal Development Build)`.
- **Título da Release**: igual, mas sem o sufixo `(Internal Development Build)`.

### Convenção de descrição de release

Padrão observado nos releases publicados em
[github.com/WolffsRoom/DeltaruneVita/releases](https://github.com/WolffsRoom/DeltaruneVita/releases):

- **Pre-Release** (build interna): descrição em **português**, técnica e detalhada. Cada mudança
  relevante explica problema → causa raiz → solução, citando símbolos do código (funções, constantes,
  ex.: `vitaTextureCacheLimit`, `builtin_array_equals`) e referenciando issues do GitHub quando houver
  (`WolffsRoom/DeltaruneVita#N`). Ao agrupar vários builds internos numa única Pre-Release, usar uma
  seção do tipo "Compilado de Alterações das Builds Anteriores (vX.XX–vY.YY)" resumindo cada uma.
- **Release pública**: descrição em **inglês**, voltada ao usuário final — cabeçalho/imagem, aviso de
  instalação (ex.: "if you already have vX.XX installed, just install the new VPK"), uma tabela-resumo
  de status por capítulo, e uma seção "Detailed Changelog Since vX.XX" em bullets, linkando para o
  release anterior.

Esse padrão espelha a convenção de idioma já usada no resto do projeto (código/commits em inglês,
documentação técnica interna em português — ver seção de Convenções abaixo).

Ao cortar uma Pre-Release/Release, é o momento de sincronizar `PORT_BUILD_VERSION`
(`playable_main.c`) e `VERSION` (`vita-probe/CMakeLists.txt`) — não a cada PR individual, para
evitar commits de bump isolados.

## Convenções e coisas a não fazer

- Não reintroduza a abordagem via APK/YoYo Loader/SoLoader — já foi tentada e abandonada por
  limitação de hardware (ver histórico acima).
- Não implemente builtins do GML especulativamente. Implemente apenas o que aparece como faltante
  no log (`ux0:data/deltarune/deltarunevita/butterscotch-probe.log`), conforme reportado por testes
  reais no hardware.
- Não trate uma correção como validada sem teste no Vita real — dumps/logs de crash e comportamento
  em hardware são a fonte de verdade, não suposição.
- Não inclua nenhum asset, `data.win`, APK ou dado proprietário do jogo no repositório ou em commits.
- Não misture `src/legacy/` com o pipeline ativo (`src/butterscotch/` + `src/vita-probe/`).
- Idioma: código e nomes de variáveis em inglês; documentação e comentários de projeto majoritariamente
  em português (refletindo o time atual); mantenha o idioma predominante de cada arquivo ao editá-lo.

## Créditos e licenciamento

- Butterscotch é derivado sob Mozilla Public License 2.0 (ver `LICENSE`).
- VitaGL: fork de Rinnegatamante/vitaGL.
- DELTARUNE © Toby Fox — este projeto não distribui arquivos comerciais do jogo.
