# Revisão Modern-GL — 2026-09-13

## Correções

- Corrigido crash desktop em `GLRenderer_ensureTextureLoaded`: o caminho de textura já carregada acessava `textureLastUsedFrame`, alocado somente no Vita. Validado também o índice da página.
- Compilação adiada agora acontece em `shader_get_uniform`, `shader_get_sampler_index` e `shader_is_compiled`, além de `shader_set`. Isso permite obter handles no Create antes do primeiro Draw. Falhas são memorizadas; `shader_is_compiled` informa o resultado real.
- A compilação preserva o programa GL ativo e libera shaders intermediários em caso de falha. Uniforms de arrays aceitam o nome sem `[0]`; handles e índices inválidos não acessam memória fora da tabela. Uploads de arrays respeitam o tamanho declarado; uniforms booleanos usam as funções inteiras.
- `gm_BaseTexture` recebe o estágio zero independentemente da ordem retornada pelo driver. Samplers secundários são reaplicados antes do desenho, porque uploads e operações de superfície alteram bindings. Primitivas, vertex buffers e sprites usam o mesmo caminho de seleção do programa/texturas.
- Removido o bloqueio por nome de `shd_lut`. O código GML consultado fornece `lut_tex` usando `sprite_get_texture(spr_luts, lut_slot)`; não depende de um arquivo LUT externo ausente.
- Corrigidos o tipo de upload de `gm_FogColour` e o limite de matrizes ativas.
- Restaurados atributos do VAO antes dos lotes de sprites após `vertex_submit`; primitivas preservam Z. O VBO recebe apenas os vértices usados. Registrado o alias `vertex_format_add_texcoord`, observado como chamada desconhecida na inicialização do Chapter 2.

## PRs upstream

- [#359](https://github.com/ButterscotchRunner/Butterscotch/pull/359): diff público consultado. O projeto já possui os builtins em `vm_vertex_builtins.inc`, os tipos de vertex buffer e os caminhos de desenho em Modern/Legacy. Não foi feito merge integral: a adaptação local usa VBO compartilhado e tem proteções próprias. Foram corrigidos os problemas de integração acima, que afetam primitivas com shaders e os sprites desenhados depois delas.
- [#330](https://github.com/ButterscotchRunner/Butterscotch/pull/330): revisado o escopo de otimizações; adotado o upload proporcional ao lote usado. Não importado o cache completo de estado.
- [#460](https://github.com/ButterscotchRunner/Butterscotch/pull/460): diff consultado. Usa cache estático de estado via wrappers por unidade de compilação. O port também muda estado em vídeo, bordas e código comum; integrar sem unificar/invalidate esses acessos pode fazer o cache divergir da GPU. Não aplicado nesta revisão.
- A lista de PRs abertos foi triada por relevância gráfica. Os demais PRs de áudio, VM, plataformas e interface não foram integrados nesta alteração.

## Validação local

Executável: `local-test/modern-build/butterscotch.exe`. Pasta separada porque o runner em `build/Debug` estava aberto antes da revisão.

1. `local-test/build_modern_review.bat` compila Debug usando o ambiente limpo de build existente.
2. `python local-test/test_modern_shaders.py` executa 14 casos: capítulos 1–5, `room_dw_mansion_single_pot` (Chapter 2) e `room_town_south` (Chapter 5), cada um com compilação imediata e adiada.
3. Cada caso executa 300 quadros, usa saves isolados e captura o quadro 299. Os dados vêm de `data/prepared/deltarune/deltarunevita`.

Resultado: **14/14**, saída zero, término no quadro 300, capturas geradas e nenhum erro de compilação/link ou fallback de shader registrado. As capturas das introduções dos cinco capítulos são idênticas entre compilação imediata e adiada. Inspeção visual da mansão e de Hometown realizada; `shd_lut` compilou e linkou no teste adiado de Hometown.

Logs, PNGs e `results.json`: `local-test/logs/modern-review`.

`BS_GL_DEFER_SHADERS=1` permite reproduzir no desktop o ciclo de consultas antes do primeiro shader_set. Não simula SHARK, GLSL ES/VitaGL, orçamento de memória ou GPU do Vita. Não houve teste em hardware, geração de VPK ou playthrough completo; a sala da mansão não constitui validação de todos os ataques/efeitos de batalha. O teste também não cobre todos os shaders em execução, mesmo que o modo imediato compile os shaders presentes nos dados.

## VPK de teste gerado posteriormente

A pedido do usuário, gerado `artifacts/current/Deltarune-v0.73-23.vpk` em Release
com VitaSDK/Docker. O build revelou que os headers VitaGL não definem `GL_BOOL*`;
os tipos booleanos desktop agora são normalizados para os tipos inteiros usados
no VitaGL. Os 14 testes desktop foram repetidos e passaram após esse ajuste.

Pacote validado: CRC ZIP íntegro, 171 entradas, `eboot.bin`, `sce_sys/param.sfo`
e ícone presentes; TITLE_ID `DELT00001`, APP_VER `00.73`, 5.592.438 bytes.
SHA-256: `d7cd669b20cd91885a5f0a9d791e60d2181e570c0fe00231b6b9f819cf3b369b`.
Selecione **Modern GL (experimental)** nas configurações para testar o caminho
alterado. O build passou; a execução no hardware ainda não foi validada.
