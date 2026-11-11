# Sincronização de saves PC ↔ Vita (v0.84): protocolo e uso

## Uso

1. **Vita:** Configurações do port > Sistema > **Sincronizar saves (PC)** = Ligado
   (`save_sync=1` no `config.ini`). Vem desligado. O servidor só roda com o jogo aberto e o
   Wi-Fi ligado.
2. **PC:** `python scripts\save-sync\deltarune_vita_sync.py`, com PC e Vita na mesma rede Wi-Fi.
   Com `pystray` e `Pillow` instalados (`pip install pystray pillow`), o app fica na bandeja;
   sem eles, roda no console. Use `--once` para uma passada só e `--vita <IP>` para pular a
   descoberta.
3. O app procura o Vita, conecta e repete a sincronização a cada 3 s enquanto os dois estão
   ligados. Os backups e o log ficam em `%LOCALAPPDATA%\DeltaruneVitaSync`; os backups do Vita
   ficam em `ux0:data/deltarune_saves/sync_backup/`.

## Regras

- **Arquivos sincronizados:** só `dr.ini`, `vacationmemories.png` e `filech<capítulo>_<slot>`.
  Nunca `true_config.ini`, `keyconfig_*` ou troféus.
- **Mudou de um lado desde a última sincronização:** copia para o outro lado.
- **Mudou dos dois lados:** vence o lado que mudou por último, pelo relógio do PC; a outra cópia
  vai para o backup. Com `--conflict ask`, o arquivo é pulado e registrado no log.
- **Apagou:** nunca é sincronizado.
- **DELTARUNE aberto no PC:** as cópias para o PC esperam o jogo fechar.
- **`dr.ini`:** é comparado de forma normalizada (`"1"` = `"1.000000"`), para não ficar indo e
  voltando entre os lados.
- **Save Anywhere:** os saves feitos com ele são arquivos normais e sincronizam como qualquer
  outro.

## Protocolo (versão 1)

- **Descoberta:** o Vita envia por broadcast UDP para a porta 26540, a cada ~2 s:
  `DRSYNC 1 26541 PSVita\n`.
- **TCP 26541:** uma conexão por vez, comandos em texto terminados por `\n`.

| PC envia | Vita responde |
|---|---|
| `HELLO DRSYNC 1` | `OK DRSYNC 1 PSVita` |
| `LIST` | várias linhas `FILE <nome> <tamanho> <crc32 hex>`, depois `END` |
| `GET <nome>` | `DATA <tamanho>` + os bytes, ou `ERR missing` |
| `PUT <nome> <tamanho>` + os bytes | `OK` ou `ERR ...` |
| `BYE` | fecha a conexão |

- **Gravação:** o Vita grava primeiro em `<nome>.sync_tmp`, move a cópia antiga para
  `sync_backup/<nome>.bak` e só então renomeia o arquivo novo.
- **Limites:** nomes fora da lista são recusados e o tamanho máximo é 4 MiB.
- **Thread:** prioridade baixa (padrão + 20); dorme 200 ms entre verificações de conexão.

## Teste local (PC)

`scratch/review-v081/synctest.py` compila o mesmo `vita_save_sync.c` como servidor de teste no
Windows (`-DSAVE_SYNC_DESKTOP_TEST`) e roda o cliente com saves reais do Cap. 5. Os testes
passaram em: descoberta, primeira cópia, idempotência, PC → Vita, Vita → PC, backups dos dois
lados, arquivo novo, `dr.ini` normalizado e nome com caminho recusado.

## Ainda não feito

- **PIN de pareamento:** sem ele, qualquer aparelho da mesma rede consegue enviar saves ao Vita
  (apenas nomes de save permitidos).
- **Executável do app do PC:** falta empacotar com PyInstaller.
- **Teste no Vita:** nada disso foi testado no Vita ainda.
