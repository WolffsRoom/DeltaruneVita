# Progresso do port

## Primeiras tentativas

O trabalho começou com YoYo Loader e uma base de SoLoader. O jogo chegava à LiveArea, mas congelava o Vita com uso alto de CPU e GPU. Foram adicionados logs por etapa para separar problemas do carregador, do parser e da renderização.

O YoYo Loader serviu para validar a organização dos arquivos e os requisitos do Vita, mas não foi suficiente para executar esta versão do runner do GameMaker. A partir daí o port mudou para o Butterscotch.

## Leitura dos capítulos

Os arquivos `chapter0.wad` até `chapter5.wad` foram analisados e o `assets/game.droid` de cada capítulo passou a ser preparado separadamente. O parser do Butterscotch conseguiu ler os principais chunks do GameMaker, incluindo salas, objetos, scripts, fontes, páginas de textura e bytecode.

O carregamento passou a ser preguiçoso para salas e texturas. Isso evitou colocar todos os dados dos capítulos maiores na memória de uma vez.

## Runner jogável

Foi criado um executável Vita com:

- VM do Butterscotch;
- loop de step e draw;
- filesystem separado para dados e saves;
- áudio temporariamente substituído por um backend vazio;
- teclado virtual mapeado para os botões do Vita;
- log persistente em `ux0:data/deltarune/butterscotch`.

O seletor chegou a criar a primeira sala, mas os primeiros builds caíam antes do primeiro quadro.

## Diagnóstico dos crashes

Os dumps mostraram três problemas principais:

1. Pool gráfico pequeno para as primeiras páginas de textura.
2. Stack padrão de 256 KiB insuficiente durante a preparação dos shaders.
3. Uso do modo imediato de OpenGL pelo renderer legado.

O pool do VitaGL passou para 64 MiB e a stack principal para 4 MiB. O compilador runtime de shaders foi colocado em modo conservador durante os testes.

## VitaRenderer

O ponto decisivo foi substituir as chamadas `glBegin/glEnd` do renderer legado por client arrays e `glDrawArrays`. Primeiro foram migrados sprites e retângulos. Depois foi criada uma ponte para converter também fontes, tiles, linhas, triângulos e blits de surfaces.

Na versão `00.17`, o seletor de capítulos apareceu e funcionou no Vita real. O menu respondeu aos controles e as páginas de textura de 256×256 e 2048×2048 foram carregadas com sucesso.

## Troca de capítulos

O seletor usa a função `game_change` do GameMaker. O Butterscotch registrava a solicitação, mas o main do Vita não tratava a mudança, por isso a tela ficava em `Initializing Chapter`.

A versão `00.18` grava o capítulo solicitado, reinicia o próprio eboot e abre diretamente o `game.droid` correspondente. O reinício libera os recursos do seletor antes de carregar capítulos que passam de 200 MB.

O `param.sfo` também passou a usar `ATTRIBUTE2=12`. O campo foi conferido no VPK final.

## Próximos pontos

- validar a entrada nos capítulos 1 a 5;
- corrigir builtins ainda não implementados conforme aparecerem no log;
- medir o uso de memória nos capítulos maiores;
- implementar áudio;
- revisar surfaces, shaders e efeitos usados durante o gameplay;
- testar saves e transições entre capítulos.
