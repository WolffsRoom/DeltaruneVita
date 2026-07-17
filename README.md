# Deltarune Vita

<p align="center">
  <img src="Assets/DeltaruneVita.png" alt="Deltarune Chapters 1–5 on PS Vita" width="900">
</p>

Port não oficial de **DELTARUNE Chapters 1–5** para PlayStation Vita.

A partir da v0.36, o projeto passou a executar diretamente os dados GameMaker da versão Windows/Steam por meio de uma adaptação do [Butterscotch](https://github.com/ButterscotchRunner/Butterscotch), com renderização pelo [VitaGL](https://github.com/Rinnegatamante/vitaGL). A versão Android não é mais a fonte principal dos dados.

> Este repositório e suas releases não incluem arquivos comerciais de DELTARUNE. Compre e obtenha o jogo oficial em [deltarune.com](https://deltarune.com/).

<p align="center">
  <img src="https://deltarune.com/assets/images/key-art.gif" alt="DELTARUNE official key art" width="760">
</p>

## Release status

<p align="center">
  <img alt="Overall progress" src="https://img.shields.io/badge/Overall_progress-~90%25-2ea44f?style=for-the-badge">
  &nbsp;
  <img alt="Platform" src="https://img.shields.io/badge/Source-PC%2FSteam-003791?style=for-the-badge&logo=steam&logoColor=white">
  &nbsp;
  <img alt="State" src="https://img.shields.io/badge/State-Playable-brightgreen?style=for-the-badge">
</p>

A versão atual é a **v0.40**. Os cinco capítulos inicializam e são jogáveis em hardware real, embora algumas cenas e efeitos ainda estejam em validação.

## O que já funciona

- seletor e troca dos cinco capítulos;
- retorno ao Chapter Select pelo menu do jogo;
- leitura direta dos arquivos Windows/Steam;
- renderer VitaGL adaptado ao backend legado do Butterscotch;
- controles físicos do Vita;
- controles touch opcionais;
- menu Game Settings em inglês e português;
- volumes separados para música e efeitos;
- posição e zoom da tela configuráveis;
- bordas da versão de console selecionadas por capítulo;
- carregamento sob demanda e cache de texturas para os capítulos maiores;
- saves, mods por capítulo e preparação de PT-BR;
- logs persistentes para diagnóstico.

## Mudança de direção

O trabalho começou estudando ports Android e o carregamento por YoYo Loader/SoLoader. Essa etapa permitiu entender a divisão dos capítulos, os arquivos externos, a inicialização do runner e os controles touch.

Depois dos primeiros testes com Butterscotch e VitaGL, o port passou a carregar os dados oficiais da versão Windows. Isso remove a dependência do APK e evita manter os problemas específicos do runner Android.

O fluxo atual é:

```text
Arquivos oficiais PC/Steam
        ↓
Preparação dos dados por capítulo
        ↓
Butterscotch adaptado ao Vita
        ↓
VitaGL + OpenAL + controles Vita
```

## Installation

### HOW TO APPLY THE PATCH:

O patcher para preparar os arquivos a partir de uma instalação oficial de PC/Steam será disponibilizado em breve.

Quando publicado, o processo será:

1. Comprar e instalar [DELTARUNE para PC](https://deltarune.com/).
2. Baixar o patcher e o VPK na página de [Releases](https://github.com/WolffsRoom/DeltaruneVita/releases/latest).
3. Executar o patcher apontando para a instalação oficial.
4. Instalar `Deltarune.vpk` pelo VitaShell.
5. Copiar a pasta gerada para `ux0:data/deltarune/deltarunevita/`.

Os fundos de console são distribuídos separadamente. A pasta `borders` deve ficar em:

```text
ux0:data/deltarune/deltarunevita/borders/
```

### Preparação manual para desenvolvimento

Coloque uma instalação legítima em `SteamFiles/DELTARUNE` e execute:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\prepare-windows-data.ps1
```

Os dados preparados serão criados em:

```text
data/prepared/deltarune/deltarunevita/
```

Para compilar o VPK com Docker e VitaSDK:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-butterscotch-probe.ps1
```

## Controles

<table>
  <thead>
    <tr><th>Controle</th><th>Ação</th><th>Controle</th><th>Ação</th></tr>
  </thead>
  <tbody>
    <tr>
      <td><img src="Assets/SonyButtons/up.png" width="30" alt="Up"> <img src="Assets/SonyButtons/down.png" width="30" alt="Down"> <img src="Assets/SonyButtons/left.png" width="30" alt="D-Pad"> / Analógico esquerdo</td>
      <td>Movimento</td>
      <td><img src="Assets/SonyButtons/cross.png" width="30" alt="Cross"></td>
      <td>Confirmar / interagir</td>
    </tr>
    <tr>
      <td><img src="Assets/SonyButtons/circle.png" width="30" alt="Circle"> <img src="Assets/SonyButtons/square.png" width="30" alt="Square"></td>
      <td>Cancelar / voltar</td>
      <td><img src="Assets/SonyButtons/triangle.png" width="30" alt="Triangle"></td>
      <td>Menu do jogo</td>
    </tr>
    <tr>
      <td><strong>SELECT</strong></td>
      <td>Abrir Game Settings</td>
      <td><img src="Assets/SonyButtons/analog_l.png" width="30" alt="L"> <img src="Assets/SonyButtons/analog_r.png" width="30" alt="R"></td>
      <td>Navegar entre categorias</td>
    </tr>
    <tr>
      <td><img src="Assets/SonyButtons/touchpad.png" width="34" alt="Touch screen"></td>
      <td>Controles virtuais</td>
      <td>Analógicos em Adjust Screen</td>
      <td>Esquerdo move; direito ajusta o zoom</td>
    </tr>
  </tbody>
</table>

## Screenshots

<p align="center">
  <img src="Assets/Screenshots/2026-07-16-021434-989045.png" alt="Deltarune Chapter 2 on PS Vita" width="32%">
  <img src="Assets/Screenshots/2026-07-16-024007-169289.png" alt="Deltarune character creation on PS Vita" width="32%">
  <img src="Assets/Screenshots/2026-07-16-030301-317266.png" alt="Deltarune chapter selector on PS Vita" width="32%">
</p>

## Vídeo

<p align="center">
  <a href="https://www.youtube.com/watch?v=yDzgiGdekas">
    <img src="https://img.youtube.com/vi/yDzgiGdekas/maxresdefault.jpg" alt="Deltarune Vita video" width="760">
  </a>
</p>

## Mods

O suporte PT-BR usa como referência a tradução comunitária [teiarruma/deltarune-ptbr](https://github.com/teiarruma/deltarune-ptbr). Os arquivos da tradução não são distribuídos neste repositório ou nas releases.

Depois de obter a tradução no projeto original, coloque-a em `mods/PTBR` e execute:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\prepare-vita-mods.ps1
```

## Histórico recente

| Versão | Mudanças principais |
|---|---|
| v0.36 | Início da migração dos dados Android para os arquivos Windows/Steam. |
| v0.37 | Ajustes no carregamento do runner Windows, fontes e áudio externo. |
| v0.38 | Retorno à biblioteca VitaGL estável e diagnóstico do primeiro frame. |
| v0.39 | Correção do crash causado pelo overlay dos controles touch. |
| v0.40 | Música externa, novo Game Settings, Chapter Select, cache de texturas e bordas de console. |

As versões anteriores documentam a fase de pesquisa com Android, os probes gráficos e a evolução inicial do runner.

## Estrutura no Vita

```text
ux0:data/deltarune/
├── config.ini
├── save/
└── deltarunevita/
    ├── chapter0/
    ├── chapter1/
    ├── chapter2/
    ├── chapter3/
    ├── chapter4/
    ├── chapter5/
    ├── music/
    ├── borders/
    └── mods/
```

O log principal é gravado em:

```text
ux0:data/deltarune/deltarunevita/butterscotch-probe.log
```

## Créditos

- DELTARUNE por Toby Fox e sua equipe. [Site oficial e compra](https://deltarune.com/).
- [Deltarune Chapters 1–5 Android Port](https://gamejolt.com/games/deltarunech1-5androidport/1080568), referência importante durante a pesquisa inicial.
- [Deltarune Android Port por AngelaPuzzle e colaboradores](https://angelapuzzle.wixsite.com/dt-port), fundamental para entender a adaptação dos capítulos, recursos externos, touch e bordas. Os gráficos dos controles touch usados como base neste port vieram desse trabalho.
- [Butterscotch](https://github.com/ButterscotchRunner/Butterscotch), runner GameMaker de código aberto.
- [VitaGL](https://github.com/Rinnegatamante/vitaGL) por Rinnegatamante.
- [VitaSDK](https://vitasdk.org/) e a comunidade homebrew do PlayStation Vita.
- [Vita Development Wiki / PSDevWiki](https://www.psdevwiki.com/vita/) pela documentação técnica.
- [Tradução PT-BR de DELTARUNE](https://github.com/teiarruma/deltarune-ptbr) pela equipe TEIARRUMA e colaboradores.

## AI Notice

GPT-5.6 Sol foi usado como apoio no desenvolvimento, diagnóstico, organização e documentação. A direção do port e os testes em hardware real foram conduzidos por Wolff.

## Licença e dados do jogo

As partes derivadas do Butterscotch permanecem sob a Mozilla Public License 2.0. Consulte [LICENSE](LICENSE).

DELTARUNE, personagens, músicas e recursos pertencem aos respectivos detentores. Este projeto não distribui os dados comerciais necessários para jogar.
