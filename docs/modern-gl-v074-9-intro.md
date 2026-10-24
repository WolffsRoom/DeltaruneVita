# v0.74-10: isolamento da introdução e do menu Modern-GL

O sequenciador do build atribuiu v0.74-10 ao pacote final desta rodada. Build concluído e integridade ZIP verificada; SHA-256: 5793f2e75df294291c3f058279e63d8fd2034c721ba32204f078685c7ce8085d.

Os arquivos em logs/chapter-log/geral registram v0.74-8, ENTRY=chapter0. O MP4 termina com presented=1 e timeout=0. O log alcança FRAME=first_complete em PLACE_CHAPTER_SELECT_2x e o dump é de GPU, sem uma exceção de CPU identificada pelo parser. Não há evidência de falta de memória nesse ponto.

A alteração é uma contenção da transição: depois da introdução, Modern-GL abre chapter0 pelo mecanismo existente restart_into_chapter. A nova execução consome o marcador explícito, pula a introdução e cria seu contexto gráfico sem reutilizar o contexto do player. Legacy continua pela sequência anterior. Falha ao reiniciar apresenta erro em vez de continuar pelo caminho sob investigação.

Isso não demonstra a causa interna do GPU crash nem garante que o menu isolado esteja livre de falhas. O próximo teste no Vita deve confirmar: introdução uma vez, seleção de capítulos, abrir Chapter 5 e retornar ao menu. STARTUP=explicit_chapter_request no log confirma a segunda execução. O log principal é recriado no novo processo, como já ocorre em trocas de capítulo.

Validação host: local-test/test_intro_handoff.py extrai o bloco real de boot para um programa C com chamadas de plataforma simuladas. Cobre Modern após vídeo, reentrada explícita sem loop, Legacy, fallback PNG e erro de reinicialização. Esse teste não simula a GPU do Vita.

Pendências anteriores permanecem: vídeo Chapter 5 precisa de validação, inclusive áudio externo; a inspeção dos arquivos MP4 locais não encontrou uma trilha de áudio. A pré-release não foi publicada. As imagens preparadas anteriormente ainda precisam de revisão antes de qualquer publicação como comparação sem/com shaders.
