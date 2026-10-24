# Fontes — linha inferior dos diálogos (v0.77-14)

Os balões de inimigos mostravam caracteres com uma linha horizontal ausente. O defeito era mais visível nos Chapters 2–5, mas o caminho afetado era compartilhado por todos os capítulos.

As UVs apontavam para as bordas exatas do retângulo do glifo. Em texturas com filtro nearest, diferenças de rasterização no Vita podem fazer o fragmento final escolher a linha seguinte do atlas ou omitir a última linha. Agora o primeiro e o último pontos usam os centros dos texels: `origem + 0,5` e `origem + tamanho - 0,5`.

O quad continua com a largura e altura originais. Avanço, kerning, escala, alinhamento e distância entre linhas não mudaram. A regra é compartilhada por fontes FONT e `font_add_sprite`, tanto em `draw_text` quanto em `draw_text_color`, nos renderizadores Legacy/Experimental e Modern.

O teste local verifica o contrato nos dois backends e inicia os Chapters 1–5 em ambos, usando os cinco `data.win` preparados. A aparência final do rasterizador GXM ainda deve ser confirmada no Vita.
