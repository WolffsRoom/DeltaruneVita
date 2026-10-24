# Correções dos testes de 14/09 à noite

## Crash no navigator

Dump psp2core-1789430997: abort da VM em obj_grassanim_new.Step, na sala room_dw_garden_riverchest. Reproduzido localmente antes da alteração. A instância 103049 referencia TILES_GRASS, ausente nessa sala; 103061 usa TILES_GRASS_1, existente. Consultas de tamanho de tilemap retornavam undefined, convertido em zero. As quatro consultas agora retornam -1 para mapa ausente; dimensões de célula validam também o índice do tileset. Isso segue a implementação oficial HTML5 do GameMaker:
https://github.com/YoYoGames/GameMaker-HTML5/blob/develop/scripts/functions/Function_Layers.js

Não há mudança em div/mod da VM nem remoção de instâncias. Teste local-test/test_river_creation.py: 300 frames GL2, GL3 e Legacy; três casos passaram após a correção. Teste anterior registrou DoRem :: Divide by zero.

## Vídeo Chapter 5

Novo log prova que a reserva inicial de 3 MiB passou, mas uma segunda reserva de 21 MiB falhou (0x80024302). São 24 MiB de buffers externos, além do heap VitaGL. Chapter 5 agora não reserva PHYCONT ao VitaGL; gráficos usam RAM/CDRAM. Chapter 3 mantém seus 4 MiB, sem mudança no Tenna. Isso corrige a distribuição de memória identificada; capacidade física e reprodução precisam ser confirmadas no aparelho. Verificar alloc_gpu_raw, primeiro frame, áudio, final do vídeo e repetição. A mensagem de pular não foi removida artificialmente enquanto o player falha.

## Fala da logo

pauseAll pausava apenas sons existentes, enquanto a música da logo era criada posteriormente e iniciava durante uploads. Agora o áudio fica desabilitado temporariamente, incluindo novos sons, até a primeira apresentação. Nesse momento os ganhos são restaurados e a preferência audioDisabled é respeitada. Não há fade inicial de um segundo sobre a fala. Fades de save/menu permanecem. Confirmar fala completa no Vita.

## Loading de shaders

O overlay usava pipeline fixo, mas herdava programa Modern-GL, unidade de textura e VBO. A barra por clear/scissor independia disso, explicando a diferença entre barra e PNGs. O desenho agora seleciona pipeline fixo/unidade zero/buffer zero e restaura o estado anterior. Upload de PNG preserva o binding da textura. Conferir ligar/desligar shaders nos dois renderers e retorno à sala.

## Limites

Testes desktop não validam SceAvPlayer, áudio de hardware, memória física nem apresentação VitaGL. Ganho de FPS e lojas ausentes da rodada anterior não estão declarados corrigidos. Os dois logs antigos com nomes de renderer/chapter divergentes permanecem separados deste dump.

## Pacote final
A entrega desta rodada é v0.74-7; v0.74-6 foi uma compilação intermediária. Foram aprovados 18 casos locais (3 grama, 3 riverchest, 12 cidade/PNG). O pacote final inclui também o isolamento do VBO no loading.
