# Chapter 5 video — retomada da v0.75-5

Em 2026-09-18, o teste no Vita com v0.75-7 não exibiu o vídeo nem seu áudio.
O usuário também viu shaders/sombras ausentes na Town do Chapter 5 e piora no
desempenho. Não há um log novo dessa sala; os sintomas podem compartilhar o
problema de reservas, mas a relação ainda requer teste no aparelho.
O log dessa execução identifica `v0.75-7` e mostra que o MP4 inglês foi aberto
com sucesso, o primeiro quadro 1280×720 foi decodificado e `video_start` chegou
à GML. Em seguida aparecem **1.153** falhas
`MODERN_GL_INTERNAL_UNIFORM_ERROR code=0x805B000B` na
`room_dw_garden_video`. O vídeo foi marcado como apresentado pelo código, mas
esse marcador não prova que pixels chegaram ao painel. O volume de falhas
invalida a premissa de que as reservas nativas de uniforms, introduzidas na
v0.75-7 para todos os desenhos customizados, funcionariam nessa cena.

O áudio do jogo nas outras salas tocou. A faixa do vídeo foi resolvida para o
override alemão `mods/Lang/German/music/ch5_intro_audio.ogg`, ficou na fila
durante a transição, foi liberada no primeiro quadro e passou por um seek que
registrou estado anterior `4114` (AL_PLAYING) com 16 buffers novos. Isso ainda
não demonstra áudio audível após o seek. A máquina local não contém o arquivo
alemão instalado no Vita; não se atribui o silêncio a ele sem medição.

## Base e correção

O backup `artifacts/Backups/v0.75-5_20260918-004954` foi comparado ao projeto
atual: os 358 arquivos de código elegíveis em `src/` e os 38 scripts
correspondentes eram idênticos antes da instrumentação do áudio. A diferença
de comportamento entre builds vinha sobretudo do VitaGL, que não faz parte
do backup padrão de `src/`/`scripts/`.

Retomado o caminho de uniforms customizados da v0.75-5: reutilização enquanto
os valores não mudam e armazenamento próprio de 2 MiB, sem consumir a reserva
nativa de GXM a cada desenho. A arena agora se recusa a sobrepor dados ainda
usados e, após encerrar a cena em `vglSwapBuffers`, espera a GPU antes de voltar
ao início quando atinge 1 MiB. Uma cena com mais de 2 MiB de uniforms rejeita
os desenhos excedentes até o próximo frame em vez de corromper dados. A
correção de ordem da coleta de recursos da v0.75-7 permanece.

Foi acrescentado `AUDIO_STREAM=ch5_video_track_state` a cada 120 atualizações
da faixa específica. O próximo log mostrará estado OpenAL, buffers na fila,
buffers processados, ganho, amostras consumidas e flags de pausa/espera. Essa
medição é limitada para evitar a escrita excessiva de logs durante o vídeo.

## Validação e limite

Os testes C com funções reais de VitaGL passaram: 10.000 ciclos de coleta;
retenção da arena até a espera da GPU; rejeição por falta de espaço e retomada;
desenho customizado pulado quando a reserva falha. O teste de pixels/estado do
vídeo em GL desktop e a compilação Vita verificam o caminho disponível
localmente. O desktop não emula GXM, o SceAvPlayer nem o arquivo de áudio
alemão do usuário. Assim, imagem e som ainda exigem novo teste no aparelho.
Sombras e FPS na Town também dependem desse teste; as regressões desktop não
medem a GPU do Vita.

O VPK para o próximo teste incorpora a etiqueta interna **v0.75-8**. A
pré-release pública v0.75 continua com v0.75-7; a v0.75-8 é um pacote local
de validação e não foi publicada como correção confirmada.

Build nativo concluído em `artifacts/current/Deltarune-v0.75-8.vpk` (171
entradas, 5.622.957 bytes, SHA-256
`deaaa6f5041f61b79effd7d7dcb32a207cd24b20287866277165926060b2b145`).
O `eboot.bin` no VPK corresponde ao ELF recompilado. Os 9 cenários locais de
vídeo/estado GL, tilemap e iluminação do jardim passaram, assim como os testes
C de coleta e uniforms. O backup numerado
`artifacts/Backups/v0.75-8_20260918-194658` inclui o código e um ZIP separado
do VitaGL usado nesta build. A v0.75-5, v0.75-7 e v0.75-8 foram adicionadas
à lista de retenção dos backups.

Adicionalmente, 12/12 cenários desktop de Town, batalha, navegador, PNG e
shaders GL2/GL3 passaram. A captura da Town em Modern-GL mostra a sombra
projetada; ela não reproduz a falha de ausência de shaders relatada no Vita.
