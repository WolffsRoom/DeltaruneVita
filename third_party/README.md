# Dependências externas de build

As dependências abaixo não são distribuídas como cópias integrais neste
repositório. Isso evita versionar binários gerados e checkouts Git aninhados.

## VitaGL sem splash

Clone `https://github.com/Rinnegatamante/vitaGL.git` em
`third_party/vitaGL-nosplash` no commit `df63ce8` e aplique:

```text
third_party/patches/vitaGL-nosplash-df63ce8.patch
```

O patch contém as alterações usadas pelo renderer VitaGL do DeltaruneVita.
Depois, `scripts/build-butterscotch-probe.ps1` compila `libvitaGL.a` com as
opções necessárias para o VPK unificado.

## Native GXM

O alvo Native GXM usa um checkout fixado de
`https://github.com/xfangfang/nanovg-gxm.git` passado por
`NANOVG_GXM_SOURCE_DIR`. O código específico do port fica em
`experiments/native-gxm`.
