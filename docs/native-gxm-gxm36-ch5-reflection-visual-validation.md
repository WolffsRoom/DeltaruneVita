# GXM36 — Reflexos nativos Chapter 5: validação no Vita3K

Data: 2026-10-13. Local: C:\Users\wolff\Documents\SDKVita\DeltaruneVita.

## Histórico e referência
- GXM30 RGBA4444 mostrava magenta/ciano e transparência incorreta mesmo com shaders OFF.
- GXM32 corrigiu o empacotamento 16bpp para R<<12 | G<<8 | B<<4 | A, validado em room_town_mid contra o caminho RGBA8888 com diferenças de quantização de 4 bits.
- GXM34 implementou shd_shoujo e shd_lut com quatro/dois samplers; 11/11 GXP no Vita3K, shaders reais na intro e em room_town_mid. O Modern-GL DELT00001 alcançou a mesma sala, mas ficou com imagem preta a ~1 FPS no Vita3K; não existe screenshot Modern-GL estável para comparar fielmente pixels.
- GXM35 e GXM36 preservam essas correções.

## Novo trabalho
- shd_castlereflect, ID 48, tag GXP 12: amostragem, shadowheight, pos_y, fade vertical, transparência NanoVG/GXM, cuidado com variável não inicializada do GLSL original.
- shd_castlereflect_layerblend, ID 6, tag GXP 13: amostragem base + u_object_surface (TEXUNIT1) + u_mask_surface (TEXUNIT2), quatro vec4 UV/bounds vindos de GameMaker e preservados por draw call do NanoVG (renderização adiada).
- Passagem de 16 floats auxiliares por draw call, com upload independente gameVectors no fragment GXP. Uniforms por asset com handles exclusivos.
- Laboratório novo controlado por marcador gxm-reflect-preview.txt, não inicia Runner nem OpenAL; compara shaders ON/OFF com exatamente as mesmas entradas.
- GXM36: amostragem de até 32 writes dos uniforms de reflexo no Runner real, com logs pouco frequentes.

## Validação da VPK e emulação
- Build separada: artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm36.vpk, Title ID DLTAGXM36, 57 arquivos ZIP, CRC íntegro, via WSL VitaSDK.
- scripts/test-native-gxm-probe-contracts.py: PASS 73 callbacks + todas regressões GXM36.
- Vita3K compilou 13/13 programas: shader_init requested=1 ready=1 mask=0x1FFF result=0x00000000.
- Chapter 5 room_dw_fcastle_final_save, índice 225 (entrada DEV após primeiro frame): o jogo requisitou IDs 48 e 6 no runtime, sem shader_fallback; samplers u_object_surface TEXUNIT1 = surface 640x480 e u_mask_surface TEXUNIT2 = surface 320x240.
- Uniforms GML reais observados: shadowheight=160.0; pos_y=224.0/271.0; objectUV, tileUV, maskUV e bounds=(0,0,1,1). São parâmetros válidos; os dois caminhos estão ligados.

## Comparativo visual do laboratório (GXP ON vs OFF)
Ambiente 960x544, oito painéis, textura-base e máscaras idênticas, mesmos 1200 frames por ciclo:
- Painéis sem shader, #0, #3, #4 e #7: 0% de pixels com diferença RGB superior a 12/255.
- shd_castlereflect nos painéis #1 e #5: 100% de pixels alterados. Diferenca RGB absoluta média de 94.85/255 e 94.86/255, respectivamente.
- shd_castlereflect_layerblend no painel #2: 6,14% dos pixels alterados, diferença absoluta média 2.41/255.
- shd_castlereflect_layerblend no painel #6: 100% dos pixels alterados, diferença absoluta média 65.18/255.
Isso prova rasterização GPU real com seleção GXP, sampler extra, uniforms, máscara e transparência. Não prova identidade pixel-a-pixel com o GLSL original no Modern-GL.

## Sala real e limitações
- GXM35 ON vs OFF em room225: diferença média de RGB apenas 0.0025/255, 16 pixels mudaram >12/255 (0.0052%); ambos mostram cenário e personagem com cores corretas. Essa sala com salto DEV pode não reproduzir uma sequência de reflexo visível ou estados criados na progressão normal.
- GXM36 confirma uniforms não nulos na sala; a posição dos objetos e o estado do jogo podem explicar a diferença mínima.
- Não afirmar visualmente equivalência total com Modern-GL, nem 60 FPS no Vita físico. O Vita3K com mute e salto DEV é um diagnóstico, não validação física.
- Permanecem shaders complexos não portados em capítulos e salas fora do escopo; dois refletores passam no laboratório, mas o jogo completo não possui ainda cobertura total dos SHDR.

## Capturas geradas
- artifacts/experiments/v0.78-native-gxm/GXM36-Vita3K-Reflection-ShaderLab.png
- artifacts/experiments/v0.78-native-gxm/GXM36-Vita3K-Reflection-ShaderLab-OFF.png
- artifacts/experiments/v0.78-native-gxm/GXM36-Vita3K-Ch5-FCastleFinalSave-RealGML.png
- artifacts/experiments/v0.78-native-gxm/GXM35-Vita3K-Ch5-FCastleFinalSave-Reflection-ON-vs-OFF.png
- docs/native-gxm-gxm34-ch5-shader-alpha-visual-validation.md (referência anterior)

## Próximos passos
- Comparar mesmas fases temporais com Modern-GL quando o emulador renderizar com estabilidade, ou usar capturas do Vita físico.
- Testar sequência normal da sala final-save (sem salto DEV) e verificar reflexos conforme GML ativa os controladores.
- Auditoria de shader coverage por sala; priorizar novos shaders ainda fixed_gxm_untranslated conforme logs de gameplay e impacto visual, sem desativar efeitos para ganhar FPS.
