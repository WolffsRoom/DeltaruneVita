# Auditoria dos trofÃ©us â€” v0.71

Data da auditoria: 2026-09-01

## Escopo

A auditoria compara os gatilhos compilados nos `data.win` com o catÃ¡logo nativo
de 30 trofÃ©us e acompanha o caminho completo atÃ© o Vita:

1. condiÃ§Ã£o original do jogo;
2. `obj_event_manager.trigger_event()` / `trophies_queue`;
3. ponte do Runner para `trophies.ini`;
4. fila assÃ­ncrona do NoTrpDrm;
5. banco de trofÃ©us do sistema.

Trata-se de uma validaÃ§Ã£o estÃ¡tica do bytecode e da ponte nativa. Ela confirma
que o caminho de execuÃ§Ã£o existe, mas os eventos raros ainda devem ser
confirmados jogando no Vita.

## Resultado do catÃ¡logo

Os IDs 1â€“29 possuem pelo menos um gatilho real nos `data.win` originais dos
Chapters 1â€“4. O ID 0 (`CONNECTED`) Ã© a platina e passa a ser consolidado pelo
Runner quando todos os outros 29 estiverem marcados.

| IDs | Conquista | CondiÃ§Ã£o auditada |
|---:|---|---|
| 1â€“4 | CHAPTER END | conclusÃ£o de cada Chapter |
| 5â€“8 | PAIN | contador de dano por Chapter |
| 9â€“12 | CONSUMPTION | consumo de item por Chapter |
| 13 | FAILURE | Game Over/falha |
| 14â€“17 | EQUIPMENT | equipar armadura por Chapter |
| 18 | CLOSENESS | evento de proximidade/amizade |
| 19 | REINFORCEMENT | aumento de forÃ§a no Chapter 2 |
| 20 | CUTLERY | classificaÃ§Ã£o correspondente no Chapter 3 |
| 21 | SYMPHONY | classificaÃ§Ã£o correspondente nos Chapters 3/4 |
| 22 | BEAST | eventos de destruiÃ§Ã£o previstos pelo jogo |
| 23 | TREASURE HUNTER | contador de tesouros |
| 24 | CLIMB | permanÃªncia de 90 segundos na parede |
| 25 | THRONE | investigaÃ§Ã£o do trono |
| 26 | SWORD | equipar arma |
| 27 | RING | evento `trigger_event(..., 27)` do jogo |
| 28 | MIRROR | interaÃ§Ã£o/evento do reflexo |
| 29 | ERASE | evento de apagar/limpar |

O `TROPCONF.SFM` contÃ©m exatamente os IDs 000â€“029, com 000 como platina e os
demais ligados a ela. NÃ£o foi encontrada divergÃªncia de numeraÃ§Ã£o entre o
catÃ¡logo in-game e o pacote nativo.

## Compatibilidade por conjunto de dados

| Dados | Ch1 | Ch2 | Ch3 | Ch4 | Resultado |
|---|---:|---:|---:|---:|---|
| Original | OK | OK | OK | OK | todos os IDs cobertos |
| Spanish | OK | OK | OK | OK | equivalente ao original |
| German | OK | OK | OK | OK | equivalente ao original |
| Russian | OK | OK | OK | OK | equivalente ao original |
| Turkish | OK | OK | OK | OK | equivalente ao original |
| French | OK | OK | OK | OK | equivalente ao original |
| Portuguese-BR | **falha** | **falha** | OK | OK | Ch1/Ch2 nÃ£o contÃªm a cadeia GML de trofÃ©us |
| Italian (Pendente) | â€” | â€” | â€” | â€” | fora do conjunto pronto para teste |

Nos Chapters 1 e 2 do PT-BR nÃ£o existe
`gml_Object_obj_event_manager_Create_0`, nem os `trigger_event()` presentes no
jogo original. O fallback nativo de arma/armadura cobre somente os IDs de
equipamento; ele nÃ£o torna os demais eventos equivalentes ao original.

Portanto, **nÃ£o Ã© correto declarar suporte completo a trofÃ©us no PT-BR Ch1/Ch2
atÃ© que os gatilhos sejam reinjetados semanticamente nesses `data.win`**. Copiar
scripts inteiros do inglÃªs nÃ£o Ã© seguro porque pode sobrescrever cÃ³digo e texto
da traduÃ§Ã£o.

## CorreÃ§Ãµes feitas no Runner

- A fila pendente `trophies_queue` Ã© lida imediatamente; nÃ£o Ã© preciso trocar de
  sala para persistir a conquista.
- `trophies.ini` Ã© reconciliado com o sistema Vita a cada inicializaÃ§Ã£o.
- A platina local Ã© marcada apÃ³s os 29 trofÃ©us de gameplay.
- O ID de platina devolvido pelo serviÃ§o NP Trophy passa a ser registrado.
- A detecÃ§Ã£o auxiliar de equipamento aguarda um estado estÃ¡vel apÃ³s carregar o
  save, evitando premiar SWORD/EQUIPMENT apenas por iniciar o jogo.
- Cada tentativa nativa passa a registrar ID, resultado e platina no probe para
  permitir diagnÃ³stico no hardware.

## PrÃ³ximo trabalho necessÃ¡rio

Criar uma etapa no builder que injete somente os gatilhos semÃ¢nticos ausentes no
PT-BR Chapters 1 e 2 e que interrompa o build se qualquer ID esperado desaparecer.
Depois disso, regenerar os respectivos `data.win` e caches de textura e repetir
a auditoria antes do teste no Vita.

