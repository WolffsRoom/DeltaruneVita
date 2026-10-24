# 0.74-4 — diagnóstico de desempenho do Chapter 5

Logs recebidos em 2026-09-14, produzidos pela versão 0.74-3:

- `butterlog2-room_dw_ralsei_castle_basketball_20260914-023645.log`
- `butterlog3-room_dw_ralsei_castle_basketball_20260914-023704.log`
- `2FPS_butterscotch-probe.log`

## Evidências

Basquete: janelas estáveis de aproximadamente 32–33 ms por quadro, próximas
da cadência de 30 FPS. Há travamentos de 2,4 e 3,47 segundos associados ao
carregamento; a página 5 (2048×2048) foi decodificada/gravada no cache preparado
e expulsou quatro páginas. Uploads posteriores também produziram picos de
119–256 ms. Isso não prova uma queda sustentada a 2 FPS nessa sala.

Flowery (`room_dw_fcastle_flowery`, índice 226): aproximadamente 370 ms no
bloco de renderização contra 2–5 ms em Step. O problema persiste sem uploads
de textura entre quadros e com o menu aberto (Step próximo de zero). Reinícios
do áudio são frequentes, mas o áudio consome aproximadamente 7–33 ms e não
explica sozinho o total. Não foi atribuída a causa ao cache de atlas.

O Draw de `obj_flowery_towery` compõe a frente e o verso da torre a partir
de dezenas de superfícies. O shader cilíndrico usa acos, amostragem adicional
nas bordas e mistura de cores. É um candidato a investigar, não uma causa
comprovada pelos logs disponíveis. O tempo `render_us` inclui composição,
GUI, apresentação e esperas do driver.

## Alterações nesta build

Esta é uma **build de diagnóstico**, sem promessa de ganho de FPS:

- `PERF_RENDER` persiste a divisão por pré-desenho, início de quadro, views,
  composição, GUI, overlay e swap, além do evento Draw individual mais caro.
- `PERF_MODERN` registra lotes/primitivas efetivamente enviados, tempo CPU
  gasto nos uploads do buffer de vértices e nas chamadas de desenho, chamadas
  de destino e quantidade/bytes de cor das superfícies vivas. Profundidade e
  recursos privados do driver não estão incluídos nesses bytes.
- Os contadores de lotes/primitivas Modern-GL existiam, mas não eram
  incrementados. Agora são preenchidos e zerados por quadro.
- O registro persistente acompanha apenas quadros acima de 50 ms, no máximo
  uma vez por segundo. Não introduz glFinish/fences para medir o GPU.

Os tempos de envio são intervalos CPU, podendo incluir bloqueios do driver;
não são medições diretas de execução GPU. Contadores Modern começam em
beginFrame; o pré-desenho tem seu tempo separado em `pre_us`.

## Validação

`local-test/test_modern_performance_rooms.py`: quatro casos de 600 quadros,
basquete e Flowery em GL2/GL3. Verifica saída normal, ausência de falha de
compilação/fallback e contadores de envio não zerados. Não reproduz o save
completo nem valida FPS no Vita. Logs em `local-test/logs/performance-rooms/`.

Resultado: 4/4 casos aprovados, compilação Vita Release concluída e CRC do
VPK validado. Manifesto de integridade em `logs/vpk-v074-4-validation.json`.

## Próxima medição no Vita

Instalar `Deltarune-v0.74-4.vpk`, repetir a entrada em Flowery e permanecer
cerca de dez segundos; fazer o mesmo no basquete. Recolher o
`butterscotch-probe.log` normal. As linhas novas permitem distinguir espera
na apresentação, trabalho do Draw, upload de vértices e submissão ao driver.
Não é necessário habilitar o painel de desenvolvimento.
