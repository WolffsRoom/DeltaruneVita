# Chapter 5 — correções após v0.74-16

## Evidência revisada

Os anexos contêm relatos e hipóteses anteriores. Os arquivos originais contradizem
parte dessas hipóteses: o vídeo inglês abre e decodifica; o log curto de
floradinnencounter registra 32,04 e 22,99 FPS, não 19,94 FPS, e não menciona
`obj_CH5_enemybattlecontroller`. No armário, `obj_gardenlight_Draw_0` consome
30,54 ms e 66.725 operações no frame amostrado. A falta de `tilemap_get_frame`
é real, mas sozinha não explica esse custo.

## Mudanças

- Vídeo: desenha na texture unit 0, usada pelo sampler, e restaura texturas,
  programa, buffers, blend, atributos e unidade ativa. O mesmo código de desenho
  está em `vita_video_draw_gl.h`, incluído no executável Vita e na regressão desktop.
  A orientação especial da introdução permanece separada da superfície GML.
- `video_ch5/watched` recebe o mesmo tratamento Vita de `VHS/watched`. O código
  original `obj_ch5_DW01_Step_0` confirma que 1 é um marcador de reprodução
  interrompida, escrito antes do vídeo e limpo ao término; não indica progresso.
  O fallback inglês existente permanece intacto.
- Tilemaps: implementado `tilemap_get_frame`, relógio de animação e resolução da
  tabela de frames tanto em `draw_tile` quanto no desenho da camada. ID inválido
  retorna -1. Índices inválidos não acessam a tabela de animação.
- Iluminação: equivalente nativo do Draw de `obj_gardenlight`, sem reduzir luzes,
  opacidade, frequência ou raio. Só é aceito o bytecode de 936 bytes com FNV-1a
  `0daeb6e6` após a resolução VARI/FUNC da VM (original no arquivo: `ea48eaf3`).
  Outros códigos/mods continuam pelo GML. A comparação local cobre 11 luzes em
  três opacidades: 33 casos e 456 chamadas de desenho idênticas, incluindo estado
  final de cor/alpha. Medição desktop inicial: GML 33.429 us, nativo 453 us no
  conjunto de casos; isso não é uma medição de FPS do Vita.

## Validação e limites

`local-test/test_ch5_fixes.py`: vídeo por leitura de pixels/estado, builtin e
animação dos tilemaps, equivalência GML/nativo em Legacy, Modern GL3 e Modern GL2.
O teste de estado falhou antes da restauração dos atributos e passou depois;
o teste de tilemap falhou antes da implementação da builtin.

O teste desktop utiliza uma textura RGBA conhecida no lugar do quadro decodificado;
não executa SceAvPlayer nem valida inicialização, áudio ou GPU do Vita.

TP duplicado e desaparecimento das pétalas ainda precisam de reprodução com o
save da sessão. Não foram removidos objetos, escondidas barras ou fixadas páginas
de textura com base apenas nas hipóteses dos anexos. O salto direto de sala é
insuficiente para reproduzir todos os estados de história/batalha.

VPK: `artifacts/current/Deltarune-v0.74-17.vpk`, 5.610.569 bytes, 171 entradas;
integridade ZIP, eboot.bin e param.sfo verificados. SHA-256:
`420a6a331781b08200f49e2a44800c1980e8dc676a4b208b8162ce7ae254a417`.
Build concluído com saída 0, backup `artifacts/Backups/v0.74-17_20260916-130701`.
Total: nove cenários novos, 12 Town/PNG/transições e três de fotografia passaram.

Teste no Vita: iniciar pelo MP4 de introdução, reproduzir o vídeo do jardim após
a fala da Susie, medir FPS no armário e repetir a batalha/troca de cenário.
Preservar os MP4 convertidos já instalados em chapter5/vid; este VPK não os inclui.
`CH5_SCENE_STATE` registra contagem de instâncias TP, máscara de views e presença
de pétalas, somente em mudanças de estado (amostragem a cada 120 frames).

Referência de tilemaps: GameMaker-HTML5
[Function_Layers.js](https://github.com/YoYoGames/GameMaker-HTML5/blob/develop/scripts/functions/Function_Layers.js)
(UpdateLayers e tilemap_get_frame) e
[yyRoom.js](https://github.com/YoYoGames/GameMaker-HTML5/blob/develop/scripts/yyRoom.js)
(framedata[tileindex * frames + frame]).
