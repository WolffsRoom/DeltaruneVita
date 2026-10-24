# v0.75-7 — sincronização de recursos e uniforms no VitaGL

**Atualização:** o teste posterior do vídeo no aparelho revelou falhas
contínuas de reserva GXM e ausência de imagem. O caminho de uniforms dessa
build foi substituído na v0.75-8; ver
[diagnóstico e retomada da v0.75-5](ch5-v0758-video-rebase.md).

Data: 2026-09-18. Build para teste no aparelho, com Modern-GL e shaders ligados.

## Evidência do relato

O usuário relatou GPU crash na v0.75-6. O arquivo atual
`logs/chapter-log/Chapter5/butterscotch-probe.log` identifica **v0.75-5**:
termina em `room_dw_castle_cafe`, imediatamente após
`MODERN_GL_RETIRED_COLLECT frame=13463 count=1`. O dump associado
`psp2core-1789706553-GPUCRASH.psp2dmp` contém notas GPU e não indica uma exceção
CPU nas threads examinadas. O registro de minigame ainda disponível em
`chapter 3/CrashMiniGame (ModernGL)` identifica **v0.74-12**; não é evidência
de execução da v0.75-6. Não houve reprodução do fault físico no desktop.

## Correções compartilhadas por todas as salas

- `vglForceGarbageCollection` encerra a cena aberta, espera os trabalhos enviados
  e só então libera as gerações de recursos. A próxima cena é criada pelo próximo
  desenho. Antes, chamava `glFinish`, cujo `scene_reset` abria outra cena e podia
  adicionar desenhos de scissor antes da liberação. O teste de fila simulada
  executa as funções reais e detectou coleta com uma cena ainda pendente.
- Uniforms dos shaders customizados passam pelas reservas nativas
  `sceGxmReserveVertexDefaultUniformBuffer` / `sceGxmReserveFragmentDefaultUniformBuffer`.
  Removido o pool próprio de 2 MiB que voltava ao início sem observar o consumo
  pela GPU. Cada desenho reserva e copia seus parâmetros, mesmo sem mudanças
  nos valores; não se reutiliza uma reserva nativa consumida por desenho anterior.
- Falhas de reserva não executam `memcpy` com ponteiro nulo e rejeitam o desenho.
  Os sete caminhos com `is_draw_legal` verificam esse resultado também no build
  `SKIP_ERROR_HANDLING`; multidraw e `vglDrawObjects` também respeitam a falha.
  Os caminhos de restauração de atributos permanecem executados.

As mudanças estão em `third_party/vitaGL-nosplash/source/`, em `gxm.c`,
`custom_shaders.c`, `draw.c`, `shared.h` e `utils/gxm_utils.{c,h}`. Nenhuma sala
é excluída dos shaders. Não há alteração de save, assets ou dados do jogo.
As APIs usadas constam na [documentação VitaSDK de GXM](https://docs.vitasdk.org/group__SceGxmUser.html).

## Validação realizada

- `local-test/test_vita_gpu_collection.py`: gera teste C das funções reais com
  fila GXM simulada. Falhou antes da correção; passou depois, incluindo 10.000
  ciclos, coleta repetida sem desenho e contexto desligado.
- `local-test/test_vita_shader_uniforms.py`: gera teste C dos helpers, macro de
  upload e `glDrawArrays` reais. A implementação anterior sobrescreveu a primeira
  reserva após ultrapassar 2 MiB. A nova passou com 6.001 reservas, injeção de
  falha, recuperação e ausência de submissão inválida em modo release.
- Regressões C existentes de reservas internas e seleção/liberação de render
  targets compartilhados passaram.
- Build desktop atualizado; 14/14 cenários de shaders dos capítulos 1–5 passaram.
- 4/4 smoke tests: cafe do Chapter 5 e gameshowroom do Chapter 3, GL2/GL3,
  900 frames por cenário, shaders habilitados. Capturas conferidas. A entrada
  direta nas salas usa estado inicial de teste; não reproduz o save nem a
  interação completa do minigame. Resultados em
  `local-test/logs/v0757-gpu-rooms/results.json`.
- VitaGL recompilado do zero (`-CleanVitaGL`); VPK e ELF concluídos. O build
  ainda emite avisos preexistentes, incluindo declaração de `nowNanos` e
  possíveis truncamentos de caminhos; não são tratados como novos resultados.
- VPK validado como ZIP, 171 entradas, 5.623.266 bytes; tag `v0.75-7` no ELF.
  SHA-256: `810e73c767331c99963af77fbab76bb11c72d189847510a1e38d88aa51c461f9`.

Os scripts de teste C geram fontes em `local-test/logs/`, compiladas e executadas
com MSVC. Não emulam o driver/GPU do Vita; comprovam as invariantes de ordem,
retenção e tratamento de falhas exercitadas. O log sozinho não permite atribuir
o fault físico a uma instrução específica. Portanto, **não se afirma que todo
GPU crash foi eliminado** antes da validação no aparelho. O custo das reservas
nativas por desenho também precisa ser medido no Vita.

## Pacote e próximo teste

VPK: `artifacts/current/Deltarune-v0.75-7.vpk`. Instalar sobre a versão anterior;
estas correções não exigem novo pacote de dados. O backup numerado é
`artifacts/Backups/v0.75-7_20260918-061357`. Um arquivo complementar de fontes
do VitaGL e testes é salvo nesse backup porque o script padrão copia apenas
`src/` e `scripts/`.

Confirmar `v0.75-7` no cabeçalho do próximo log. Com Modern-GL + shaders ON:
entrar/sair repetidamente do cafe/loja de Castle Town; completar e reiniciar o
minigame; testar outras transições e o vídeo. Verificar estabilidade, efeitos
visuais e FPS. Se persistir, guardar o log e o novo dump da mesma execução.
