# DELTARUNE Vita v0.57

A v0.57 é uma atualização cumulativa: inclui a base da v0.53 e os ajustes desenvolvidos nas builds internas v0.54, v0.55 e v0.56.

## Estado da revisão

- **Chapter 1:** revisado sala a sala durante uma nova campanha. O percurso principal foi verificado, embora alguma interação opcional ainda possa ter ficado fora e será conferida em uma nova partida.
- **Chapter 2:** recebeu a maioria das correções identificadas até agora, incluindo áudio, batalhas, transições, texturas, menus e desempenho. Algumas salas continuam em observação.
- **Chapters 3 e 4:** inicializam e são jogáveis, mas ainda não passaram pela mesma revisão completa dos dois primeiros capítulos.
- **Chapter 5:** continua sendo o principal desafio de otimização. A região da cidade possui o maior custo de processamento, renderização e memória e ainda pode apresentar FPS baixo ou crash.

## Principais mudanças desde a v0.53

- cache de texturas preparado por capítulo e carregamento sob demanda revisado;
- cache secundário em RAM para reduzir leituras repetidas do cartão;
- proteção de fontes, menus e atlas pequenos durante a troca de texturas;
- streaming de música e cache de efeitos sonoros revisados;
- música liberada apenas depois do primeiro quadro visível, evitando áudio durante a tela de loading;
- sliders separados para Volume Mestre, Efeitos Sonoros e Música;
- opção para desabilitar todo o áudio como alternativa de desempenho;
- loading animado durante a preparação inicial de cada capítulo;
- editor de posição e tamanho dos controles touch;
- correção do joystick touch duplicado que retornava ao layout Android original;
- navegação touch no Game Settings e sons próprios da interface;
- troca manual de bordas por sala usando `R + D-Pad`;
- correção da leitura completa de `borders_config.txt`, mantendo a borda escolhida após a troca;
- perfil de atlas específico para `room_dw_castle_area_2_transformed`, evitando objetos e casas ausentes sem reduzir abaixo da resolução do Vita;
- saves separados em `ux0:data/deltarune_saves/`;
- Dev Log com versão da build, tempos de sala, scripts GML mais caros e métricas de eventos, alarmes, colisões, áudio e renderização;
- navegador de salas separado do Dev Log;
- suporte ao `draw_path` usado pela sala original Cyber Music Bullet do Chapter 2;
- parâmetros `devmode` e `showsettings` no `config.ini`;
- correções de estabilidade nos Chapters 3, 4 e 5;
- tentativa de migração do `legacy-gl` para um backend OpenGL mais recente. A mudança foi descartada nesta versão após conflitos gráficos e crashes.

## Bordas de console

O mod [NXRUNE](https://gamejolt.com/games/nxrune/629072) foi utilizado como referência para estudar a relação entre salas e bordas de console. Nenhum arquivo do mod é incluído nesta release. O sistema do Vita mantém mapeamentos próprios em `borders_config.txt`, que podem ser ajustados manualmente com `R + D-Pad`.

## Configurações avançadas

```ini
devmode=0
showsettings=1
```

`devmode=1` exibe as ferramentas de desenvolvimento e o navegador de salas. `showsettings=0` desativa o menu aberto pelo Select.

## Aviso

Esta é uma versão em desenvolvimento. Bugs, quedas de desempenho e crashes ainda podem ocorrer. Ao registrar um problema, anexe o arquivo:

`ux0:data/deltarune/deltarunevita/butterscotch-probe.log`
