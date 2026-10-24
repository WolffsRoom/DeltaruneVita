# Native GXM39 — Chapter 3 FPS, emulação e integridade

Data: 2026-10-14. Workspace: `C:\Users\wolff\Documents\SDKVita\DeltaruneVita`.

## Logs físicos

Os últimos registros do Vita físico em `logs/chapter-log/geral/Ch3/native-gxm-probe.log` correspondem à **GXM36**, e não à GXM38/GXM39. Na `room_dw_couch_overworld_intro` (índice 98), as janelas de FPS ficaram principalmente em ~18,5–21,6 FPS, com queda ~15 FPS. Em amostras de 300 frames: Step 23–25 ms, desenho 16–23 ms, swap 4–7 ms. Eventos do Runner consumiam cerca de 23–25 ms. As páginas de textura estavam presentes; sem evidência de estouro de cache. GXM36 mede apenas hotspot de Normal Step, então os rankings físicos mais antigos não capturam Begin/End Step.

## Diagnóstico GXM38 no Vita3K (Room 98, GameMaker real)

Vita3K 0.2.1. Lançamento direto com marcador de teleporte e áudio emulado desativado. Shader opt-in desativado para isolar custo da CPU; os dados e scripts GameMaker foram mantidos.
- Perfil inicial de 300 frames: Step 2,208 µs, draw 4,423 µs, swap 1,867 µs; begin dispatch 492 µs; normal 659 µs; end 539 µs. ~21 FPS durante o avanço inicial (há carregamento/cutscene).
- Perfil posterior: Step ~1,729 µs; draw ~2,253 µs; swap ~128 µs, e janelas de ~29 FPS em estado estável.
- Hotspots detalhados: `obj_time` **Begin Step**; `obj_mainchara` **End Step**; `obj_ch3_PGS01A` e `obj_darkcontroller` **Normal Step**. Os dois primeiros são uma via de custo central no capítulo e não devem ser pulados porque controlam entrada e câmera.

## GXM39 — otimização conservadora

Em `src/butterscotch/src/runner.c`, instrumentação detalhada de início/fim de cada evento passa a ocorrer uma vez a cada oito frames (`runner->frameCount & 7`), escalando os acumuladores para manter estimativas ao longo da janela de 300 frames. Mantidas integrais: execução de TODOS os eventos, scripts GML, cálculos, animações, câmera, áudio, input, ordens de desenho e medição geral de custos `frame_cost` e `runner_profile`. Trata-se de reduzir custo do **profiler**, não de reimplementar os scripts pesados.

Código preserva `NATIVE_GXM_PROBE_VERSION`: não afeta a VitaGL/Modern-GL. Novo Title ID `DLTAGXM39`, GXM38 não sobrescrita.

### Validação

- Testes `scripts/test-native-gxm-probe-contracts.py`: PASS, incluindo todas as regressões de shaders, atlas, vídeo, culling, room navigator e instrumentação GXM39.
- GXM39 compilada no WSL/VitaSDK. Arquivo `artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm39.vpk`, ZIP CRC íntegro, Title ID verificado.
- Vita3K iniciou Chapter3 `room_dw_couch_overworld_intro` real; centenas de frames sem crash.
- Capturas `GXM38-Vita3K-Ch3-Room98-Performance-Baseline.png` e `GXM39-Vita3K-Ch3-Room98-LowOverhead-Perf.png`: **0 pixels RGB diferentes** em 960×544. A mudança não apresentou diferença visual nesse frame.
- Amostra emulada estável GXM38 ~1,729 µs Step; uma amostra GXM39 ~1,452 µs Step. Os dois ensaios não estão temporalmente sincronizados e a diferença não demonstra ganho consistente de FPS; ambos observaram ~29 FPS em trechos posteriores. Os clocks do PC/Vita3K não representam o ARM do console.

## Conclusões e próximos testes físicos

A GXM39 é um hotfix experimental de overhead de diagnóstico e não resolve o gargalo estrutural de ~23 ms por frame no interpretador GameMaker do Vita físico. **Não afirmar 60 FPS nem melhoria física mensurada**. Instalar GXM39 separadamente e registrar os novos `step_phases`, `step_hotspot`, `frame_cost`, `fps` na sala 98. A fase Begin/End mais onerosa orientará a otimização de chamadas de câmera/entrada ou VM mediante validação de comportamento. Os shaders e visual dos jogos não foram reduzidos pelo hotfix.

