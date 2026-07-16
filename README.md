# Deltarune Vita

<p align="center">
  <img src="Assets/DeltaruneVita.png" alt="Deltarune Chapters 1–5 on PS Vita" width="900">
</p>

Port jogável de **DELTARUNE Chapters 1–5** para PlayStation Vita.

O projeto executa os dados GameMaker da versão Android através de uma adaptação do [Butterscotch](https://github.com/ButterscotchRunner/Butterscotch), com renderização pelo [VitaGL](https://github.com/Rinnegatamante/vitaGL). A proposta é ter um port nativo para o Vita, com seletor de capítulos, controles físicos e touch, áudio, saves e ajustes próprios para a tela e a memória do console.

> Este repositório não contém os dados comerciais de DELTARUNE. É necessário obter os arquivos do jogo legalmente.

## Release Status

<p align="center">
  <img alt="Overall progress" src="https://img.shields.io/badge/Overall_progress-~85%25-2ea44f?style=for-the-badge">
  &nbsp;
  <img alt="Platform" src="https://img.shields.io/badge/PS_Vita-Butterscotch_+_VitaGL-003791?style=for-the-badge&logo=playstation&logoColor=white">
  &nbsp;
  <img alt="State" src="https://img.shields.io/badge/State-Playable-brightgreen?style=for-the-badge">
</p>

## Estado atual

A versão atual é a **v0.32**. Os capítulos 1 a 5 inicializam e são jogáveis no hardware real.

- seletor e troca de capítulos;
- renderização VitaGL adaptada ao renderer legado do Butterscotch;
- áudio OpenAL;
- controles físicos do Vita;
- controles touch com os gráficos originais do jogo;
- menu Game Settings em inglês e português;
- suporte a mods por capítulo, incluindo preparação de PT-BR;
- volume, touch e modo de tela configuráveis;
- posição e zoom da tela ajustáveis pelo usuário;
- perfil ampliado de memória e carregamento sob demanda;
- log persistente para diagnóstico.

Ainda podem existir diferenças gráficas, de desempenho ou de compatibilidade em cenas específicas. Relatos acompanhados do log ajudam a localizar esses casos.

## Installation

### HOW TO APPLY THE PATCH:

> **The APK-based patcher will be available soon.** It will prepare the required Vita files from a legally obtained DELTARUNE Android APK. No commercial game data will be included with the patcher or the VPK.

Quando disponibilizado, o processo será:

1. Baixar o patcher e o VPK na página de [Releases](https://github.com/WolffsRoom/DeltaruneVita/releases/latest).
2. Colocar uma cópia legal do APK de DELTARUNE na pasta indicada pelo patcher.
3. Executar o patcher e aguardar a criação da pasta `butterscotch`.
4. Instalar o VPK pelo VitaShell ou outro instalador compatível.
5. Copiar a pasta gerada para `ux0:data/deltarune/`.

### Preparação manual atual

Enquanto o patcher não é publicado, extraia legalmente os dados da versão Android para `data/extracted-apk/assets` e execute:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\prepare-butterscotch-data.ps1
```

Depois copie o conteúdo de `data/prepared/deltarune/butterscotch` para:

```text
ux0:data/deltarune/butterscotch/
```

A estrutura deve conter `chapter0` até `chapter5`, cada um com seu `game.droid` e os demais assets do capítulo. Não copie apenas o `game.droid`, pois música, sons e arquivos auxiliares também são necessários.

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
      <td>Confirmar / Interagir</td>
    </tr>
    <tr>
      <td><img src="Assets/SonyButtons/circle.png" width="30" alt="Circle"> <img src="Assets/SonyButtons/square.png" width="30" alt="Square"></td>
      <td>Cancelar / Voltar</td>
      <td><img src="Assets/SonyButtons/triangle.png" width="30" alt="Triangle"></td>
      <td>Menu do jogo</td>
    </tr>
    <tr>
      <td><strong>SELECT</strong></td>
      <td>Abrir Game Settings</td>
      <td><img src="Assets/SonyButtons/analog_l.png" width="30" alt="L"> <img src="Assets/SonyButtons/analog_r.png" width="30" alt="R"></td>
      <td>Page Down / Page Up</td>
    </tr>
    <tr>
      <td><img src="Assets/SonyButtons/touchpad.png" width="34" alt="Touch screen"></td>
      <td>Controles virtuais</td>
      <td>Analógicos em Adjust Screen</td>
      <td>Esquerdo move; direito ajusta o zoom</td>
    </tr>
  </tbody>
</table>

Em **Adjust Screen**, o analógico esquerdo move a imagem e o analógico direito ajusta o zoom. X salva e Círculo restaura o padrão.

## Screenshots

<p align="center">
  <img src="Assets/Screenshots/2026-07-16-021434-989045.png" alt="Deltarune Chapter 2 on PS Vita" width="32%">
  <img src="Assets/Screenshots/2026-07-16-024007-169289.png" alt="Deltarune character creation on PS Vita" width="32%">
  <img src="Assets/Screenshots/2026-07-16-030301-317266.png" alt="Deltarune chapter selector on PS Vita" width="32%">
</p>

## Mods

Mods de PC precisam ser preparados para a estrutura usada no Vita. Coloque os arquivos-fonte em `mods/PTBR` e execute:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\prepare-vita-mods.ps1
```

Copie a pasta `mods` gerada para `ux0:data/deltarune/butterscotch/`. A seleção pode ser feita em Game Settings; quando necessário, o capítulo é reiniciado para aplicar os dados.

## Compilação

Com Docker instalado:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-butterscotch-probe.ps1
```

O resultado será criado em `artifacts/current/Deltarune.vpk`.

Configuração principal:

- Title ID: `DLTVITA01`
- Nome: `Deltarune`
- memória ampliada: `ATTRIBUTE2=12`
- stack principal: 4 MiB
- pool VitaGL: 64 MiB

## Logs

```text
ux0:data/deltarune/butterscotch/butterscotch-probe.log
```

Em caso de crash, anexe também o arquivo `psp2core` gerado pelo Vita.

## Créditos

- DELTARUNE por Toby Fox e sua equipe.
- [Port original para Android](https://gamejolt.com/games/deltarunech1-5androidport/1080568), usado como referência para os dados e a divisão dos capítulos.
- [Butterscotch](https://github.com/ButterscotchRunner/Butterscotch), runner GameMaker de código aberto.
- [VitaGL](https://github.com/Rinnegatamante/vitaGL) por Rinnegatamante.
- [VitaSDK](https://vitasdk.org/) e comunidade homebrew do PlayStation Vita.
- Agradecimento ao [Vita Development Wiki / PSDevWiki](https://www.psdevwiki.com/vita/) pela documentação técnica reunida pela comunidade.

## AI Notice

GPT-5.6 Sol foi usado como apoio no desenvolvimento, diagnóstico, organização do projeto e documentação. As decisões, testes em hardware e direção do port foram conduzidos por Wolff.

## Licença e dados do jogo

As partes derivadas do Butterscotch permanecem sob a Mozilla Public License 2.0. Consulte [LICENSE](LICENSE) e os avisos presentes no código-fonte.

DELTARUNE, seus personagens, músicas e assets pertencem aos respectivos detentores. O repositório e as releases não distribuem os dados necessários para jogar.

O histórico técnico completo está em [docs/PROGRESS.md](docs/PROGRESS.md) e as mudanças por versão em [CHANGELOG.md](CHANGELOG.md).
