# 0.74 — Chapter 5, cache e navegador de salas

Revisão de 2026-09-14. A versão-base em `config/version.txt` passa a 0.74.

## Evidências dos logs

- `butterlog1-room_dw_castle_town_20260914-012329.log`: páginas 98, 116 e 117
  recusadas pelo limite de cache; substituições frequentes de páginas 22, 100,
  116 e 117. Havia aproximadamente 10 MiB de RAM, 18 MiB de PHY e 74 MiB de
  CDRAM livres no trecho final. O limite fixo de 24 MiB ocultava partes da sala
  mesmo com memória disponível. Foram registrados picos de 249–412 ms durante
  uploads, embora trechos estáveis ficassem perto de 13–15 ms por quadro.
- `crash_butterscotch-probe.log` e dump `psp2core-1789359853-0x00007a2681-eboot.bin.psp2dmp`:
  transição de 225 para 226, `room_dw_fcastle_flowery`, após navegação por salas.
  O PC pertence ao SceGxm. A pilha, resolvida com o ELF preservado de v0.73-25,
  aponta para `sceGxmSetUniformDataF` em `update_scissor_test`, chamado por
  `glClear`/`draw_clear_alpha`. O código ignorava o resultado da reserva do
  buffer de uniforms e deixava o ponteiro local sem inicialização.
- A reprodução desktop da sequência cria dezenas de superfícies, muitas de
  360×360. Isso sugere pressão nos recursos de renderização do driver. O dump
  não contém o retorno da reserva nem todas as variáveis globais, portanto não
  permite afirmar qual erro de GXM iniciou a falha.

## Correções

### Texturas e picos de carregamento

O cache passa a ter **alvo de retenção de 48 MiB**, sem recusar o conjunto usado
no quadro atual apenas por superar esse alvo. Páginas antigas podem ser expulsas;
uploads continuam sujeitos ao espaço real nos pools mapeados, com reservas de
3 MiB em RAM, 1 MiB em PHY e 16 MiB em CDRAM. Isso preserva margem para superfícies.

Trocar de sala deixa de esvaziar o cache quando há espaço para construção da
próxima sala. Invalidação explícita continua disponível. A prioridade de fontes,
proteção de LUTs e liberação adiada permanecem; pressão meramente causada pelo
alvo de retenção não permite expulsar uma textura já usada no quadro atual.

### Recorte, limpeza e destinos de renderização

As reservas internas de uniforms de vértice/fragmento agora inicializam o
ponteiro e verificam o retorno. Se falharem, recorte/limpeza não passam um
ponteiro inválido ao GXM, restauram os estados de desenho e deixam o recorte
pendente para nova atualização. O log Modern registra
`MODERN_GL_INTERNAL_UNIFORM_ERROR` com o código e a sala, caso isso ocorra.
Essa proteção evita o acesso inseguro identificado; a eliminação completa do
crash ainda exige confirmação no Vita.

O build habilita `SHARED_RENDERTARGETS=1` do VitaGL, sem reciclagem forçada.
Destinos de mesmo tamanho e largura até 512 podem compartilhar o objeto interno
do driver, com até oito referências. Os buffers de cor/profundidade continuam
separados. Isso cobre as superfícies 360×360 de Flowery e reduz a quantidade de
objetos internos. A mudança de flags exige recompilação limpa do VitaGL, feita
na geração do pacote final.

### Sombras

Não foi encontrado bloqueio de `obj_sunshadows` para Modern-GL. A captura
`local-test/logs/modern-town/gl2-deferred-town-239.png` mostra a sombra em
`room_town_mid` no desktop. A ausência relatada no Vita pode envolver uma página
recusada pelo cache ou uma falha gráfica específica; não foi considerada
resolvida só com essa captura.

### Estado do festival e bloqueios

`layer_destroy` ignorava camadas vindas do editor e, nas dinâmicas, não destruía
suas instâncias. Agora remove ambos os tipos, executando Destroy/Clean Up das
instâncias. A camada é destacada antes desses eventos, evitando referências
inválidas se um evento criar ou remover outras camadas. Os dados originais da
sala são preservados para a próxima entrada.

Confirmado no `data.win` preparado: `obj_room_town_mid` usa `global.flag[1324]`
para remover `OBJECTS_FESTIVAL` e as colisões do festival de manhã, e
`OBJECTS_MORNING_COLLISION` quando o festival começou. A falha mantinha confetes
e bloqueios ativos. A correção segue a [semântica de layer_destroy do GameMaker](https://manual.gamemaker.io/lts/en/GameMaker_Language/GML_Reference/Asset_Management/Rooms/General_Layer_Functions/layer_destroy.htm)
e vale para ambos os renderizadores; não força flags nem altera saves no Vita.

## Testes reproduzíveis

- `test_modern_town.py`: 12 casos de 600 quadros, GL2/GL3, shaders imediatos e
  adiados; cidade, sala de Seth e rota 22 → 54 → 225 → 226. Inclui leitura de
  pixels de PNG, 16 fontes regulares e cópia de superfície.
- `test_modern_shaders.py`: 14 casos de 300 quadros dos capítulos 1–5 e salas
  selecionadas.
- `test_festival_layers.py`: 24 casos GL2/GL3, estados 0–3 do festival em
  `room_town_mid`, `room_town_south` e `room_town_north`. Verifica visibilidade das caixas, existência das camadas,
  ausência de instâncias ativas nas camadas removidas e confetes apenas no
  estado 1. Capturas de manhã e festival inspecionadas.
- `test_modern_cache_policy.py`: funções C reais de admissão/expulsão com
  alocação simulada; protege o quadro atual quando só o alvo de cache é excedido.
- `test_vita_uniform_reservation.py`: falhas injetadas nas reservas de vértice
  e fragmento e recuperação; confirma que o ponteiro anterior não é reutilizado.
- `test_vita_render_targets.py`: seletor real do VitaGL com driver simulado;
  30 superfícies 360×360 usam quatro destinos, dimensões incompatíveis ficam
  separadas e todas as referências são liberadas.

Os testes desktop não executam GXM. Os testes C simulam o driver e a memória;
não medem desempenho no Vita. A rota automática reproduz as transições, mas
não o save/posicionamento completo do usuário nem a batalha inteira de Seth.

Resultado desta revisão: **50/50 casos desktop e 3/3 testes C aprovados**.
Inspeção visual das capturas de Hometown, Castle Town e Flowery realizada.

## Pacote

`v0.74-1` e `v0.74-2` foram compilações intermediárias. O pacote `v0.74-3`
inclui também a correção das camadas do festival. VitaGL foi recompilado limpo
em `v0.74-2`, com as mesmas flags e fontes usadas no pacote seguinte.

Build Release concluído; CRC de todos os arquivos do VPK e `APP_VER=00.74`
verificados. SHA-256: `4eb0e653a08722f943f9d0bb97296c193a444bdd17eb319e05f85e3444cc0f64`.
