# Changelog

Mudanças importantes por versão. Itens de trabalho pendente ficam em [backlog.md](backlog.md); ao
concluir um item do backlog, registre-o aqui com a versão/data e remova-o de lá.

> Este arquivo é a fonte de verdade para "o que mudou". O `README.md` mantém uma tabela resumida
> para o usuário final — ao adicionar uma entrada aqui, considere se vale espelhar no README também.

## Não lançado (pós-v0.63, em desenvolvimento)

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
- Criado `CLAUDE.md` na raiz como visão geral e convenções do projeto para assistência por IA.
