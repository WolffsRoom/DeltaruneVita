# Chapter 5 — Castle Town GPU e chegada (v0.77-13)

O dump `psp2core-1791768413-GPUCRASH.psp2dmp` termina em `room_dw_castle_cafe` depois da ativação de `shd_pal_swapper`. O conjunto residente estava em aproximadamente 47 MiB de atlas e 56 MiB totais do VitaGL, portanto não era esgotamento global de VRAM. Antes da falha, a loja permaneceu em quadros de 60–67 ms; o shader compacto podia fazer até 256 buscas dependentes na textura de paleta por pixel.

Nos interiores de Castle Town do Chapter 5, esse shader agora usa o desenho texturizado estável. O caminho continua ativo nas demais áreas, inclusive nas paletas longas do quiz do Chapter 3. A restrição usa o nome da sala e do shader, sem IDs de atlas.

Em `room_dw_castle_area_1`, o log registrou 736 ms no primeiro quadro, dos quais 732 ms eram renderização e 318 ms pertenciam a `obj_mainchara`. Os atlas de Kris, Susie e Ralsei Dark World agora são carregados durante a transição. A borda também passa ao preto imediatamente ao preparar essa sala, em vez de manter a borda da escola durante um fade de 30 quadros.

Os contratos locais verificam a seleção preta imediata e a proteção do shader. O teste definitivo do watchdog GXM depende de repetir no Vita o percurso Town → Castle Town → restaurante → café.
