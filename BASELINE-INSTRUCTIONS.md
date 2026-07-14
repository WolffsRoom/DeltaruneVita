# DELTARUNE 1–5 — teste-base com YoYo Loader Nightly

Estado: `[IMPLEMENTADO, AGUARDANDO TESTE]`

Este teste usa o binário Nightly oficial do YoYo Loader, sem incorporar ou
redistribuir o APK do jogo.

## Arquivos verificados

- `YoYoLoader-Nightly.vpk`
  - SHA-256: `AADB6E092B8094E91B1E14829232673A93191EB7BD2E8104F9ABA54830DD82CA`
  - tamanho: 8.208.053 bytes
- `yoyoloader-builder.zip`
  - SHA-256: `240E54E7BE177463F5DA7760C230EC58DC8FC50937ED092AB23B24DEFA6674BC`
  - contém `standalone.bin` e `standalone_video.bin`.

## Instalação

1. Instale `YoYoLoader-Nightly.vpk`.
2. Confirme `kubridge.skprx`, `fd_fix.skprx` (não usar junto com rePatch) e
   `libshacccg.suprx` conforme o README oficial.
3. No Vita, crie `ux0:data/gms/deltarune/`.
4. Copie o APK analisado para `ux0:data/gms/deltarune/game.apk`.
5. Crie `ux0:data/gms/deltarune/yyl.cfg` com:

```ini
forceGLES1=0
forceBilinear=0
platTarget=0
debugShaders=0
debugMode=1
noSplash=0
maximizeMem=0
netSupport=0
squeezeMem=0
disableAudio=0
uncachedMem=0
doubleBuffering=0
```

6. Execute uma vez sem usar Optimize APK nem externalização de assets.
7. Recupere `ux0:data/gms/shared/yyl.log` imediatamente após o teste.
8. Se houver crash, recupere também o `psp2core` dessa mesma execução.

## Resultado esperado

O loader deve identificar o Game ID e ativar o workaround existente:

```text
Detected DELTARUNE as Game ID
Enabling Deltarune specific gamehack!
Startup ended
```

## Critério para a próxima revisão

- Se não houver `Startup ended`, usar a última linha do log e o core.
- Se houver erro de extensão/WAD, implementar somente o método observado.
- Se chegar ao menu ou gameplay, registrar áudio, vídeo, input, FPS e capítulo.
- Não ativar otimizações de memória antes de obter esse resultado-base.

O Nightly oficial ainda usa o log compartilhado `yyl.log`. O `port.log` com
Build ID `DLTVITA-0001` pertence à revisão-fonte customizada e só existirá após
ela ser compilada com VitaSDK-softfp.
