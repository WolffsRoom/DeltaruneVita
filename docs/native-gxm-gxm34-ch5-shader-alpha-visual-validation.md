# GXM34 — validação Chapter 5 no Vita3K

Data: 13/10/2026. Workspace: C:\Users\wolff\Documents\SDKVita\DeltaruneVita.

## Comparação da mesma sala — room_town_mid (índice 22)

- GXM30 RGBA4444 antiga: árvores magenta, rua marrom, sprites rosa/ciano e opacidade incorreta, inclusive com shaders desligados.
- GXM30 RGBA8888: rua roxa, árvores alaranjadas, transparência preservada. Esta é a referência de cor do TEXEL, não uma execução Modern-GL.
- GXM31 (intermediária) ainda trocava vermelho e alfa; não deve ser usada.
- GXM32–GXM34: o formato GXM U4U4U4U4_ABGR passa a receber nibbles na ordem R<<12 | G<<8 | B<<4 | A. Captura real confirmou rua (119,85,136) contra RGBA8888 (115,89,136), árvores (255,187,0) contra RGBA8888 (255,177,0). Quantização para 4 bits explica as pequenas diferenças.

## Shaders, uniformes e alfa

- GXM33: correção de dupla multiplicação do alfa nos GXP grayscale_fade, saturation, alpha, brightness_to_alpha, forcefullalpha. Mantém a saída RGBA pré-multiplicada do NanoVG/GXM.
- GXM34: novos vínculos para texturas auxiliares do GameMaker com texture_set_stage(1..3), capturando os IDs por draw call adiada antes de cada flush GXM.
- shd_shoujo (tag10): original GML Shader ID55, com sampler de gradiente 128x128, bolhas 256x256, estrelas 256x256 e base TEXUNIT0; time e appSurfWidth são preservados por shader.
- shd_lut (tag11): original GML Shader ID14, LUT 512x512 no estágio 1, strength e lookup 8x8x64. obj_lutshader volta a executar quando o GXP LUT está disponível.
- A GXM34 compilou 11/11 GXP candidatos no Vita3K (mask=0x7FF). No Chapter5 intro, logs confirmaram shd_shoujo modo runtime e stage1=6, stage2=5, stage3=7. Na room_town_mid, logs confirmaram shd_lut ativo e sampler1 imagem5 de 512x512, além de shadowblend, forcecolour e forcefullalpha.
- A cena real room_town_mid exibiu cores normais com os shaders ativos. Diferença média vs GXM32 shaders OFF: 0,148 RGB de 255 por canal; 0,663% dos pixels mudaram mais que 12 níveis. Como há animação do personagem, isso não prova equivalência perfeita do LUT ou de cada shader.

## Tentativa Modern-GL

O mesmo data.win e mesma sala22 foram testados no Vita3K com o app DELT00001 em renderer=modern-gl e dev_start_room=room_town_mid. O log confirmou RENDERER_ACTIVE=modern-gl e ROOM_CHANGE para room_town_mid, mas o app ficou próximo de 1 FPS e a janela permaneceu preta após o primeiro frame. Modern-GL ainda não forneceu uma imagem estável para comparação pixel a pixel. A referência RGBA8888 usada no comparativo é do GXM, não Modern-GL.

## Arquivos de comparação

- artifacts/experiments/v0.78-native-gxm/GXM34-Vita3K-Ch5-TownMid-Old-vs-GXM34-vs-RGBA8888.png: tripla imagem 2880x604.
- artifacts/experiments/v0.78-native-gxm/GXM34-Vita3K-Ch5-TownMid-ShoujoLUT-ShaderON.png: GXP ativo no gameplay da cidade.
- artifacts/experiments/v0.78-native-gxm/GXM34-Vita3K-Ch5-Shoujo-Intro-RealGame.png: GXP shoujo ativo em trecho da logo.
- artifacts/experiments/v0.78-native-gxm/GXM32-Ch5-TownMid-Fixed-RGBA4444-ShaderOFF.png: comparação sem shaders.
- artifacts/experiments/v0.78-native-gxm/GXM30-Vita3K-Ch5-TownMid-RGBA8888-NoShaders.png: baseline de cores.
- artifacts/experiments/v0.78-native-gxm/ModernGL-Vita3K-Ch5-TownMid-SameRoom.png: tentativa Modern-GL sem quadro estável.
- artifacts/experiments/v0.78-native-gxm/GXM34-Ch5-Color-Correction-Pixel-Validation.json: medições.

## Builds e limitações

- VPK final experimental: artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm34.vpk, Title ID DLTAGXM34. GXM30–33 e outras builds preservadas.
- Testes de contrato: 73 callbacks e regressões GXM34 PASS; VPK compilada via WSL/VitaSDK.
- Ainda NÃO estão portados todos os shaders do jogo: shd_castlereflect e shd_castlereflect_layerblend continuam pendentes, entre outros.
- Testes gráficos ocorreram em sala real por salto DEV, sem toda a progressão normal. A imagem da introdução não foi comparada no mesmo frame com o GLSL original; não afirmar fidelidade integral.
- Vita3K usou mute opt-in temporário para evitar a falha de OpenAL na criação do Runner; o teste fica sem áudio. Vita físico ainda não testado; desempenho 60 FPS não garantido.
- Após o teste, restaurar config.ini e remover marcadores temporários do filesystem virtual do Vita3K.
