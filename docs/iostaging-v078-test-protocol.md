# Protocolo de teste do IOStaging na v0.78

O IOStaging e opcional e nao acompanha o VPK. O jogo deve funcionar da mesma
forma sem CapUnlocker e sem IOStaging. O teste mede se o plugin reduz o custo
de leituras sequenciais sem introduzir corrupcao, travamentos ou falhas de
audio.

## Preparacao

Use o mesmo VPK, cartao, instalacao dos dados, configuracao grafica e save em
todas as rodadas. Ative o Dev Mode para registrar as linhas `IO_PROFILE`.
Altere somente o estado do IOStaging e reinicie completamente o Vita depois
de cada mudanca no `config.txt` do taiHEN.

Nao considere a existencia de um arquivo de configuracao como prova de que o
plugin carregou. Confirme a configuracao do taiHEN e o reinicio do aparelho.

## Matriz A/B

1. Execute tres execucoes frias com **IOStaging desativado**.
2. Execute tres execucoes frias com **IOStaging ativado**.
3. Em cada execucao, teste:
   - video inicial ate o Chapter 0 responder;
   - carregamento do Chapter 3;
   - percurso entre varias rooms do Chapter 5;
   - entrada e transicoes da batalha final do Flowey;
   - continuidade de musica, efeitos e videos.

Registre tempo total, mediana, percentis 95 e 99, maior travada e qualquer
crash, freeze, textura incorreta ou audio interrompido. Compare separadamente
CapUnlocker ausente e presente; nao altere os dois plugins no mesmo passo.

## Criterio de recomendacao

O IOStaging so deve ser documentado como opcional recomendado quando reduzir
a mediana de loading e nao piorar o percentil 99, estabilidade, integridade de
dados ou audio. Resultados inconsistentes ou dependentes do adaptador/cartao
devem permanecer descritos apenas como experimento.
