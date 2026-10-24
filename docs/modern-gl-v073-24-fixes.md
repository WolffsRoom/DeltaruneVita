# Modern-GL — v0.73-24

Correções após o teste no Vita de v0.73-23, em 2026-09-13.

## Crash na cidade do Chapter 5

O log registra a passagem de `room_town_south` (24) para `room_town_mid` (22),
pressão de RAM e expulsão de texturas. O dump aponta para liberação de memória
no coletor do VitaGL: os endereços de retorno correspondem a `garbage_collector`
e `vglForceGarbageCollection` no ELF preservado de v0.73-23. A identificação
automática do PC pelo parser não é confiável aqui: o PC pertence ao SceLibKernel.

A coleta forçada agora usa `glFinish()`, que encerra/submete a cena antes de
esperar pela GPU. A chamada direta anterior a `sceGxmFinish()` não fazia esse
encerramento. A coleta sob pressão passou para o início do quadro; uploads
deixam de forçar coleta no meio da cena ou expulsar sucessivamente todo o cache
sem que o contador de memória livre tenha sido atualizado. Lotes pendentes são
enviados antes da expulsão, e IDs removidos são apagados dos samplers salvos.

Isso corrige falhas concretas no ciclo de liberação, mas a confirmação de que
resolve o crash reportado ainda depende de repetir a transição no Vita.

## Cores, PNGs e bordas

- Páginas usadas por `spr_luts` preservam RGBA8888 do TXTR original, ignorando
  caches RGBA4444. A quantização de uma tabela LUT altera o mapeamento de cores.
  Essas páginas também ficam protegidas contra expulsão.
- Vértices 2D inicializam Z em zero.
- PNGs externos usam o shader padrão e enviam o lote imediatamente, restaurando
  depois o shader do jogo.
- A apresentação GL2 usada no Vita agora desenha as bordas; antes isso ocorria
  somente no caminho GL3. As bordas desligam o VBO antes de fornecer ponteiros
  de vértices da CPU.

## Validação

- Build desktop Debug e build Vita Release concluídos.
- Suíte anterior Modern-GL: 14/14 casos, 300 quadros por caso.
- Nova suíte `local-test/test_modern_town.py`: 4/4 casos, GL2/GL3 com shaders
  imediatos/adiados, 600 quadros e quatro transições por caso.
- PNG externo desenhado com shader personalizado previamente ativo: leitura
  exata do pixel RGBA `(37, 149, 211, 255)` e restauração do shader em 4/4 casos.
- Logs e capturas: `local-test/logs/modern-town`; build:
  `logs/build-modern-gl-v073-24.log`.

O desktop não reproduz o alocador/GPU VitaGL nem executa o carregador nativo de
bordas. Não houve validação desta versão no hardware ou comparação de cores
por captura direta do Vita; uma fotografia também sofre influência da câmera
e da tela.

VPK: `artifacts/current/Deltarune-v0.73-24.vpk`, 5.593.473 bytes, 171 entradas,
CRC ZIP íntegro e arquivos de inicialização presentes.

SHA-256: `c90dfcded865bd92701a6a8237c6b6bcb153c1fbb4a8c7ad471d12a220e9bdd2`.
