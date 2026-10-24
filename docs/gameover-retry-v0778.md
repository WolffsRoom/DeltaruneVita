# Reinício após derrota — v0.77-8

## Diagnóstico

Inspecionados os scripts reais de Chapter 3: `DEVICE_FAILURE`,
`obj_gameover_init`, `scr_tempload`, `scr_load` e `scr_tempsave`.
Continuar usa `filech3_9`, o checkpoint temporário, e faz uma troca de sala;
não precisa reiniciar o executável. O destino é definido pelo próprio save.

O port tinha um escape de tela preta baseado apenas na quantidade de primitivas
desenhadas em `PLACE_FAILURE`. Isso não distingue o fade de Continuar da tela
escura de Desistir. Um botão poderia causar um reinício completo do capítulo.
Agora esse escape exige `DEVICE_FAILURE.EVENT == 28` (Desistir).

Outro problema concreto: o overlay de arquivos consultava o mod antes dos saves
gravados. Um `filechN_X` antigo distribuído junto de um mod podia esconder o
checkpoint ou save atual. Para esses nomes exatos, nos Chapters 1–5, o arquivo
da pasta de saves agora tem prioridade. Sem esse arquivo, permanece o fallback
mod/bundle; assets de tradução mantêm a prioridade anterior. Não cria nem
sobrescreve checkpoints extras.

## Espera reduzida

Depois de selecionar Continuar, o fluxo comum de Chapters 2–5 limita a espera
do estado 7 a 10 passos e o fade branco do estado 9 a 30 passos. O original
usa 30 + 120 passos na primeira tentativa. O incremento do fade acompanha o
novo prazo. Alarme, mudança de estado e `scr_tempload()` continuam no GML.

Chapter 1, diálogos, escolhas, Desistir, Knight e fluxos sem os campos esperados
não são encurtados. A política opera no Vita; no desktop só com o teste ativado.
Não reduz o custo de recriar a sala nem promete esse ganho em sequências de
derrota próprias, como as cenas especiais de Chapter 5.

## Validação

`scripts/test-gameover-retry.py`: passou em Legacy-GL e Modern-GL.
Cada execução cobre 29 estados, três exclusões e cinco leituras/gravações em
pastas de teste isoladas. Também executa Step, Alarm e Draw reais do controlador
do Chapter 3 até o estado que chama `scr_tempload`: 40 passos, fade 1.0000.
O teste interrompe antes de efetivamente restaurar uma batalha.

Ainda necessário no Vita: perder contra Tenna, escolher Continuar e confirmar
o ponto de retomada e o tempo total; depois testar Desistir separadamente.
Não havia log de derrota disponível na pasta indicada nesta rodada, portanto
não se atribui toda a demora relatada a uma única causa.

Marcadores do log: `RETRY=continue_wait_shortened` e
`RETRY=continue_fade_shortened`. Não altera o save permanente nem os dados do
Chapter 3; as mudanças ficam no VPK.
