# Chapter 3 e fonte do Chapter 5 — revisão da v0.74

Base: retomada da v0.74-5, preservando a inicialização e a memória do vídeo dessa versão.

O usuário confirmou que os relatos de fotografia, ritmo e `Bugado.png` foram
observados na v0.73; Modern-GL ainda não havia sido testado nesses casos.

## Evidências e alterações

- O jogo de ritmo reproduziu 596 mensagens `PERF_EVENT_SLOW` em 600 quadros
  em Legacy e 606 em Modern no desktop. O diagnóstico agora registra no máximo
  um evento por segundo, contando os suprimidos. Os máximos de Draw continuam
  sendo coletados em todos os quadros. Na repetição, ambos geraram 6 mensagens.
  Isso reduz a escrita de logs; não constitui medição de ganho de FPS no Vita.
- O teste de captura anterior usava uma superfície uniforme. Uma região colorida
  fora do centro revelou falha no recorte com `GLCommon_surfaceBlit`. Superfícies
  já invertem a projeção na escrita; a segunda inversão na cópia selecionava outra
  faixa vertical. A cópia agora conserva as coordenadas, como o caminho GL2.
- `shd_grayscalesand` e `shd_grayscalesand_tolerance` não estavam na lista compacta
  do Legacy no Vita. Foram incluídos; o fragmento original é preservado.
  A compilação em desktop não substitui a validação do compilador de shaders do Vita.
- Nenhuma das 16 fontes do `chapter5/data.win` preparado cobre os acentos
  portugueses consultados (`logs/ch5-font-coverage.log`). O menu utilizava letras
  sem acento e marcas geométricas sobrepostas. O fallback agora usa glifos completos
  de `data/frames/undertale.otf`, gerados por `scripts/generate-settings-latin-font.py`.
  Mantém o avanço da fonte do jogo e escala pela altura de A. Fontes que já tenham
  o caractere continuam usando seu próprio glifo. Conferir alinhamento no Vita.

## Validação

`local-test/test_ch3_reported_rooms.py`: 600 quadros em `room_dw_rhythm`,
`room_ch3_gameshowroom` e `room_board_2`, em Legacy e Modern. Os seis casos
completaram sem crash. Capturas e resultados estão em
`local-test/logs/ch3-reported-rooms`.

`local-test/test_ch3_surfaces.py`: os três caminhos (Legacy, Modern GL3 e
Modern GL2) passaram no recorte fora do centro e na leitura de pixels em cinza
(RGB 92/92/92). A regressão ampliada de Modern também passou nos 12 casos de
`test_modern_town.py`; antes da correção, o novo recorte falhava em GL3.

Build nativo concluído: `artifacts/current/Deltarune-v0.74-14.vpk`. Integridade
ZIP verificada; SHA-256:
`32ce6a361017700375fe072a976f9bab36d86231984d0aa52e964a40a548f843`.
Compilação C/C++ concluída não valida a compilação GLSL no aparelho.

A geração dos glifos é determinística, e os 24 caracteres portugueses testados
são distintos do glifo ausente da fonte. A prévia de fonte isolada não substitui
a conferência do alinhamento no menu do Vita.

### Correção da escala dos acentos após a v0.74-14

A foto enviada pelo usuário confirmou que o fallback ficou grande. O cálculo
anterior confundia altura da célula do atlas com altura visível: no Chapter 5,
o A tem célula 6x13, mas tinta 6x9 com quatro linhas transparentes acima.
O fallback agora é gerado em 12 px (A com tinta 6x9), usa a largura da letra
para a escala e alinha a base descontando o espaço transparente e ascenderOffset.
As escalas horizontal e vertical do jogo são respeitadas separadamente.

`local-test/test_settings_font_size.py` compara os pixels com a fonte real
exportada por `research/InspectSettingsFont.csx`: A idêntico, 6x9 e início em y=4.
O teste também confirma que o cálculo antigo excedia o avanço de sete pixels.
Prévia de palavras completas em `local-test/logs/settings-font-size/preview.png`.

Build dessa correção: `artifacts/current/Deltarune-v0.74-15.vpk`, compilação e
integridade do pacote verificadas. O tamanho visual foi conferido na prévia com
o atlas real; reteste do menu no Vita permanece necessário.

### Fonte menor, cantos do menu e vídeo — rodada posterior à v0.74-15

Rótulos principais reduzidos de 1,84 para 1,60, igualando a escala dos valores
principais à direita. O fallback de acentos continua com as mesmas métricas.
Os cantos estavam com orientação incorreta em Modern: `drawUIIconExt` calculava
os UVs espelhados, mas chamava uma API que sempre usava UVs 0..1. A nova variante
`GLRenderer_drawExternalTextureFlipped` recebe os dois espelhamentos; a API
anterior preserva seu comportamento para os demais consumidores.
O teste de pixels usa quatro cores diferentes e verifica as quatro orientações.

O log do Chapter 5 da v0.74-15 mostra falha de alocação PHY de 21 MiB após um
bloco de 3 MiB, sem primeiro quadro decodificado. O usuário confirmou que instalou
somente o VPK, sem os MP4 convertidos. Portanto esse log não testa os MP4 Baseline
disponibilizados anteriormente. O código de vídeo foi preservado byte a byte em
relação à v0.74-15; o novo pacote local inclui também os MP4 de teste 1280x720.
O áudio separado tocar não comprova que o decoder de vídeo iniciou.

Entrega: VPK v0.74-16 e pacote local
`artifacts/testing/Deltarune-v0.74-16-menu-video-test.zip` com o VPK, os dois MP4
Baseline 1280x720 e instruções. Build nativo e integridade dos dois ZIPs
verificados. Os 12 casos Modern de GL2/GL3 passaram, incluindo os quatro
espelhamentos e a restauração do shader ativo. Vídeo no Vita pendente de reteste
após copiar também os MP4.

O início direto das salas não reproduz a sequência de fogo/gacha de `Bugado.png`
nem a entrada de fotografia durante a batalha. Esses cenários continuam exigindo
validação de sequência; não considerar a ausência de crash como correção visual.

## Teste no aparelho

1. Legacy com shaders ligados: fotografar no Round 2 e verificar a área cinza.
2. Batalha Shutta: fotografar um personagem próximo à borda superior e inferior;
   conferir o conteúdo da fotografia, não apenas a posição da moldura.
3. Repetir em Modern e jogar o início e uma música completa dos Luzianos.
4. Repetir o gameshow no ponto de `Bugado.png` (fogo e cápsulas).
5. Game Settings em português: conferir Á/Ã/Ç/É/Ê/Ó/Õ/Ú e suas minúsculas.
6. Regressão da retomada: introdução MP4, troca de capítulos e vídeo do Chapter 5.
