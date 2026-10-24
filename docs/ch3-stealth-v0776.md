# Chapter 3 — TVs de stealth_d (v0.77-6)

VPK gerado: `artifacts/current/Deltarune-v0.77-6.vpk` (5.642.526 bytes,
171 entradas, integridade ZIP verificada). SHA-256:
`cffb63b8f4b7990162b4ff5ecb06dd9cebf72521a231edf57775d896e6720596`.
O rótulo público interno continua `DELTARUNE VITA v0.77`.

## Evidência do Vita

O final de `logs/chapter-log/chapter 3/butterscotch-probe.log` corresponde ao
build v0.77-5, Experimental-GL com shaders ligados. A queda contínua ocorre
em `room_dw_teevie_stealth_d`: médias de 45,9–51,2 ms por frame (20–22 FPS),
com aproximadamente 29–33 ms no desenho das views e 10–16 ms no Step.
`obj_dw_teevie_tv_screens` aparece repetidamente como o Draw individual mais
caro, chegando a 4,7 ms. Existem cinco painéis ao longo da sala, separados
por aproximadamente 1.000 pixels, cada um com até 18 TVs e seis sprites por TV.

A última sala registrada é `room_dw_teevie_chef`. Seu resumo final fica em
13 ms por frame; há picos de carregamento/diálogo/save, mas o problema
contínuo imediatamente anterior está na sala stealth_d.

## Ajustes

- A VM reconhece getters cujo início é exatamente `push.v self.campo; ret.v`.
  Usa o mesmo campo e a mesma regra de ownership do retorno normal, sem criar
  frame de chamada e alocar locais. Campos ausentes/herdados, outros códigos,
  chamadas com argumentos e tracing/profiling conservam a interpretação.
  O tipo da pilha GML também é preservado. Não depende de IDs de assets.
- O Draw original das TVs em stealth_d tem uma otimização específica de
  visibilidade, protegida por assinatura de bytecode (1.844 bytes,
  FNV-1a `c5e84f91`, após resolução de referências). Calcula a extensão real
  de todos os sprites/posições do painel e usa as matrizes ativas para
  testar se ele está inteiramente fora da imagem, com margem de segurança.
  Não pula Step, animação, entrada, luzes ou lógica de detecção do jogador.
  Mesmo fora da tela, mantém o incremento de timer e o estado final do Draw.
- Painéis parcialmente visíveis executam o Draw original. Shaders ativos,
  perspectiva, rotação de sprites, getters modificados e dados incompatíveis
  conservam o caminho original. Mudanças efetivas de sprite-base também são
  executadas pelo GML. Não cria surfaces nem aumenta o cache de texturas.
- Legacy inicializa `currentShader` como -1, coerente com o pipeline fixo
  inicial; antes esse campo começava incorretamente em zero.

## Validação local

`scripts/test-ch3-stealth-getters.py` reproduz 300 frames com seed fixa e
desloca a câmera ao longo da sala, comparando o caminho original e o
otimizado em Legacy-GL e Modern-GL. Nove capturas por renderer são
comparadas pixel a pixel, inclusive painéis parcialmente visíveis e TVs que
voltam a aparecer. Resultados em `local-test/logs/ch3-stealth-getters/`.

Na rodada de validação, ambos conservaram pixels idênticos e evitaram
1.250 desenhos de painéis em 300 frames. A mediana local caiu de cerca de
13,35 para 7,64 ms em Legacy e de 13,57 para 7,44 ms em Modern.
Esses tempos incluem instrumentação no desktop;
não são uma medição nem previsão de FPS do Vita.

O teste nativo compara oito getters reais com a interpretação, verifica
pilha/contexto e a posse de valores numéricos, booleanos, strings, arrays e
structs. Os testes anteriores de monitores, regiões cinzas e matriz/paleta
de shader também passaram.

A versão russa disponível localmente tem a mesma assinatura do Draw;
ativou o culling e passou a comparação dos oito getters. O Chapter 3 PT-BR
usado no log não está na pasta local de traduções, portanto não foi
validado diretamente. Se seu bytecode diferir, o culling conserva o Draw
original; o fast path de getters continua independente de idioma/assinatura.
Os testes de surfaces passaram em Legacy, Modern e Modern em contexto GL2.

## Teste no hardware

Percorrer stealth_d nos dois sentidos em Experimental/Modern + shaders ON,
observando TVs, luzes, detecção e transição para chef. O log deve registrar
`CH3_TV_CULL=active` quando houver um painel elegível fora da câmera.
Comparar os novos `PERF_SUMMARY` com o baseline de aproximadamente 48 ms.
O ganho real e a estabilidade no Vita ainda precisam dessa confirmação.
