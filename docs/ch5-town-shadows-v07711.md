# Chapter 5 — sombras da cidade no Experimental-GL (v0.77-11)

## Evidência

O log `butterscotch-probe.log` de 2026-10-11 confirma Experimental-GL com
Shaders ON. `shd_shadowblend`, `shd_shadowblend_evening`, `shd_forcecolour` e
`shd_forcefullalpha` compilaram; os shaders 25 e 41 também ficaram ativos em
`room_town_south`. Cinco superfícies de sombra foram criadas. A captura, porém,
não contém as sombras estáticas nem as projetadas pelos personagens.

A regressão local usa o shader 25 e o `data.win` real do Chapter 5. A máscara
de teste tinha alfa 0 fora e 255 dentro, mas a composição permanecia branca.
Assim, a geração da máscara e o uniforme `shadowCol` não eram a causa.

## Causa e correção

O bridge 2D do Vita enviava posição, UV e cor através de `glVertexPointer`,
`glTexCoordPointer` e `glColorPointer`. Essas são entradas do programa interno
fixed-function do VitaGL. Os programas compilados do Experimental-GL usam
atributos genéricos ligados aos índices 0, 1 e 2; portanto podiam aparecer no
log como compilados/ativos sem receber nenhum vértice.

O bridge agora usa `glVertexAttribPointer` enquanto um programa experimental
está ativo e restaura as client arrays originais no fallback ou `shader_reset`.
Isso cobre quads diretos, a conversão de `glBegin/glEnd`, superfícies, sprites,
tilemaps e primitivas que passam pelos shaders compactos. Legacy-GL sem shader
continua no caminho anterior.

## Validação e limites

`scripts/test-ch5-shadow.py` desenha uma máscara em uma superfície, aplica o
shader real e verifica pixels internos/externos. Falhava antes e passou depois.
Também passaram `test-experimental-gl-contract.py` e `test-finale-render.py`
nos renderizadores Legacy e Modern. O build Vita valida a disponibilidade das
APIs e o link do novo caminho; a aparência final ainda precisa ser confirmada
no aparelho em `room_town_south`, Experimental-GL + Shaders ON.

Pacote: `Deltarune-v0.77-11.vpk`, 5.645.275 bytes, 171 entradas e CRC íntegro.
SHA-256: `f4cec87108da1e182c1ca2af2243bd3a5d287649a44e715d5fa3a92d1a4fd222`.
