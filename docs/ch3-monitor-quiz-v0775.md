# Chapter 3 — monitores e paleta do quiz (v0.77-5)

VPK gerado e integridade ZIP verificada: `artifacts/current/Deltarune-v0.77-5.vpk`
(5.640.248 bytes, 171 entradas). SHA-256:
`798518b6e9771e64959cc5e3b93cd67ccd9c0a5e12c2765515ba2695420ff272`.
Backup: `artifacts/Backups/v0.77-5_20261006-185027`.

## Alterações

- `room_dw_puzzlecloset_2`: retirado o throttle global de um frame em quatro,
  que só suprimia o alvo CRT. A cópia inicial e os passes cinza continuavam
  sendo executados. Agora cada monitor reaproveita uma atualização em duas,
  alternando pela paridade da instância. A cópia, os passes cinza e o CRT são
  tratados juntos; o Draw final, timers, Step e entrada continuam executando.
- O cache é individual, inicializado antes do primeiro reuso e invalidado
  quando a superfície é recriada, o recorte muda, as dimensões mudam ou o
  monitor retorna depois de não desenhar. O recorte e o enquadramento não
  foram alterados. A imagem do monitor pode ter até um frame de atraso.
- Passes de `obj_board_grayregion` inteiramente fora do monitor são omitidos,
  evitando trocar o render target para um desenho sem pixels visíveis.
- O shader compacto `shd_pal_swapper` examinava somente 64 cores. A paleta
  `spr_gameshow_palette` possui 128 linhas e `bg_towntiles_palette`, 253.
  O limite passa a 256, interrompendo no limite UV da paleta ou na primeira
  correspondência. Nenhum ID de atlas foi fixado no código.
- Legacy/Experimental atualizam as matrizes do shader ao trocar superfície
  ou câmera, sem depender de outra chamada a `shader_set`. O teste desktop
  da paleta usa os atributos convencionais corretos do OpenGL. Removida a
  escrita incorreta em uniform usando o número do texture stage no desktop.

## Validação local

`scripts/test-ch3-monitor-cache.py` executa o Draw GML real da sala nos dois
renderizadores desktop e um teste de pixels da paleta/projeção.

| Medida em dois frames | Antes | Depois |
|---|---:|---:|
| Cópias dos seis monitores | 12 | 6 |
| Cópias por frame | 6 / 6 | 3 / 3 |
| Chamadas de troca de alvo | 24 | 12 |

No teste adicional com duas regiões cinzas (uma sobreposta e outra fora dos
monitores), as trocas de alvo caíram de 72 para 14. Esse teste executa o
`with (obj_board_grayregion)` original nos dois renderizadores locais.

Também passaram as regressões de fotografia/tons de cinza em Legacy, Modern
e Modern GL2, o teste de coordenadas do blit VitaGL e os contratos de GPU do
Chapter 3 e das configurações Experimental-GL.

O teste da entrada 100 da paleta retornava branco com o limite antigo e verde
com a correção. Desativar a atualização de matriz, mantendo a correção da
paleta, faz o mesmo teste retornar preto. Com ambos os ajustes, retorna verde.
O teste C cobre inicialização, distribuição, perda/troca de superfície,
redimensionamento, mudança de recorte e retorno depois de ocultação.

Esses números são contagens de trabalho local, não FPS medidos no Vita.
O cenário completo da foto do quiz não foi reproduzido no aparelho: a correção
da paleta resolve uma falha comprovada, mas a presença do cenário anterior na
foto ainda deve ser conferida. O novo loop do shader também precisa ser
validado pelo compilador/runtime do Vita, que não é executado no teste desktop.

## Teste no Vita

1. Usar o mesmo renderizador, shaders e perfil de textura do teste anterior.
2. Em `room_dw_puzzlecloset_2`, observar centralização, resposta e movimento
   das telas, incluindo sair e voltar à sala.
3. Repetir o quiz com Tenna bravo, observando palco, cortinas e fundo.
4. Comparar os novos `PERF_SUMMARY` com a v0.77-4. Nenhum novo log por frame
   foi habilitado no VPK.
