# GXM42 — RGBA4444 exclusivo, Chapter 5 e shaders

Data: 14/10/2026. Referência visual: imagem Modern-GL enviada pelo usuário, com folhas rosa/alaranjadas, sombras e rua roxo-escura.

## Modificações
- Base GXM41 preservada; novo Title ID DLTAGXM42, 13 shaders GXP mantidos (shoujo, LUT, sombras, reflexos, entre outros).
- Novo arquivo experiments/native-gxm/rgba4444_native.h para empacotamento da imagem decodificada (R<<12 | G<<8 | B<<4 | A). Sem premultiplicação no CPU; fragment GXP aplica o blend.
- Texturas de atlas do data.win, spritefonts/fontes, PNG das bordas, ícones e sprites dinâmicos de surfaces usam RGBA4444. Não existe fallback para RGBA8888/BC3 no upload das texturas de jogo, nem override pelo arquivo gxm-texture-mode.txt.
- O menu nativo mostra RGBA4444 (fixed) e ignora controles de alternância de formatos. Shaders continuam configuráveis.
- Render targets/surfaces do framebuffer mantêm o formato de escrita suportado pelo GXM, que não é o mesmo que os atlas amostrados do jogo.

## Validação no Vita3K, sala room_town_mid, Chapter 5
- 13 shaders compilados: mask=0x1FFF. Shader calls reais: shd_forcefullalpha, shd_shadowblend, shd_forcecolour, shd_lut. LUT com sampler de 512x512 na unidade 1.
- Com o arquivo de teste gxm-texture-mode.txt=2 propositalmente presente, métricas confirmaram 20 páginas RGBA4444, zero RGBA8888, zero BC3, cache cerca de 12 MiB.
- Cores corretas: rua RGB (119,85,136), folhas RGB (255,187,0). Comparação do GXM42 com o baseline de textura RGBA8888 do GXM30: erro médio cerca de 4,03/255 por canal, esperado na quantização de 4 bits; sem inversão magenta/ciano.
- ON/OFF na área central: diferença média RGB 0,148/255; 0,661% dos pixels >12 níveis, inclusive diferença de animação. Isto comprova seleção dos GXP por log, mas NÃO fidelidade integral ao Modern-GL.
- Capturas no diretório artifacts/experiments/v0.78-native-gxm: GXM42-Vita3K-Ch5-TownMid-RGBA4444-ShadersON.png, GXM42-Vita3K-Ch5-TownMid-RGBA4444-ShadersOFF.png e GXM42-Vita3K-Ch5-TownMid-ON-OFF-Difference.png.

## Referência visual e limite
- A imagem do usuário corresponde a outro enquadramento/estado narrativo e iluminação; amostra de rua na referência RGB (87,52,101) versus início direto de room22 na GXM42 (119,85,136). Não são a mesma coordenada do mundo, portanto a diferença não demonstra isoladamente um erro de shader.
- O Modern-GL DELT00001 no Vita3K executou a mesma room22 via dev_start_room, mas permaneceu em quadro preto perto de 1 FPS; não gerou imagem alinhada estável. Evitar inventar gradação de cores escurecendo a cena globalmente.
- A GXM42 não foi testada no PS Vita físico. Para provar equivalência dos efeitos visuais, comparar o mesmo save, enquadramento e estado de iluminação em progressão normal, sem salto DEV.
- Alpha de 4 bits pode degradar suavidade dos contornos de fontes em comparação com RGBA8888; este é o compromisso do formato solicitado.

## Build
- Deltarune-NativeGXM-v0.78-gxm42.vpk, Title ID DLTAGXM42, 57 arquivos, CRC ZIP íntegro, SHA256 0979782167bc87d7619b67fb05b3b1934d9b647b7848cb934345835e81a8dcc2.
- Compilação pelo VitaSDK via WSL; scripts/test-native-gxm-probe-contracts.py aprovou 73 callbacks e as regressões de textura e shaders. Build GXM41 e anteriores preservadas.
