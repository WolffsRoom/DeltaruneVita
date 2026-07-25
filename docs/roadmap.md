# Roadmap

Direção de médio/longo prazo do port. Para o histórico de como chegamos até aqui, veja
[PROGRESS.md](PROGRESS.md). Para tarefas concretas e imediatas, veja [backlog.md](backlog.md).

## Estado atual

Jogável nos cinco capítulos (v0.63 em desenvolvimento; última versão documentada publicamente no
README é v0.52 — ver [changelog.md](changelog.md) para o que falta documentar entre essas versões).
Renderer, seletor de capítulos, troca de capítulos via reinício de eboot, cache de texturas, áudio
via OpenAL, controles físicos e touch, menu de configurações (EN/PT-BR) e suporte a mod de tradução
já funcionam.

## Direções planejadas

- **Estabilidade de gameplay em todos os capítulos**: validar entrada e progressão completa nos
  capítulos 1 a 5, não apenas o boot/seletor.
- **Cobertura de builtins do GML**: implementar builtins do Butterscotch ainda ausentes conforme
  aparecerem no log (`butterscotch-probe.log`), em vez de tentar prever quais faltam.
- **Memória em capítulos grandes**: medir e otimizar uso de RAM/VRAM nos capítulos maiores
  (especialmente Capítulo 5, que já teve trabalho de tile culling).
- **Áudio**: revisar robustez do streaming OpenAL (sincronização de faixas, thrashing de buffer).
- **Efeitos visuais e surfaces**: revisar shaders, surfaces e efeitos usados durante o gameplay que
  ainda não foram validados no hardware real.
- **Saves e transições**: testar salvamento/carregamento e transições entre capítulos de ponta a ponta.
- **Localização**: expandir suporte além de PT-BR — o pipeline de dados precisa permitir escolher o
  idioma do `data.win`/mod no momento da preparação (hoje é majoritariamente PT-BR vs. inglês padrão).
- **Limpeza de código legado**: eventualmente remover ou arquivar definitivamente `src/legacy/`,
  `config/yyl.cfg` e `tools/yoyoloader-builder/` depois que não houver mais valor de referência.

## Não-metas

- Não há plano de reintroduzir a abordagem via APK Android/YoYo Loader/SoLoader — essa via foi
  avaliada e abandonada (ver [PROGRESS.md](PROGRESS.md), seção "Primeiras tentativas").
- O projeto não distribui e não pretende distribuir assets/dados comerciais de Deltarune.

## Como este documento é mantido

Atualize este roadmap sempre que uma decisão de direção mudar (não apenas quando uma tarefa for
concluída — isso é o papel do [backlog.md](backlog.md) e do [changelog.md](changelog.md)).
