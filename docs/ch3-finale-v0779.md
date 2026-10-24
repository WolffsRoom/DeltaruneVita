# Chapter 3: finale, bordas e entrada de batalhas — v0.77-9

## Evidências recebidas em 2026-10-11

O log principal de Chapter 3 identifica v0.77-8, Experimental-GL, backend Legacy
e shaders ON. O registro separado do minigame musical tem apenas cabeçalho e
encerramento; o do bunker contém uma amostra de desempenho. Não comprovam,
isoladamente, a causa dos elementos ausentes. O principal registra shaders
compilados e utilizados, sem mensagem de falha que explique diretamente o caso.

Chapter 2: entrada em `room_dw_cyber_maze_queenscreen` custa ~1,21 s. Durante a
aproximação/início da batalha há frames de ~61,7 ms (45,2 ms de Step), ~99,6 ms
(87,2 ms de Step), ~53,1 ms e ~60,2 ms, com uploads no Draw do controlador e
Virovirokun. Há também um frame posterior de ~477,9 ms, dos quais ~458,3 ms
no Draw Pre; não foi atribuído esse último exclusivamente a texturas.

## Mudanças

- `room_dw_snow_zone`: fallback TV Black independente do plot; o som
  `snd_whip_crack_only`, usado no estalo do Tenna, sinaliza Blue Stars nessa sala.
  A sinalização do Cavaleiro continua tendo prioridade. A sala de batalha
  `room_dw_snow_zone_battle` começa com Blue Stars.
- `room_ed` (Ch3) e `room_cc_fountain` (Ch4) entram no mecanismo de fade/hold
  preto, sem permitir que uma borda anterior reapareça por fallback/configuração.
- Legacy/Modern guardavam fatores de blend convencionais como enums OpenGL,
  mas fatores explícitos como enums GameMaker. `gpu_get_blendmode_ext` devolvia
  valores incompatíveis, e a restauração do alpha-test do Vita reconvertia enums
  GL como se fossem GameMaker. Ambos agora armazenam fatores GameMaker.
- Removida a substituição de DST_ALPHA por SRC_ALPHA ao ativar alpha-test
  no Vita. Os scripts reais das notas usam `scr_draw_set_mask` e vários
  ataques do Cavaleiro dependem de máscaras independentes no destino; esses
  fatores não são equivalentes. O FFP do VitaGL já implementa o descarte de
  fragmentos através de `alpha_op` e `ALPHA_CUT_UNIF`.
- O descarte de sprites no Vita Legacy/Experimental considera a matriz efetiva
  de mundo/view/projeção, e não apenas `camera.viewX/viewY`. Shaders customizados
  e projeções em perspectiva ficam fora desse descarte conservador. Isso evita
  retirar geometria sem poder provar que está fora da tela, podendo aumentar
  trabalho em cenas com muitos sprites sob shaders.
- Preparação antecipada de três SFX curtos da entrada de batalhas: impact,
  weaponpull_fast e tensionhorn. Demais sons permanecem sob demanda.
- Core compartilhado inclui poses desarmadas; queenscreen prepara os sprites
  nomeados de entrada e Virovirokun durante a transição. Não fixa páginas na RAM
  nem pré-carrega famílias inteiras. O custo passa para o carregamento anterior.

## Testes e limites

`test-finale-render.py` passou em Legacy e Modern: pixels idênticos antes/depois
de salvar/restaurar os fatores de blend; projeção transformada conserva um
quad visível com coordenadas altas e descarta outro fora da tela; perspectiva
usa caminho conservador. `vita_border_rules_test` e os contratos de fade preto
passaram com as novas salas e prioridades.
O teste também desenhou os sprites reais `spr_rhythmgame_note`,
`spr_rk_quickslash_marker` e `spr_shelter_open` sob máscara e confirmou pixels
visíveis em ambos os renderizadores. Os cinco testes existentes de monitores,
recortes cinza e troca de alvo com shader também passaram.

Esses testes comprovam as correções de contrato, não a aparência completa dos
ataques, do bunker ou do minigame musical no Vita. Confirmar essas três cenas
com o VPK novo e medir a entrada da batalha do Chapter 2. Não há promessa de
eliminar todos os stalls de VM, áudio ou upload nesta rodada.
