# Deltarune Vita

Port experimental de DELTARUNE Chapters 1–5 para PlayStation Vita.

O projeto usa o [Butterscotch](https://github.com/ButterscotchRunner/Butterscotch) para interpretar os arquivos do GameMaker e o VitaGL como backend gráfico. O objetivo atual é carregar o seletor de capítulos, iniciar cada jogo e adaptar os recursos necessários ao hardware do Vita.

## Estado atual

A versão mais recente é a `00.18`.

Confirmado no Vita real:

- carregamento do `chapter0`;
- menu de capítulos funcionando;
- controles do Vita;
- primeiro quadro e texturas renderizados;
- renderer convertido de `glBegin/glEnd` para arrays;
- solicitação de troca de capítulo identificada.

A versão `00.18` implementa a troca para `chapter1` até `chapter5` por reinício limpo do eboot. Esta parte ainda precisa de validação no hardware. O áudio continua desativado enquanto o carregamento e a renderização são estabilizados.

## Controles

- Direcional ou analógico esquerdo: movimento
- X ou O: confirmar
- Quadrado: cancelar
- Triângulo ou START: menu
- L e R: Page Down e Page Up
- SELECT: sair

## Preparação dos dados

Os arquivos do jogo não fazem parte deste repositório.

Extraia legalmente `chapter0.wad` até `chapter5.wad` para `data/extracted-apk/assets` e execute:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\prepare-butterscotch-data.ps1
```

Copie o resultado para:

```text
ux0:data/deltarune/butterscotch/
```

A estrutura no Vita deve ficar assim:

```text
ux0:data/deltarune/butterscotch/chapter0/game.droid
ux0:data/deltarune/butterscotch/chapter1/game.droid
ux0:data/deltarune/butterscotch/chapter2/game.droid
ux0:data/deltarune/butterscotch/chapter3/game.droid
ux0:data/deltarune/butterscotch/chapter4/game.droid
ux0:data/deltarune/butterscotch/chapter5/game.droid
```

Depois instale o VPK e abra `Deltarune` pela LiveArea.

## Compilação

Com Docker instalado:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-butterscotch-probe.ps1
```

O VPK será criado em `artifacts/current/Deltarune.vpk`.

O build usa:

- Title ID `DLTVITA01`;
- nome `Deltarune`;
- `ATTRIBUTE2=12` para o perfil ampliado de memória;
- stack principal de 4 MiB;
- pool de 64 MiB para o VitaGL.

## Log

O arquivo de diagnóstico fica em:

```text
ux0:data/deltarune/butterscotch/butterscotch-probe.log
```

Em caso de crash, envie também o `psp2core` gerado pelo sistema.

## Histórico

O caminho percorrido até a versão atual está em [docs/PROGRESS.md](docs/PROGRESS.md).

## Aviso

Este repositório não inclui APK, WAD, `game.droid`, áudio ou outros dados comerciais de DELTARUNE. Use somente arquivos obtidos legalmente.
