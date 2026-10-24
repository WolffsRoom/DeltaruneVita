# GXM25 — verificação visual local dos shaders via Vita3K

Data: 13/10/2026. Vita3K 0.2.1 build 4124. Captura real da janela em 960×544.

## Resultado
**Sete dos sete shaders experimentais compilaram e foram vistos efetivamente desenhando pixels no emulador.**
O log informou shader_init ready=1 mask=0x7F. A cena-laboratório GXM25 apresentou os frames 0 e 120, sem iniciar GameMaker/Runner ou OpenAL. Captura original e captura identificada estão na mesma pasta da VPK GXM25.
Isso valida visibilidade do fragment GXP no ambiente emulado, NÃO equivalência pixel-a-pixel com o GLSL original nem execução dentro de um capítulo real no Vita físico.

| Posição | Shader | Efeito visual observado | Erro absoluto RGB médio vs original | Pixels alterados (diferença maior que 12) |
|---|---|---|---:|---:|
| 0 | Original | Gradiente colorido com metade direita semitransparente | — | — |
| 1 | shd_grayscale | Gradiente cinza | 46,3 / 255 | 100% |
| 2 | shd_invert | Cores invertidas | 106,6 / 255 | 100% |
| 3 | shd_grayscale_fade (fade=0,20) | Cores suavizadas / dessaturadas | 36,9 / 255 | 100% |
| 4 | shd_saturation (0,90) | Predominantemente cinza | 42,2 / 255 | 100% |
| 5 | shd_alpha (0,35) | Transparência limitada; imagem mais escura sobre o fundo | 53,4 / 255 | 89,3% |
| 6 | shd_brightness_to_alpha | Luminância convertida em transparência, visual cinza | 49,9 / 255 | 100% |
| 7 | shd_forcefullalpha | Metade direita originalmente translúcida fica opaca | 16,5 / 255 | 50% |

As diferenças foram calculadas em áreas centrais de 172×172 pixels usando ImageChops.difference do Pillow. O comparativo comprova mudança de pixels, não exatidão com a versão PC.

## Arquivos
- Captura original: artifacts/experiments/v0.78-native-gxm/GXM25-Vita3K-Shader-Visual-Test.png
- Captura com nomes: artifacts/experiments/v0.78-native-gxm/GXM25-Vita3K-Shader-Visual-Test-Labeled.png
- VPK do laboratório: artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm25.vpk (DLTAGXM25). GXM24 e GXM23 preservadas.

## Bloqueio na inicialização real de capítulos
No teste do Chapter 5 pela GXM24, a inicialização chega até runner_begin stage=construct_only e o Vita3K apresenta EXCEPTION_ACCESS_VIOLATION (0xC0000005), leitura em 0x0. O erro ocorreu tanto com shaders ativos (mask=0x7F) quanto com shaders forçados desligados. Portanto, essa falha do Vita3K não depende da compilação dos novos shaders; a origem exata permanece incerta. Não houve primeiro frame jogável nem teste de FPS do jogo.

## Segurança, limpeza e escopo
O teste GXM25 foi protegido por marcador temporário gxm-shader-preview.txt e ignorou o Runner. Os arquivos temporários gxm-native-shaders.txt (antes ausente) e gxm-shader-preview.txt foram apagados do emulador; o arquivo de próximo capítulo foi consumido. A configuração original config.yml do Vita3K permanece inalterada e não ficou processo do emulador aberto.

## Próximos passos
Validar os sete shaders no Vita físico durante draw calls de GameMaker; comparar com Experimental-GL e adicionar samplers/uniforms vetoriais para os efeitos ausentes, sobretudo shd_shoujo, paletas, CRT e reflexos do Chapter 5. A tela do laboratório demonstra os sete efeitos, mas não os shaders complexos restantes.
