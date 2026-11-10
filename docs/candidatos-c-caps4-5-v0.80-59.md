# Salas pesadas dos Caps. 4 e 5: candidatos a GML em C

**Revisão:** 05/11/2026, a partir dos logs do PS Vita de v0.80-46 a v0.80-57 em `logs/chapter-log/chapter 4` e `logs/chapter-log/Chapter5` (15 arquivos). Nenhuma mudança de código foi feita para este levantamento.

## Como foi medido

- **Tempo por sala:** média ponderada das linhas `PERF_SUMMARY`. Salas com menos de 60 quadros ficaram de fora.
- **Custo por objeto:** `DRAW_HOTSPOTS`, que existe só na batalha final e nos dois Cliffs.
- **Custo por script:** perfis `GML_TOP_STEP_DRAW` das capturas DEV.
- **Quadros lentos:** objeto do maior desenho (`draw_object`) em quadros com mais de 15 ms.

**Limites:** muitas salas não têm perfil de GML, e algumas médias são de antes das otimizações recentes. O número por sala serve para ordenar, não para prometer FPS.

## Salas mais lentas nos logs

| Sala | Média por quadro | Quadros | Situação |
|---|---|---|---|
| `room_dw_churchc_titanclimb1_tiled` | 77 ms | 2060 | Torre em C na v0.80-59, sem teste no Vita |
| `room_dw_fcastle_obscured_bullets` | 65 ms | 465 | Candidato 3 |
| `room_dw_fcastle_flowery` (batalha final) | 48 ms | 19849 | Maior parte antes da v0.80-57; candidatos 2 e 5 |
| `room_dw_fcastle_yellowblue` | 45 ms | 1034 | Candidato 4 |
| `room_dw_fcastle_ultradash` | 44 ms | 364 | Quadros lentos vêm da estátua com shader de paleta; não é GML |
| `room_dw_churchb_darkclimb` | 39 ms | 2411 | Inclui batalhas; ver candidato 5 |
| `room_dw_churchb_gersonstudy` | 29 ms | 2952 | Sem perfil de GML |
| `room_dw_fcastle_orange_gauntlet` | 26 ms | 1002 | Candidato 1 |
| `room_dw_fcastle_top_staircase_1`, `foyer` | 23-29 ms | 1594 | Picos de `obj_castlereflect_manager`, já a 10 Hz |

## Candidatos, em ordem recomendada

### 1. `obj_plat_vertical_floor_grass_strip` (plataformas do Cap. 5)
- **Custo medido:** 7,1-7,3 ms por quadro em `room_dw_fcastle_orange_gauntlet`, só 1473 ops, porque quase tudo são chamadas de desenho (5 µs por op).
- **Por quê:** enquanto a grama está visível, cada instância recria a própria surface a cada quadro: limpa, lê o tilemap célula a célula e desenha 3 tiles por coluna. O conteúdo só depende do tilemap, que não muda nessa sala.
- **Ideia em C:** guardar a surface já montada e refazer só quando o tamanho ou os tiles mudarem; o `draw_surface_ext` final continua igual.
- **Ganho estimado:** a maior parte dos 7 ms, nas salas de plataforma que usam esse objeto.
- **Risco:** baixo a médio. Se um tile animado mudar o tilemap, a surface precisa ser refeita; dá para comparar o tilemap a cada quadro, que custa pouco.

### 2. `obj_parallax_cliffs_moving_layers` (batalha final e Cliffs)
- **Custo medido:** cerca de 6 ms por quadro nas fases em que aparece (`DRAW_HOTSPOTS`), e é o objeto mais frequente nos quadros lentos da batalha (57 vezes).
- **Por quê:** desenha duas camadas por quadro com `draw_lay`, que aplica shader e escala por plataforma.
- **Ideia em C:** o mesmo esquema da decoração da torre: calcular as camadas em C e agrupar os desenhos, mantendo o shader.
- **Ganho estimado:** 3 a 5 ms nessas fases.
- **Risco:** médio. Falta ler `draw_lay` por inteiro, e o shader do sol muda por fase.

### 3. `obj_plat_fusuma_mask` (`obscured_bullets`)
- **Custo medido:** 4,9 ms por quadro de GML; a sala inteira passa de 60 ms. Parte do resto é GPU, com surfaces do tamanho da sala (v0.80-47-ch5b).
- **Por quê:** 530 linhas que recortam as portas de correr com surfaces e máscaras todo quadro.
- **Ideia em C:** desenhar só as portas na tela e pular as que estão fechadas e paradas.
- **Ganho estimado:** 2 a 4 ms.
- **Risco:** alto. Muita lógica visual; precisa de captura antes e depois em cada estado de porta.

### 4. `obj_blue_platform` (`yellowblue`)
- **Custo medido:** 2,3 ms de Step e 1,1 ms de Draw, em uma amostra só.
- **Por quê:** laços `repeat (6)` e `repeat (16)` por plataforma, a cada quadro.
- **Ganho estimado:** 2 a 3 ms. **Risco:** médio, porque é lógica de jogo (colisão do jogador).
- **Antes de mexer:** uma captura DEV maior dessa sala.

### 5. `obj_battlecontroller` (todas as batalhas)
- **Custo medido:** 2-7 ms por quadro na batalha final, e aparece nos quadros lentos de `darkclimb`.
- **Risco:** alto. É o controlador geral de batalha, usado em todos os capítulos. **Não recomendo** portar inteiro; só trechos depois de um perfil.

### Não compensa agora
- `obj_flowery_towery` e `obj_flowery_towery_pillars`: cerca de 1,5 a 2 ms cada, já com reuso a 3,75 Hz.
- `obj_castlereflect_manager`: já atualiza a 10 Hz; os picos são de upload de textura, tratados na v0.80-58.
- `room_dw_fcastle_ultradash`: os quadros lentos vêm do shader de paleta da estátua, não de GML.

## O que falta medir

Faça uma captura DEV (`GML_TOP_STEP_DRAW`) de pelo menos 30 s nestas salas, pelo caminho normal do jogo:

- [ ] `room_dw_churchb_gersonstudy` (Cap. 4)
- [ ] Batalhas da `room_dw_churchb_darkclimb` (Cap. 4)
- [ ] `room_dw_churchc_angelprophecy_encounter` e `room_dw_churchc_titanclimb2` (Cap. 4)
- [ ] `room_dw_fcastle_yellowblue` e `room_dw_fcastle_obscured_bullets` (Cap. 5)
- [ ] Batalha final em fases com o fundo dos penhascos (Cap. 5)

Com esses perfis dá para confirmar os candidatos 2 a 5 e achar outros.

## Implementação v0.81 (07/11/2026)

- `obj_plat_vertical_floor_grass_strip`: cache nativo da surface. No A/B local
  de 600 quadros, o tempo total do processo caiu de 12,651 s para 8,476 s; as
  capturas dos quadros 300 e 599 são byte a byte idênticas. O Draw deixa de
  aparecer no perfil estável (302 ops por amostra antes; caminho C depois).
- `obj_parallax_cliffs_moving_layers`: o perfil Simple já escolhido para o
  Vita ganhou um Draw nativo que conserva `event_user(0)`, sprites 2/3,
  posições, escalas e alpha. O salto direto para `flowery` não constrói a fase
  dinâmica que usa esse Draw; as capturas Simple antes/depois são idênticas,
  mas o ganho precisa ser medido pelo caminho normal no Vita.
- `obj_plat_fusuma_mask`: o GML original foi preservado. Antes de executá-lo,
  apenas paredes completamente fora da câmera são marcadas temporariamente
  como não visíveis, e o valor é restaurado logo após o Draw. Na câmera inicial
  de `obscured_bullets` não havia parede elegível; capturas idênticas e ganho
  local inconclusivo. Estados das portas, máscaras e colisão não foram tocados.
- `obj_blue_platform`: somente o Draw foi portado (platswap, squash e retículo).
  O Step, colisão, partículas e áudio continuam no GML. As capturas dos quadros
  300/599 são idênticas; o A/B local ficou dentro do ruído, porque a sala
  iniciada diretamente não aciona a sequência interativa medida no Vita.

Revisão de 10/11/2026 (v0.81-14): o cache do sol do penhasco e o atalho do chão
`obj_plat_floortex_FLOOR` da v0.81-10 divergiam do GML e foram corrigidos; o A/B
de penhasco acima não ativava o caminho nativo (`native_cliff: false`). Ver
`docs/revisao-desempenho-v0.81-14.md`, que também cobre os Caps. 2 e 3.

Artefatos A/B: `scratch/v081-ch5-c-candidates/`. O hardware ainda é a fonte de
verdade para os candidatos 2 a 4; nenhum ganho de FPS no Vita é afirmado aqui.
