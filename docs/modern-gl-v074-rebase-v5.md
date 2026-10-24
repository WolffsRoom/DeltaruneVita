# Retomada da v0.74-5

A pedido do usuario, foram restaurados os fontes de runtime da copia research/backup-archive/v0.74-5_20260914-194718. Os tres arquivos que divergiam foram preservados em research/recovery-before-v0745-restore. A comparacao dos diretorios src/vita-probe/source e src/butterscotch/src ficou sem diferencas antes de retomar as correcoes.

O VPK original foi recuperado como artifacts/current/Deltarune-v0.74-5-restored.vpk. SHA-256: 959890d3e7fa4ffe5f53b1c76bc45eb5f4d5ab171e5230381a7d86b6e9a20600. Nao e uma recompilacao.

## Escopo retomado

- Boot e heap PHYCONT permanecem como na v0.74-5. Removida a reinicializacao apos o MP4 que nao resolveu o crash.
- Navigator: getters de dimensoes de tilemap ausente retornam -1; indice do tileset validado. Evita o zero indevido usado por obj_grassanim_new na sala riverchest, conforme a implementacao oficial GameMaker HTML5.
- Logo: novos sons ficam retidos ate a primeira apresentacao; removido o fade inicial de um segundo sobre a fala. Preferencia de audio desativado preservada.
- Loading: isolamento do programa, unidade de textura e VBO restrito ao bloco de alternancia de shaders dos Settings. Funcoes globais de loading e liberacao dos recursos do video retomadas da base.
- Video Chapter 5: MP4 local nao possui audio; manter OpenAL permite ao GML tocar ch5_intro_audio.ogg. Nao iniciar worker de audio AvPlayer para esse filme.

## Dados de video separados do VPK

Os logs da v0.74-5 registram alocacoes de 3 MiB e 21 MiB; a segunda falha. Em vez de alterar novamente o heap de boot, foram produzidas copias de teste H.264 Baseline nivel 3.1, uma referencia, sem B-frames, CRF 18, sem alterar resolucao ou cadencia. Originais preservados. A recodificacao pode introduzir diferencas de compressao.

As copias ficam em artifacts/testing/v0745-rebase/chapter5/vid, com instrucoes de instalacao. Medicao da memoria real, reproducao, sincronismo, pausa e retorno precisam de hardware. Nao declarar o video resolvido somente pela validacao do arquivo.

## Validacao e reteste

Executar as regressões river-creation, grass-creation e modern-town no local-test. Conferir boot contra a base, build nativo, integridade do VPK e decodificacao completa dos MP4. No Vita: boot pelo LiveArea, fala da logo, navigator riverchest, alternancia de shaders e video do jardim com os novos dados. Registrar o resultado com o numero exato do VPK.

A causa interna dos GPU crashes das versoes posteriores nao foi comprovada; a base foi restaurada por orientacao do usuario. Nao publicar a pre-release com alegacao de estabilidade sem esse reteste.

Validacao local concluida em 2026-09-15: river-creation (3), grass-creation (3)
e modern-town (12) passaram. Os MP4 EN/JP decodificaram integralmente e mantiveram
1280x720, 728 quadros e 30.333333 s. Pacote local de dados:
`artifacts/testing/Chapter5-video-test-v074.zip`; os originais foram preservados.
O VPK v0.74-14 inclui a retomada e os ajustes descritos em
[ch3-v074-review.md](ch3-v074-review.md). Hardware ainda pendente.
