# GXM29/GXM30 — validação dos shaders do Chapter 5 no Vita3K

Data: 13/10/2026. Emulador: Vita3K 0.2.1 build 4124-29ffbcfe, backend OpenGL, captura 960x544.

## Método

A GXM28 original iniciou Chapter5 e registrou mask=0x1FF, mas o Vita3K terminou com EXCEPTION_ACCESS_VIOLATION 0xC0000005 dentro da criação do Runner, após consultas do OpenAL. Foi criada GXM29 experimental, Title ID DLTAGXM29, com workaround explicitamente opt-in via gxm-emulator-mute.txt. Com ele, o Runner abriu e apresentou gameplay real, sem áudio no emulador. A GXM30 experimental, Title ID DLTAGXM30, acrescentou gxm-emulator-room.txt, que solicita uma troca de sala após o primeiro frame e depois do encerramento do comando GXM. Isso permite inspecionar cenas GameMaker e draws reais sem percorrer a introdução inteira. A GXM28 e a versão VitaGL não foram sobrescritas. O workaround não se ativa no Vita físico sem marcador.

## Resultados

| Cena real | Shader ligado | Observado |
|---|---|---|
| room_intro_ch5 (247), GXM29 | mask=0x1FF, shd_shoujo ID55 requisitado | A cena e borda aparecem; efeito shd_shoujo **não funciona**: backend=fixed_gxm_untranslated, dependente de texGradient, texBubble, texStars e uniforms time/appSurfWidth |
| room_town_krisyard (18), GXM30 | 37 forcefullalpha, 25 shadowblend, 41 forcecolour | 400 draw calls e uniforms, mas área do jogo fica preta ao entrar via salto; imagem idêntica com shaders OFF (comparação SHA256 exata). Isso indica que a tela preta não pode ser atribuída só aos shaders; possivelmente faltam estado e variáveis de progressão |
| room_town_mid (22), GXM30 | shd_forcefullalpha, shd_shadowblend, shd_forcecolour executados; shd_lut não traduzido | CENÁRIO E PERSONAGEM VISÍVEIS, inclusive com shaders ligados. Execução GXP e atualização de uniforms comprovadas por log. Comparação ON/OFF: 0,944% dos pixels na área central diferem >12 níveis, concentrados no personagem; pode refletir animação/timing, não é prova de equivalência do shader com GLSL original |
| room_dw_castle_town (54), GXM30 | mask=0x1FF, sem shader_room na cena analisada | CENÁRIO e personagens visíveis, iluminação muito escura. Nenhum shader customizado confirmado nesse trecho, portanto esta captura não valida os shaders de reflexos |

## Arquivos de captura (todos em artifacts/experiments/v0.78-native-gxm)

- GXM29-Vita3K-Ch5-Intro-Real-Gameplay.png
- GXM30-Vita3K-Ch5-Town-ShaderON-Gameplay.png
- GXM30-Vita3K-Ch5-Town-ShaderOFF-Gameplay.png
- GXM30-Vita3K-Ch5-TownMid-ShaderON-Gameplay.png
- GXM30-Vita3K-Ch5-TownMid-ShaderOFF-Gameplay.png
- GXM30-Vita3K-Ch5-TownMid-On-vs-Off.png
- GXM30-Vita3K-Ch5-CastleTown-ShaderON-Gameplay.png

## Conclusão técnica

- O compilador de shaders, o binding do programa GXP e a seleção em draws GameMaker estão funcionando emulado: mask=0x1FF e logs de shd_shadowblend/shd_forcecolour/shd_forcefullalpha.
- Ainda NÃO foi possível comprovar a fidelidade visual integral dos shaders. As diferenças ON/OFF observadas na sala22 foram discretas e concentradas no personagem.
- A logo usa o shd_shoujo, que NÃO está portado (três samplers auxiliares) e fica no fixed GXM.
- shd_lut também está no fallback, e os reflexos shd_castlereflect/layerblend continuam pendentes.
- Um salto direto ignora scripts/flags de progressão: não considerar tela preta de room18 ou ausência de certos efeitos como comportamento definitivo numa partida normal.
- As medições de FPS do Vita3K foram feitas sem áudio e com teleporte para as cenas; não extrapolar para o FPS real do Vita físico.
- Próximas etapas de engenharia: múltiplos samplers GXM para shd_shoujo e shd_lut, e comparação sincronizada por frame com Experimental-GL, além de verificar entrada normal nas salas na Vita física.

## Limpeza e isolamento

- Marcadores de teste no filesystem virtual do Vita3K são temporários, removidos ao finalizar a análise.
- Configuração original do emulador não foi alterada; somente .tmp/gxm25-vita3k-test-config.yml temporário.
- GXM29 e GXM30 são VPKs de laboratório separadas; devem ser usadas com dados legítimos, não consideradas releases validadas no hardware.
