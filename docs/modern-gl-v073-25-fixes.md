# Modern-GL — revisão v0.73-25 (2026-09-14)

Revisão dos relatos de textos ausentes, crash de transição e lag na v0.73-24.

## Evidências

- `logs/chapter-log/Chapter5/butterscotch-probe.log`: página 104 recusada
  repetidamente pelo cache; transição de `room_town_south` para `room_town_mid`.
- O ELF da versão testada foi preservado como `logs/chapter-log/Chapter5/v073-24.elf`.
  No novo dump, o retorno de código do jogo `0x81169051` corresponde ao endereço
  ELF `0x81123051`, em `gpu_free_texture_data`, ao liberar memória. O PC está
  em SceLibKernel; a associação automática do PC a `main` pelo parser é inválida.
  Os registros contêm valores compatíveis com pixels, como `0xff441111`, e o log
  já apresenta contadores de CDRAM inválidos antes do crash. Isso indica corrupção
  anterior à liberação, não apenas falta de RAM na troca de sala.
- `logs/chapter-log/Chapter5/LogBattle/butterscotch-probe.log`: página 107
  repetidamente recusada, 18.628.608 bytes de texturas residentes e limite de
  18 MiB. Um resumo tem média de 60.225 us por quadro, máximo de 478.937 us;
  no pico, 467.558 us são de renderização. Havia cerca de 10 MiB de RAM,
  24 MiB de PHY e 80 MiB de CDRAM livres nesse trecho.

## Alterações

- VitaGL marca texturas usadas como destino de renderização com o quadro atual.
  Antes apenas o uso como sampler atualizava essa informação. Uma superfície
  desenhada, mas ainda não amostrada, podia ser liberada imediatamente enquanto
  a GPU ainda escrevia nela. Agora segue a fila de liberação adiada. A falha
  identificada é compatível com a corrupção do dump; falta confirmar no hardware
  se explica integralmente o crash reportado.
- Criar sprite a partir de superfície restaura o framebuffer anterior. Antes,
  desenhos posteriores podiam continuar na superfície usada como origem.
- Cache do Chapter 5 passa de 18 para 24 MiB, com verificação de espaço em RAM,
  PHY e CDRAM. RAM baixa isoladamente deixa de bloquear um upload que pode
  caber em outro pool mapeado. As reservas continuam sendo respeitadas.
- Fontes regulares e fontes criadas com sprites têm prioridade de admissão.
  Fontes usadas neste quadro ou no anterior são protegidas do LRU; fontes
  antigas podem sair. O carregamento continua sob demanda: carregar todas as
  fontes antecipadamente consumiria vários atlas grandes e prejudicaria o cenário.
- Uma fonte pode superar o bloqueio provocado pelo cenário e, se necessário,
  substituir uma página de cenário já desenhada, com envio do lote pendente e
  liberação adiada do armazenamento GPU. LUTs permanecem protegidas.
- Falha de carregamento de uma página é tentada apenas uma vez por quadro,
  incluindo fontes de sprites que pedem a mesma página por letra. Tentativas
  repetidas deixam de abrir/gravar o log em cada fase; a recusa persistente é
  registrada a cada 120 quadros.
- Coleta forçada ocorre quando há páginas expulsas aguardando liberação,
  em vez de ocorrer em todo quadro com menos de 8 MiB de RAM livre.
- Liberados os vetores de controle do cache na destruição do renderer.

## Validação

- Suíte anterior: 14/14 casos de 300 quadros.
- Suíte ampliada: 8/8 casos de 600 quadros, GL2/GL3 e shaders imediatos/adiados,
  em Hometown e `room_dw_fcastle_seth_encounter`. Os quatro casos de Hometown
  executam quatro trocas de sala cada.
- Leitura de pixels: PNG externo sob shader personalizado; 16 fontes regulares
  do Chapter 5; cópia de região de superfície com restauração do destino e
  preservação do sprite depois de liberar a origem.
- `local-test/test_modern_cache_policy.py` extrai as funções reais de seleção
  LRU e admissão de memória do renderer Vita para um teste C com chamadas GL e
  alocação simuladas. Passou para prioridade de fonte, proteção de LUT/fonte
  recente, expulsão de fonte antiga e uso dos três pools com reservas.

Limites: o teste da sala de Seth mostra o cenário, mas não reproduz o combate
completo nem o save do usuário. O desktop não executa o VitaGL/GXM; o teste C
de política também não simula a GPU. Não há medição de ganho de FPS no Vita nem
confirmação de eliminação do crash. O menu nativo Game Settings depende do
teste em hardware, embora use o caminho de fontes corrigido.

Logs: `local-test/logs/modern-town`, `local-test/logs/modern-review` e
`logs/build-modern-gl-v073-25.log`.

## Pacote de teste

Build Vita Release concluído, incluindo o VitaGL alterado.
`artifacts/current/Deltarune-v0.73-25.vpk`: 5.593.747 bytes, 171 entradas,
CRC ZIP íntegro e `eboot.bin`, `sce_sys/param.sfo`, `sce_sys/icon0.png` presentes.
SHA-256: `50e228f0e72d42bbd12e82c6ccdec420a1be33e8db36001d6a9bd5fd76105424`.
