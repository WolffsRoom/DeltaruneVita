# Cobertura de shaders Modern-GL — 2026-09-14

Auditoria do código atual (0.74-4) e teste dos dados preparados dos capítulos.
O backend executa os shaders dos dados do jogo de forma comum aos capítulos;
não existe uma implementação habilitada exclusivamente para Chapter 5.

## Teste de compilação local

Executável desktop atual, Modern-GL com `BS_GL_FORCE_GL2=1`, compilação imediata
de todos os shaders presentes e 300 quadros por capítulo. Cinco casos aprovados,
sem erros de compilação/link nem `MODERN_GL_SHADER_FALLBACK`.

| Capítulo | Shaders do jogo compilados no teste GL2 |
|---|---:|
| 1 | 0 (somente programa padrão do renderer) |
| 2 | 5 |
| 3 | 21 |
| 4 | 41 |
| 5 | 66 |

Inventário completo e resultados: `local-test/logs/shader-audit-gl2/results.json`.
Esses números excluem o programa padrão do renderer; não representam quantidade
de efeitos efetivamente desenhados durante os 300 quadros.

Chapter 4 inclui `shd_prophecy`, `shd_prophecy_legend`, `shd_fountaineffect`,
`shd_fountaineffect_alt`, `shd_fountainsurface`, `shd_tunnel` e variantes,
`shd_ripple`, `shd_steam`, `shd_distortiondonut`, Gaussian horizontal/vertical,
outline, dissolve, perspectiva e troca/ajuste de cores.

## Implementação e limites

- Modern-GL implementa seleção/reset de shader, consulta de compilação,
  uniforms float/int, arrays float e samplers adicionais. `shader_replace_simple`
  utiliza o caminho de shader do runner; a sincronização da DLL auxiliar não
  é necessária aqui.
- No Vita, cada shader é compilado sob demanda pelo compilador VitaGL/SHARK.
  Um sucesso no driver OpenGL do PC não prova sucesso nesse compilador nem
  desempenho adequado na GPU do Vita. A build preserva fallback individual
  para falhas de compilação.
- Camadas nativas `RoomLayerType_Effect` ainda têm TODO no desenho do runner.
  Portanto, compilar `_filter_tintfilter_shader` não comprova que filtros
  automáticos de camada sejam aplicados. Os efeitos acionados explicitamente
  por scripts têm caminho implementado.
- Os testes não percorrem todas as salas, batalhas e estados narrativos.
  Os problemas de desempenho de Flowery demonstram que shader implementado
  e compilável pode continuar caro demais no hardware.

## Prioridades de confirmação no Vita

No Chapter 4: cenas de profecia (múltiplos samplers), fontes/água, túneis e
distorções, steam, Gaussian e dissolve. Comparar visualmente com o PC e
recolher o log 0.74-4 se houver travamento ou queda sustentada. Nos demais
capítulos, confirmar transições e batalhas que acionem os efeitos, além dos
testes de inicialização já executados. Nenhuma alteração de renderer ou novo
VPK foi necessária para esta auditoria.
