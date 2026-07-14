# DELTARUNE capítulos 1–5 para PS Vita

Build ID: `DLTVITA-0001`

## Estado

`[IMPLEMENTADO, AGUARDANDO TESTE]`

Foi criada uma revisão diagnóstica baseada no YoYo Loader Vita. Nenhuma
compatibilidade em hardware foi declarada sem teste.

## Evidência confirmada

- APK SHA-256: `C25CCCCB42CD7C62FC41480B188F72367F60F123FEC395A762E072CC4DB7AC48`
- APK: 746.349.483 bytes
- Package: `com.hadrian.deltarune`
- `libyoyo.so`: ELF32 little-endian ARM, `e_machine=40`, EABI5 soft-float
- GameMaker bytecode em `assets/game.droid`
- Dados em `chapter0.wad` a `chapter5.wad`
- O YoYo Loader já possuía keymap DELTARUNE e workaround específico de
  `glReadPixels` para o Game ID `DELTARUNE`.
- O loader já possui AssetManager, áudio Android/OpenSL, player de vídeo e
  inicialização `RunnerJNILib_Startup`.

## Mudanças da revisão

- log isolado em `ux0:data/gms/deltarune/port.log`;
- log anterior removido no início de cada execução;
- Build ID, hash e tamanho do APK esperado registrados no log;
- chamadas de extensões GameMaker registradas com módulo, método e argc;
- configuração inicial conserva o comportamento padrão e ativa apenas o log.

## Limite atual

`[DESCONHECIDO]`

O PC atual não possui `VITASDK` configurado. A revisão passou em `git diff
--check`, mas ainda não foi compilada nem executada no Vita.

Também não está confirmado se `WADLoader` precisa executar código Java. A
primeira execução deve mostrar se o runner acessa os WADs diretamente ou chega
a uma chamada de extensão não implementada.

## Primeiro teste

1. Compilar com VitaSDK-softfp, `LOADER=ON` e suporte a vídeo.
2. Instalar o loader e seus requisitos (`kubridge`, `fd_fix` quando aplicável e
   `libshacccg.suprx`).
3. Criar `ux0:data/gms/deltarune/`.
4. Copiar o APK legítimo como `game.apk`.
5. Copiar `ports/deltarune/yyl.cfg` para a mesma pasta.
6. Executar uma única vez, sem otimizar ou externalizar assets.
7. Recuperar `port.log` e o `psp2core` correspondente, se houver.

Marcadores esperados:

```text
BUILD_ID=DLTVITA-0001
Detected DELTARUNE as Game ID
Enabling Deltarune specific gamehack!
Startup ended
```
