# Backlog

Tarefas concretas e pendentes. Itens de direção geral ficam em [roadmap.md](roadmap.md); itens já
concluídos vão para [changelog.md](changelog.md).

> Convenção: ao concluir um item, mova-o (com a versão/data) para `changelog.md` e remova-o daqui,
> em vez de apenas marcar como feito.

## Gameplay / runtime

- [ ] Validar entrada completa (não só boot) nos capítulos 1 a 5 no hardware real.
- [ ] Corrigir builtins do GML ainda não implementados conforme surgirem no
      `butterscotch-probe.log`.
- [ ] Revisar surfaces, shaders e efeitos usados durante o gameplay (fora do que já foi validado
      no seletor e nas primeiras salas).
- [ ] Testar saves e transições entre capítulos de ponta a ponta.

## Performance / memória

- [ ] Medir uso de memória (RAM + VRAM) nos capítulos maiores, especialmente Capítulo 5.
- [ ] Revisar thrashing/realocação de texturas remanescente fora do Capítulo 2 (já parcialmente
      corrigido, ver changelog v0.42).

## Áudio

- [ ] Revisar robustez do streaming de áudio via OpenAL (buffer, sincronização entre faixas).

## Documentação / housekeeping

- [x] Consolidar `docs/` (remover material da era YoYo Loader/SoLoader, manter só o que é válido).
- [ ] Atualizar `README.md` — a tabela de "Recent Changelog" para em v0.52, mas o build atual está
      em v0.63 (`PORT_BUILD_VERSION` em `playable_main.c`). Preencher o changelog real do intervalo
      v0.53–v0.63 (ver [changelog.md](changelog.md)).
  Nota: manter README e CLAUDE.md sincronizados quanto à versão atual publicada.

## Legado (baixa prioridade)

- [ ] Decidir se `src/legacy/` (soloader, yoyoloader), `config/yyl.cfg` e
      `tools/yoyoloader-builder/` devem ser removidos do repositório ou mantidos como arquivo
      histórico permanente.
