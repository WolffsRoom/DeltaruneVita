# Native GXM28 — Chapter 3, Chapter 5, shaders e FPS

Data: 13/10/2026. Fonte dos testes no Vita físico: logs/chapter-log/geral/Ch3 e Ch5.

## Evidências
- Chapter 3, GXM25, 16:46: Room Navigator de room98 para room108 funcionou até o renderer abortar com unsupported=drawSpritePos_projective_quad. Na primeira sala a VM consumia ~23 ms por frame em eventos e o desenho 15–23 ms, limitando o FPS a aproximadamente 20–23.
- Chapter 5, GXM25, 17:04: compilação GXP ativa mask=0x7F; shader shd_shoujo da logo ainda sem suporte, assim como castlereflect e castlereflect_layerblend. A sala59 acumulou 912 capturas de superfícies e 501 liberações, correlacionadas com FPS abaixo de 10 em alguns trechos. Esses números não identificam sozinhos o script de origem.
- Chapter 0: a versão v0.78 já está no data.win preparado, mas cópias externas antigas instaladas no Vita e traduções podem usar uma atribuição antiga.

## Correções
1. Native sprite_pos: nova função nvgDrawImageQuad em scratch/refs/nanovg-gxm/src/nanovg.c/h desenha dois triângulos com quatro vértices e UVs independentes, mantendo blend, recorte e transformação. Renderer GXM não aborta mais em quadriláteros deformados. Imagens invisíveis por culling incorreto foram resolvidas e testadas em GPU emulada.
2. Local screenshot: artifacts/experiments/v0.78-native-gxm/GXM28-Vita3K-Quad-Visual-Test.png (960x544). Oito variantes de quadriláteros, incluindo distorção, recorte e transparência, foram exibidas. Contagem de pixels coloridos por bloco: 42214,40338,28848,33444,30276,29954,23984,25974. Evidência de render real no Vita3K, não do Runner em gameplay no Vita.
3. Capturas custosas: enquanto o native GXM não possui suporte para o segundo sampler de shd_lut, as passagens Step/Draw do controlador obj_lutshader são puladas APENAS no renderer GXM em Chapters 3 e 5. Essa passagem não produzia efeito LUT porque o shader não estava implementado e exigia capturas/sincronizações custosas; o Modern/Experimental-GL não foi modificado. Não equivale a implementar LUT e não garante FPS.
4. Diagnósticos adicionais: NATIVE_GXM=snapshot_source room=... obj=... registra quem solicita capturas, sem saturar o arquivo; NATIVE_GXM=step_hotspot identifica os quatro scripts Step mais caros a cada 300 frames.
5. Composição Tenna: fundo preto opaco SOMENTE atrás da área central da aplicação, preservando a superfície de jogo e seu canal alfa. Evita que a borda apareça através dos efeitos semitransparentes; aguarda validação no hardware físico.
6. GXP: adicionados shd_shadowblend e shd_forcecolour, equivalentes iniciais ao GLSL-ES do DataWin e usando uniforms preservados por draw call, além dos sete existentes. O Vita3K compilou os nove GXP (mask=0x1FF). A logo shd_shoujo ainda requer samplers adicionais; os shaders de reflexo também não estão portados.
7. Versão do Chapter0: o evento Create de obj_ui_version atualiza _version_text para DELTARUNE VITA v0.78 no runtime GXM, mesmo com data.win externo antigo, sem alterar os dados originais.

## Validação e limitações
- Build separada Deltarune-NativeGXM-v0.78-gxm28.vpk, Title ID DLTAGXM28, WSL VitaSDK; 57 arquivos ZIP, teste CRC passou. SHA-256 64fec20e310165c27420e617773904054e05493a63943056f409656f7f4c38a2.
- Testes de contratos: 73 callbacks e regressões anteriores passaram.
- Teste local: Vita3K renderizou oito variantes da quad scene; compilação GXP registrou mask=0x1FF. Ainda não houve benchmark/validação da GXM28 no Vita físico, nem teste visual dos dois shaders novos durante gameplay.
- O Vita3K havia apresentado violação de acesso ao inicializar o Runner (inclusive sem shader); o laboratório visual não executa Runner, portanto não é um teste de jogo completo.
- A GXM27, GXM26 e anteriores continuam preservadas.

## Testes recomendados no Vita físico
- Chapter 3 room98 -> Room Navigator room108: verificar crash e exibição dos sprites deformados.
- Chapter 5 Tenna: verificar se a área central está opaca e se o brilho continua correto, sem borda aparecendo por trás.
- Chapter 5 Castle Town e demais salas: comparar FPS e observar step_hotspot e snapshot_source. Não interpretar 60 FPS como alcançado.
- Chapter 5: shader_init mask=0x1FF, shader_effect para shd_shadowblend/shd_forcecolour; ainda não esperar shd_shoujo.
- Chapter 0: confirmar DELTARUNE VITA v0.78.
