# DELTARUNE Vita v0.57

Esta versão consolida o trabalho realizado desde a v0.52. As versões v0.54, v0.55 e v0.56 foram mantidas como builds internas de teste.

## Principais mudanças

- cache e carregamento de texturas revisados para reduzir travadas e manter fontes e cenários;
- streaming de música e cache de efeitos sonoros revisados;
- áudio liberado somente depois do primeiro quadro visível nos menus de capítulo;
- editor de posição e tamanho dos controles touch;
- volumes Mestre, Efeitos Sonoros e Música, além do modo para desabilitar áudio;
- navegação touch no Game Settings e sons próprios da interface;
- troca manual de bordas restaurada com `R + D-Pad`;
- saves movidos para `ux0:data/deltarune_saves/`;
- Dev Log com versão da build, tempos de sala, scripts GML e métricas de desempenho;
- suporte ao `draw_path` usado pela sala original Cyber Music Bullet do Chapter 2;
- novos parâmetros `devmode` e `showsettings` no `config.ini`.

## Configurações avançadas

```ini
devmode=0
showsettings=1
```

`devmode=1` mostra Debug Dev e o navegador de salas. `showsettings=0` desativa completamente o menu aberto pelo Select.

## Nota técnica

Foi feita uma tentativa de substituir o backend `legacy-gl` por uma implementação OpenGL mais recente. Muitos conflitos gráficos e crashes foram identificados, então esta versão permanece no caminho `legacy-gl`, atualmente mais estável no PS Vita.

Esta ainda é uma versão em desenvolvimento. Bugs, quedas de desempenho e crashes podem ocorrer. Ao registrar um problema, anexe `ux0:data/deltarune/deltarunevita/butterscotch-probe.log`.
