# v0.75-14 — revisão local dos Chapters 4 e 5

## Evidência dos logs

- O dump mais recente em `logs/chapter-log/geral` foi registrado no Chapter 2 com
  `RENDERER_ACTIVE=legacy-gl` e shaders desativados. O processo estava próximo dos
  limites dos pools de RAM e memória física da VitaGL. Esse caso não demonstra uma
  falha exclusiva do Modern-GL.
- A batalha final do Chapter 5, em `room_dw_fcastle_flowery`, mantém cerca de 36–37
  surfaces e recompõe a torre a partir de vinte camadas 360x360. Os maiores stalls
  do log pertencem a `obj_flowery_towery` e `obj_battlecontroller`.
- A batalha normal em `room_dw_garden_floradinnencounter` continua coberta pelas
  correções de páginas protegidas e da barra de TP introduzidas na v0.75-13.

## Alterações

- As camadas intermediárias 360x360 da torre usam RGBA4444 no Vita. A surface final
  240x720 permanece RGBA8888 para preservar a qualidade da composição acumulada.
- A composição da torre amostra dez das vinte camadas alternadas por frame. O efeito
  mantém toda a circunferência ao longo da animação e reduz pela metade o trabalho de
  amostragem da etapa mais cara.
- As surfaces frontal e traseira da torre são atualizadas a 10 Hz e reutilizadas nos
  frames intermediários. A animação geral do jogo continua na taxa normal.
- A estimativa de economia das camadas intermediárias é de aproximadamente 5 MiB de
  memória de vídeo, além da redução de largura de banda e chamadas da composição.

## Validação local

- Matriz Modern-GL com shaders diferidos, 600 frames por sala, aprovada em quatro
  salas pesadas do Chapter 4 e três do Chapter 5.
- Todos os 14 casos da suíte de shaders dos Chapters 1–5 foram aprovados.
- Os nove casos de regressão do Chapter 5 foram aprovados em Legacy, Modern e Modern
  com shaders.
- Os testes de regras de surfaces, descarte de recursos da GPU e reservas de uniforms
  foram aprovados.
- O VPK Vita compilou e foi empacotado sem erros como v0.75-14.

## Validação necessária no Vita

Os testes locais validam fluxo, shaders, regras e compilação, mas não reproduzem os
limites físicos da GPU do Vita. A batalha final deve ser conferida visualmente para
avaliar a suavidade da torre e medir FPS/memória. Uma sessão longa em Modern-GL ainda
é necessária antes de afirmar que não há GPU crash em qualquer sala.
