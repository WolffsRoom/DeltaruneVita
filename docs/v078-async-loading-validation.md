# v0.78-8 — validação de carregamento e frame pacing no Vita

## Escopo

Este build introduz uma base conservadora para retirar trabalho de I/O da
thread de renderização sem executar VitaGL, GXM, eventos GameMaker, mutações da
VM ou saída de áudio em segundo plano.

- O `data.win` do Chapter 0 começa a ser analisado enquanto o vídeo de abertura
  é reproduzido. Ao final do vídeo, o resultado pronto é adotado; se a tarefa
  falhar ou o caminho mudar, o carregamento síncrono existente continua sendo o
  fallback.
- Payloads de rooms lazy usam um arquivo independente e podem ser preparados
  pelo único worker de fundo antes da troca. A room antiga permanece ativa até
  o payload obrigatório estar pronto, preservando a ordem de Room End/Start.
- Leituras de texturas lazy também usam um arquivo independente. Nesta versão,
  o estágio é somente de CPU e a criação/upload gráfico continua na thread
  principal, evitando ampliar o risco de GPU crash.
- O limitador de 30/40 FPS usa deadlines absolutos, resto fracionário e uma
  margem de espera que se adapta ao atraso real do scheduler. Um frame atrasado
  reinicia a cadência em vez de tentar compensar com um frame curto.
- O serviço usa o quarto núcleo quando o CapUnlocker está disponível e retorna
  automaticamente ao núcleo normal quando não está. O orçamento total de
  staging é limitado a 8 MiB.
- Registros `STARTUP_PRELOAD`, `VITA_ROOM_PREFETCH` e `IO_PROFILE` permitem
  comparar carregamento, espera e sobreposição no hardware.

## Artefato validado

- Build: `Deltarune-v0.78-8.vpk`
- Tamanho: 7.053.405 bytes
- SHA-256: `AF42217D14D966E0E3B99527E0B0DC83487134CD6268A3F1AFF6CF0898DC3C0A`
- Integridade ZIP/VPK: todos os itens passaram pela verificação CRC.
- Conteúdo conferido: `eboot.bin`, `sce_sys/param.sfo`,
  `sce_sys/livearea/contents/bg0.png`, `startup.png`, `first_load.mp4` e
  `assets/chapter0/data.win`.

## Validação local executada

Os contratos de worker, fallback do CapUnlocker, cadência 30/40/60 FPS,
preload do Chapter 0, staging de room/textura, telemetria de I/O, vídeo,
Experimental-GL, memória em troca de rooms e orçamento do gameshow do Chapter 3
passaram. O teste de renderização da final do Chapter 3 passou em Legacy e
Modern.

O build completo pelo VitaSDK terminou com código zero. Os avisos exibidos
durante a compilação já pertencem às dependências e caminhos antigos; não houve
erro de compilação ou link.

## Decisões de segurança

O plano original previa cache de dois payloads de room e decode/upload de
texturas assíncrono. O primeiro build usa apenas um payload em andamento para
reduzir pico de RAM. A preparação de textura permanece CPU-only porque o fluxo
atual termina a room anterior antes de existir um ponto seguro para publicar
recursos gráficos. Adiar o frame nesse ponto poderia criar softlock ou novo GPU
crash. A separação de leitura já entregue prepara uma futura máquina de estados
por fases sem alterar o renderer agora.

IOStaging não é detectado, instalado ou exigido pelo jogo. O protocolo de teste
A/B está em `docs/iostaging-v078-test-protocol.md` e usa exatamente o mesmo VPK
e save com o plugin desligado e ligado em sessões separadas.

## Checklist no Vita

1. Iniciar sem CapUnlocker e confirmar vídeo, áudio e entrada imediata no
   Chapter 0. Repetir com CapUnlocker e comparar `STARTUP_PRELOAD`.
2. Testar 30 e 40 FPS por pelo menos dez minutos, atravessando rooms e abrindo
   batalhas. Registrar median, p95, p99 e maior frame no log.
3. Repetir ida e volta entre Town, Castle Town e lojas do Chapter 5; confirmar
   texturas, áudio e ausência de freeze/GPU crash.
4. Entrar no Round 2 e gameshow do Chapter 3 em Automático, Modern e
   Experimental; confirmar shaders, enquadramento e estabilidade.
5. Testar a entrada e as transições da batalha final do Flowey, observando
   torre, quedas nas mudanças de fase e uso de VRAM.
6. Fazer o A/B de IOStaging seguindo o protocolo dedicado e anexar o log de
   cada condição. Um ganho só deve ser aceito se não houver corrupção, perda de
   áudio, freeze ou crash.

Os testes locais não simulam o VitaGL/GXM real nem validam ganhos de FPS no
hardware. Portanto, a v0.78-8 é o artefato de teste dessa implementação, e os
resultados do checklist acima determinam se as otimizações devem permanecer na
próxima pre-release.
