# Chapter 3 — relógio musical e entrada da batalha (v0.77-10)

## Evidência

Os logs de 2026-10-11 às 21:44, 21:47 e 21:49 pertencem à mesma sessão,
dividida ao abrir as configurações. Na segunda parte, o áudio recebe seeks
de 76,924 e 181,756 segundos. As faixas locais tenna_battle e
tenna_battle_guitar têm 7.939.459 amostras a 48 kHz (165,405396 segundos).

O OpenAL repetia o áudio, mas getTrackPosition somava todas as amostras
reproduzidas sem retornar ao início da faixa. O GML do ritmo usa essa posição
para chart_start, seleção das notas e sincronização das duas faixas. O seek
fora da faixa falhava e voltava ao começo, mas continuava reportando a posição
solicitada. Isso explica uma divergência real nas rodadas posteriores; os logs
não registram botões ou acertos individualmente para provar todos os sintomas.

## Alterações

- Posição de streams em loop limitada pelo comprimento exato em amostras.
  A contagem da fila permanece cumulativa; a preparação antecipada do próximo
  loop não adianta o relógio audível.
- Seek de loops normalizado para a faixa, entradas inválidas limitadas e
  falha de seek sincronizada com o retorno real do decoder ao início.
- Preparação de uma lista pequena de sprites dos personagens/interface ao
  entrar em room_dw_snow_zone_battle. Sem fixar novas páginas na memória ou
  carregar todos os ataques do chefe.

## Validação e limites

scripts/test-audio-stream-clock.bat valida cinco loops, passagem pelo fim da
faixa e seis seeks, incluindo os valores dos logs. O áudio obtido após seek
é comparado com decodificação sequencial das duas faixas reais. Passou.
O teste cobre o relógio compartilhado e Vorbis; não simula a saída OpenAL do Vita.

As pausas incluem 164/172 ms em battlecontroller/herokris com aumento do atlas,
e 365 ms em tenna_tv_time. Há ainda pico resumido de 1,602 s sem detalhamento
do frame. A preparação limitada não resolve todos os recursos de cutscenes.
Nas rodadas posteriores o log também chega perto do limite de VRAM; por isso
não foi adicionado preload de todo o chefe. Fluidez e acertos nas rodadas 2/3
ainda exigem confirmação no aparelho, no mesmo modo Experimental + shaders.

O pacote de teste gerado é Deltarune-v0.77-10.vpk (5.645.489 bytes, 171
entradas, CRC íntegro), SHA-256
2794b769fa88a093c485604185dba89f8c43ba70a814af0019671fc9c7fa5206.
