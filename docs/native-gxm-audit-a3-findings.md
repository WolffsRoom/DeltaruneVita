# Deltarune Vita — auditoria GXM a2 analisada e correções a3

Data: 13/10/2026. Referência: logs/chapter-log/geral/{summary.log,chapter0.log..chapter5.log}.

## Resultado real a2 no PS Vita
- `AUDIT result=FINISHED chapters_scanned=6 errors=0`, com parse de DataWin e VM bem-sucedidos.
- 1.254 salas catalogadas, 133 shaders SHDR com fonte GLSL/HLSL e sem GXP compilado.
- GPU_TEST BC3 e RGBA4444 PASS significava alocação de textura de amostra, **não** equivalência visual nem execução dos scripts das salas.
- Ch2 estava marcado PASS_STATIC apesar de `PVR page=414 status=missing`; falha de classificação corrigida na Audit a3.

## Correção do arquivo ausente
- O `data.win` original do Chapter 2 contém TXTR[414] como PNG 512x256.
- Vinte itens TPAG referenciam essa página (índices 9538-9557).
- O PVRTexToolCLI está instalado; o PVR base foi regenerado a partir do PNG original com `-f BC3,UBN,sRGB`.
- O parâmetro UBN importa: `-f BC3,UB,sRGB` gera channel type 2; o loader do projeto exige channel type 0.
- Arquivo corrigido: `data/prepared/deltarune/deltarunevita/chapter2/pvr/page_414.bc3.pvr` (512x256, 131140 bytes).
- A auditoria de cabeçalhos do conjunto Ch1–5 passou de 2.858 para 2.859 páginas válidas; a Audit a3 verifica todas as seis fontes.

## Pendência REAL revelada ao cruzar TPAG e PVR
- A entrada TPAG[9557], entre outras, pertence à página 414 e solicita srcX=490 com srcWidth=68, resultando em x=558.
- A imagem original **tem somente 512 pixels de largura**. Esse retângulo extrapola a própria fonte original.
- `scripts/validate-native-gxm-atlas-bounds.py` verifica todos os Chapters 0–5; é esperado identificar Ch2 página 414 como `ATLAS_BOUNDS`, enquanto não houver arte original compatível ou ajustes comprovadamente corretos.
- Não expandir/gerar pixels inexistentes nem modificar o data.win apenas para ocultar o diagnóstico. O overflow dessa referência ainda precisa de investigação de origem.
- Se o Chapter2 carregado no Vita for diferente do preparado no PC, validar também a versão exata do data.win.

## Audit a3
- VPK: `artifacts/experiments/v0.78-native-gxm/Deltarune-GXM-Audit-v0.78-a3.vpk`.
- Title ID `DLTGMAUD3`; não sobrescreve a auditoria a2 e não altera GXM16.
- Relatórios em `ux0:data/deltarune/deltarunevita/gxm-audit-a3/`.
- Alerta `TPAG_OUT_OF_BOUNDS` quando página PVR é menor do que qualquer retângulo referenciado; conta como invalid e WARN.
- Arquivos PVR ausentes passam a contar como problemas, em vez de PASS_STATIC.
- Logs incluem quantidade de entradas TPAG por página e dimensões mínimas requeridas pelo atlas.
- Estados/checkpoints separados da a2, continuação após reiniciar e opção de reset segurando L+R.

## Pendências estruturais fora do escopo da auditoria
- 133 shaders GML sem tradução GXP, efeitos de fog, blend especiais, alpha test por pixel, vertex buffers e vídeo.
- A auditoria estática não percorre a gameplay nem garante FPS, imagens corretas ou estabilidade em todas as salas.
- A GXM16 ainda deve ser validada fisicamente: configura-se para RGBA4444 por padrão para evitar corrupção por upload BC3 linear do gxm15.
- Os dados não são incluídos dentro da VPK: o PVR reparado do Chapter 2 precisa ser copiado para ux0:data/deltarune/deltarunevita/chapter2/pvr/ para testar BC3 real.

## Próxima validação
1. Obter o log da GXM16 e as imagens do Vita para comparar RGBA4444 com Modern-GL.
2. Conferir a origem da TPAG[9557] do Chapter 2 e se pertence a algum mod/idioma.
3. Executar Audit a3 quando útil, recolher `summary.log` e `chapter2.log`.
4. Implementar GXP shader por prioridade e testar salas em hardware, não inferir paridade pelo PASS_STATIC.
