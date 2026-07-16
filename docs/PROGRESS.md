# Desenvolvimento do port

## Primeiros testes

O projeto começou com YoYo Loader e uma base de SoLoader. Esses builds validaram o VPK, a LiveArea e a organização dos dados, mas o Vita congelava com CPU e GPU em 100%. Logs por etapa mostraram que seria necessário controlar diretamente o carregamento, a memória e o renderer.

## Mudança para Butterscotch

O Butterscotch tornou possível interpretar o bytecode GameMaker dos seis pacotes: o seletor (`chapter0`) e os capítulos 1 a 5. O port ganhou um executável próprio, filesystem separado para assets e saves, entrada do Vita e um log persistente.

O carregamento de salas e texturas passou a ser sob demanda. Isso foi essencial para capítulos maiores, que ultrapassavam o limite normal de memória do Vita.

## Renderer do Vita

Os primeiros quadros falhavam por três motivos principais: pool gráfico pequeno, stack insuficiente e uso de OpenGL imediato pelo renderer legado.

O VPK passou a usar o perfil ampliado de memória, stack principal de 4 MiB e pool VitaGL de 64 MiB. O renderer legado foi adaptado de `glBegin/glEnd` para client arrays e `glDrawArrays`, cobrindo sprites, fontes, tiles, primitivas e surfaces. Esse trabalho levou o projeto de páginas de textura isoladas ao menu e depois ao gameplay.

## Capítulos jogáveis

O seletor original solicita outro executável por meio de `game_change`. No Vita, a troca foi implementada como um reinício limpo do próprio eboot com o capítulo solicitado. Dessa forma os recursos do capítulo anterior são liberados antes do próximo carregamento.

Após correções de builtins, surfaces e inicialização gráfica, os capítulos 1 e 2 foram os primeiros a rodar. O mesmo caminho foi estendido aos capítulos 3, 4 e 5. A opção Change Chapter dentro do jogo retorna ao seletor em vez de encerrar o aplicativo.

## Experiência no Vita

O backend de áudio OpenAL foi ativado e o mapeamento foi ajustado para X confirmar e Círculo cancelar. SELECT abre um menu próprio do port, com touch, idioma/mod, volume e tela. Os controles touch usam sprites já existentes no jogo.

Como as cenas e bordas dinâmicas variam entre capítulos, foi criado um ajuste de posição e zoom com contorno visível. A configuração fica salva em `ux0:data/deltarune/config.ini`.

## Situação atual

A v0.31 é um port jogável e viável no Vita real. O trabalho futuro é de compatibilidade e acabamento: corrigir efeitos específicos que ainda se comportem diferente, melhorar desempenho em salas pesadas, refinar bordas dinâmicas e ampliar a compatibilidade com mods.
