# v0.74-5 — criação de instâncias e vídeo

O dump psp2core-1789379955 termina em abort da VM no resto por zero de obj_grassanim_new.Step. Entrada direta em room_dw_garden_floradinnencounter reproduziu o problema no desktop. A substituição da grama usa a struct opcional layername=TILES_GRASS_PINK, ignorada por instance_create_depth. Create procurava a camada errada e obtinha tile_size zero.

As variantes depth/layer agora aplicam a struct entre PreCreate e Create, usando o setter existente, incluindo variáveis internas e referências superficiais. Layer antes copiava depois de Create. Nenhuma mudança na divisão/resto da VM. local-test/test_grass_creation.py completou 300 frames em Modern GL3, Modern GL2 e Legacy sem abort.

No vídeo do jardim, SceAvPlayer falhou com 0x80024302 ao pedir bloco de 3 MiB, havendo somente 2 MiB de PHYCONT fora do VitaGL. Chapter 5 agora usa a reserva de 4 MiB para VitaGL já usada pelo Chapter 3. O restante fica disponível ao player. Decoder e caminho Tenna permanecem iguais. Há menos PHYCONT para atlas; RAM/CDRAM e reservas continuam disponíveis. Reprodução, retorno e repetição precisam de teste no Vita, em Modern e Legacy.

Pendências:
- Ch5-crash-butterscotch-probe.log contém ENTRY=chapter3 e termina em room_ch3_gameshowroom. Não determina a causa do GPU crash relatado.
- Legacy Crashcbutterscotch-probe.log identifica Modern-GL. O dump analisado comprova abort da VM, não falha GPU.
- Basquete: recarga da página 5 (2048x2048) coincide com pico de 916 ms e expulsão de quatro atlas. Também há custo sustentado de submissão (~11 ms) e apresentação (~10 ms). Ganho de FPS não medido nesta versão.
- Lojas: captura registra deferred=0, sem falha de upload identificada. Necessário comparar imagem/save do mesmo estado. Testes de PNG não comprovam correção desse relato.
- Água: usuário confirmou comportamento esperado; sem alteração.

Reteste: grama por transição normal/navigator; vídeo jardim Modern/Legacy; Tenna; basquete com log; lojas com imagem/save; preservar log e dump da mesma execução do GPU crash.
