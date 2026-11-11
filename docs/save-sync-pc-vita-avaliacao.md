# Sincronização de saves PC ↔ Vita sem FTP: avaliação

**Data:** 10/11/2026. Referências: `DeltaruneSync-Windows.zip` (`deltarune_sync.py`, README) e o
LAN do port Lightners (`Deltarune Lightners Live Plus/game/ui/dr_lan.gd`).

## 1. "Ad-hoc" de verdade não serve para o PC

O modo ad-hoc do Vita (`sceNetAdhoc*`, `sceNetAdhocMatching`) é o protocolo de rede local
Vita↔Vita/PSP. Um PC com Windows não entra nele. O multiplayer do Lightners também **não** usa
esse ad-hoc: `dr_lan.gd` faz descoberta por broadcast UDP (porta 26534) e conexão ENet (26533)
na mesma rede Wi-Fi. É esse o modelo viável aqui: um canal direto na rede local, sem FTP e sem
VitaShell, com o PC e o Vita no mesmo roteador. (Um hotspot do próprio PC também funciona.)

## 2. Compatibilidade dos saves (boa)

O port roda o mesmo `data.win` do Windows, então os arquivos têm o mesmo formato:

| Arquivo | Sincroniza? | Observação |
|---|---|---|
| `filechN_M` (N = capítulo, M = slot; 9 = backup automático do jogo) | sim | mesmo formato texto |
| `dr.ini` (conclusão de capítulos, extras) | sim | o PC e o Vita escrevem números em formatos diferentes (`"1"` × `"1.000000"`); o `fhash` do script já normaliza isso, manter |
| `vacationmemories.png` | sim | |
| `true_config.ini`, `keyconfig_*.ini`, `trophies.ini`, `steam_autocloud.vdf` | **não** | específicos de cada plataforma (o script já exclui) |

- Vita: `ux0:data/deltarune_saves/` (`SAVE_PATH` em `playable_main.c`). PC: `%LOCALAPPDATA%\DELTARUNE`.
- Traduções (mods de idioma) usam o mesmo formato de save.
- **Save Anywhere (v0.83-6)**: o save é o do próprio `obj_savemenu`, então guarda a sala e o
  jogo do PC carrega normalmente nela. Combina sem mudança de formato.
- **Steam Cloud**: ao abrir o jogo, a Steam pode trocar os arquivos locais pelos da nuvem. O app
  do PC precisa tratar a Steam como uma terceira cópia (sincronizar com o jogo fechado, como o
  script já faz ao esperar o DELTARUNE fechar).

## 3. Arquitetura proposta

**Vita (dentro do `vitagl.bin`, opcional):**
- Opção nova nas Configurações do port: "Sincronizar saves com o PC" (`save_sync=1` no
  `config.ini`, desligada por padrão).
- Com a opção ligada e o jogo aberto, uma thread de rede de baixa prioridade:
  - anuncia o Vita por broadcast UDP a cada 2 s (token, versão do protocolo, nome do console);
  - aceita **uma** conexão TCP do PC pareado.
- Protocolo pequeno e binário: `HELLO` com versão e PIN, `LIST` (nome, tamanho, hash, contador
  de versão), `GET`, `PUT` e `DELETE` (este nunca usado, como no script).
- Gatilho imediato: quando o jogo grava um `filech*` ou o `dr.ini`, o port avisa o PC na hora,
  por um gancho no sistema de arquivos do Butterscotch.
- Quando chega um save do PC, ele é gravado como `.tmp` e depois renomeado. O jogo só relê os
  saves no menu de carregar, então gravar durante a partida não mexe no estado atual. Antes de
  sobrescrever, a cópia antiga vai para `deltarune_saves/sync_backup/`.

**PC (app de bandeja):**
- Reaproveitar a lógica boa do script atual: regras de decisão (`decide`), normalização do
  `dr.ini`, backups e restauração, e espera pelo jogo fechado. Trocar a camada FTP pelo novo
  protocolo.
- Ícone na bandeja (`pystray` ou WinForms), monitor de pasta com `watchdog` para enviar assim
  que o PC salva, e opção "iniciar com o Windows".
- **Pareamento:** na primeira conexão, o Vita mostra um PIN de 4 dígitos que é digitado no PC.
  Assim, outro aparelho da rede não consegue gravar saves no Vita.

**Decisão de conflito:** relógios do Vita e do PC não são confiáveis (o FTP do Vita só dá
minutos, por isso o `TIE_WINDOW` de 90 s). Usar hash + "última versão vista por cada lado"
(o estado que o script já guarda) em vez de horário. Mudou só de um lado: copia. Mudou dos dois:
o mais recente vence e o outro vai para o backup. Empate: pergunta ao usuário.

## 4. Esforço e riscos

| Parte | Tamanho estimado |
|---|---|
| Vita: thread de rede, protocolo, gancho de gravação, opção nas configurações | ~600 a 900 linhas em C |
| PC: novo transporte no script, bandeja, pareamento | ~400 linhas Python (e empacotar o .exe) |

**Riscos:**
- O Wi-Fi do Vita precisa ficar ligado durante o jogo, o que gasta mais bateria. Por isso a opção
  é opcional e vem desligada.
- A thread de rede não pode competir com o jogo: prioridade baixa, sem trabalho por quadro.
- **Steam Cloud:** pode trocar os arquivos locais pelos da nuvem ao abrir o jogo no PC.
- **Autoria:** o app é de outra pessoa. Para torná-lo oficial, vale combinar com o autor (crédito
  e licença do `deltarune_sync.py`).

**Recomendação:** fazer em duas etapas. Primeiro o lado do Vita com um cliente de teste em
linha de comando no PC; depois a bandeja.
