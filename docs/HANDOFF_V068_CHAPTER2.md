# DeltaruneVita v0.68 — Handoff do teste do Chapter 2

Atualizado em: 2026-08-09  
Build atual: `v0.68-102`  
Estado: Internal Development Build — validação direta no PS Vita

## Objetivo atual

Validar o Chapter 2 sala por sala no PS Vita, procurando:

- texturas ausentes, trocadas, corrompidas ou exibidas por apenas um frame;
- quedas de FPS, stuttering e congelamentos temporários;
- falhas de áudio durante batalhas e transições;
- regressões de puzzles, colisões, diálogos e eventos;
- problemas introduzidos pela política de carregamento e descarte de atlas.

O foco imediato não é adicionar funções novas. Primeiro é necessário estabilizar o carregamento de texturas do Chapter 2 sem perder o desempenho já alcançado.

## VPK atual

Arquivo:

`artifacts/current/Deltarune-v0.68-102.vpk`

SHA-256:

`DCD62D9D747E997ACEDDD27D5030E6C4DECF17ECADD15DC60EA6432B850517C8`

Para testar esta revisão, basta instalar o VPK. Não é necessário copiar novamente os dados do jogo.

### Ajustes adicionais da v0.68-102

- cache RGBA regional ausente ou inválido volta automaticamente ao PVR disponível;
- página 17 usa RGBA4444 válido nas regiões Light World e Castle, protegendo Seam, o fundo da loja e `spr_castle_cafe`;
- página 15 usa PVR nas salas Cyber, reduzindo sua ocupação de 8 MB para 2 MB;
- Teacup deixou de usar o culling experimental que ocultava `obj_teacup` mesmo com a página 21 carregada;
- destinos Cyber pesados agora entram com alvo de 68 MB antes da criação da room.

## Estrutura usada no Vita

Dados externos principais:

`ux0:data/deltarune/deltarunevita/`

O runner pode carregar:

- `data.win` como fonte dos metadados, rooms, objetos e código GML;
- `texture-cache/` para páginas RGBA4444;
- `pvr/` quando o uso de PVR está ativado nas configurações;
- áudios, vídeos, UI, bordas e arquivos por capítulo presentes na pasta externa.

O VPK contém principalmente o runner, a interface Vita e os recursos pequenos necessários para iniciar o port.

## Política de texturas da v0.68-101

O Chapter 2 passou a usar uma política por região, em vez de manter as mesmas páginas lossless em todas as salas:

- Light World;
- Castle;
- Cyber/City;
- Mansion;
- salas genéricas.

Páginas atualmente preservadas em RGBA4444 conforme a região:

- página 12: Castle;
- páginas 14 e 16: Mansion;
- página 17: Light World;
- página 29: preservada globalmente por conter elementos sensíveis.

As demais páginas podem usar a fonte configurada pelo renderer, incluindo PVR quando habilitado.

Salas pesadas com política de memória restrita:

- `room_dw_castle_area_2_transformed`;
- `room_shop1`;
- `room_dw_cyber_viro_ring`;
- `room_dw_cyber_teacup_final`;
- `room_dw_cyber_rollercoaster`;
- `room_dw_cyber_maze_queenscreen`;
- `room_dw_cyber_musical_shop`.

Nessas salas:

- o alvo de memória antes da entrada é aproximadamente 76 MB;
- atlas antigos podem ser liberados após 8 frames;
- nas salas comuns do Chapter 2, a proteção continua sendo maior para evitar flicker.

Arquivos centrais alterados:

- `src/butterscotch/src/gl_legacy/gl_legacy_renderer.c`
  - seleção da política por região;
  - carregamento, retenção e descarte das páginas;
  - proteção LRU reduzida nas salas restritas.
- `src/butterscotch/src/runner.c`
  - classificação de destinos pesados;
  - trim de memória antes da troca de room.

## Resultado que levou à v0.68-101

Nos logs da revisão anterior:

- Castle transformado mantinha cerca de 101–106 MB residentes e adiava repetidamente a página 12;
- `room_shop1` não conseguia manter ou carregar de forma estável o atlas do Seam;
- Teacup e Rollercoaster entravam com cerca de 77–79 MB, mas subiam novamente para aproximadamente 102 MB durante a criação da sala;
- páginas necessárias ficavam em `preflight_deferred` por vários frames;
- a retenção global de 120 frames impedia a liberação rápida de páginas antigas.

A v0.68-101 tenta resolver isso com política regional, trim antecipado e retenção curta apenas nas salas críticas.

## Salas prioritárias para o próximo teste

Testar nesta ordem:

1. `room_dw_castle_area_2_transformed`
   - confirmar a região superior do castelo;
   - sair e voltar à sala;
   - verificar NPCs, restaurante e elementos do topo.
2. `room_shop1`
   - confirmar se Seam aparece continuamente;
   - observar se aparece somente em frames isolados;
   - abrir e fechar diálogos e a loja.
3. `room_dw_castle_restaurant`
   - confirmar o fundo, incluindo `bg_dw_bakery`;
   - verificar a transição de ida e volta.
4. `room_dw_cyber_teacup_final`
   - testar a entrada e a sequência completa;
   - observar texturas ausentes e FPS;
   - repetir após sair e voltar.
5. `room_dw_cyber_rollercoaster`
   - registrar FPS durante toda a animação;
   - verificar se o custo está em renderização, eventos ou upload de textura.
6. `room_dw_cyber_viro_ring`, `room_dw_cyber_maze_queenscreen` e `room_dw_cyber_musical_shop`
   - confirmar se a política restrita não introduziu novas ausências.
7. Batalhas do Chapter 2
   - conferir `spr_attackspot`, barras, retratos e efeitos;
   - registrar o primeiro uso e uma repetição da mesma batalha.

## Procedimento de teste no Vita

Antes de iniciar:

1. Instalar `Deltarune-v0.68-101.vpk`.
2. Manter os dados externos atuais; não regenerar cache para este teste.
3. Confirmar no Chapter Select que a build exibida corresponde à revisão em teste.
4. Usar o mesmo save e as mesmas configurações gráficas durante uma rodada comparativa.
5. Ativar o Dev Log somente no trecho que será medido, pois ele também afeta o desempenho.

Para cada problema:

1. Entrar na sala normalmente.
2. Aguardar alguns segundos sem movimentar o personagem.
3. Reproduzir o problema uma vez.
4. Se for seguro, sair e voltar para verificar se a textura muda.
5. Encerrar o Dev Log para salvar um arquivo isolado.
6. Anotar se PVR estava ligado ou desligado, bordas ligadas ou desligadas e o modo gráfico usado.

Salvar os logs em:

`logs/chapter-log/chapter 2/`

Formato recomendado:

`N_room_nome_YYYYMMDD-HHMMSS.log`

Também copiar o `butterscotch-probe.log` da sessão completa.

Ao ocorrer crash, copiar também:

- arquivo `.psp2dmp`;
- `butterscotch-probe.log` completo;
- nome da room de origem e destino;
- indicação de PVR, bordas, mod e Dev Log;
- ação exata imediatamente anterior ao crash.

## O que procurar nos logs

Sinais relacionados a texturas:

- `preflight_deferred` repetido para a mesma página;
- upload seguido imediatamente por eviction;
- crescimento inesperado de memória após a construção da room;
- páginas diferentes ocupando o mesmo slot ou sendo reutilizadas cedo demais;
- atlas requerido que nunca é enviado para a GPU.

Sinais relacionados a desempenho:

- `render_us` alto com `events_us` baixo: gargalo gráfico;
- `events_us`, `alarms_us` ou `collision_us` altos: gargalo de lógica GML;
- primeiro frame lento e repetições normais: cache ou upload inicial;
- lentidão contínua: excesso de draws, surfaces, partículas ou atlas residentes;
- áudio cortando junto com o frame: thread principal ou transição bloqueada.

## Sequência de correção recomendada

1. Validar a v0.68-101 nas sete salas prioritárias.
2. Separar os resultados em:
   - textura ausente;
   - textura errada/corrompida;
   - desempenho;
   - áudio;
   - lógica/softlock.
3. Para texturas ausentes, identificar a página do atlas e conferir se foi adiada ou liberada cedo.
4. Para texturas erradas, verificar associação página/slot e geração do cache antes de alterar compressão.
5. Ajustar primeiro a lista de páginas obrigatórias por região/sala.
6. Só depois alterar os limites de memória ou o tempo de retenção LRU.
7. Recompilar um único VPK incremental, com número sequencial.
8. Repetir exatamente a mesma rota no Vita.
9. Quando Castle, Shop, Teacup e Rollercoaster estiverem estáveis, continuar a auditoria sala por sala.
10. Após concluir o Chapter 2, congelar sua política de texturas antes de voltar aos Chapters 3 e 5.

## Regras para evitar regressões

- Não regenerar `texture-cache` ou PVR durante esta comparação, salvo quando o defeito for confirmado como dado externo.
- Não trocar várias páginas entre RGBA4444 e PVR no mesmo teste.
- Não aplicar culling global para corrigir uma única sala.
- Não reduzir a qualidade do modo Original.
- Não remover atlas de UI, fontes, diálogos ou combate apenas para ganhar memória.
- Não assumir que uma textura ausente é causada pela compressão: primeiro confirmar carregamento, slot e eviction.
- Manter backups dos VPKs que apresentam comportamento conhecido.
- Preservar as mudanças locais existentes; a árvore do Git contém muitas alterações e arquivos não versionados.

## Compilação do próximo VPK

Executar a partir da raiz do projeto:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\scripts\build-butterscotch-probe.ps1" -Configuration Release
```

Após compilar:

1. confirmar o caminho em `artifacts/current/`;
2. confirmar que foi criado backup em `artifacts/Backups/`;
3. calcular e registrar SHA-256;
4. instalar somente o novo VPK, salvo se a mudança também envolver dados externos.

## Próxima decisão esperada

Depois do teste da v0.68-101:

- se Castle e Seam forem corrigidos, manter o trim de 76 MB para destinos Castle pesados;
- se Teacup e Rollercoaster melhorarem sem perder texturas, manter a retenção curta de 8 frames;
- se houver flicker ou texturas que aparecem ao voltar à sala, aumentar seletivamente a retenção ou fixar somente as páginas comprovadamente necessárias;
- se continuar `preflight_deferred`, reduzir os atlas residentes da região ou antecipar o carregamento da página obrigatória;
- se o FPS continuar baixo com todas as texturas presentes, investigar draws, surfaces, partículas e eventos GML em vez de aumentar o cache.

## Estado do repositório

O diretório atual possui muitas alterações acumuladas, arquivos removidos e arquivos ainda não versionados. Antes de criar commit, release ou sincronizar com GitHub:

1. revisar `git status`;
2. não restaurar arquivos em massa;
3. não usar reset destrutivo;
4. separar código do runner, scripts, dados gerados, logs e artefatos;
5. validar a lista exata de arquivos que serão enviados.
