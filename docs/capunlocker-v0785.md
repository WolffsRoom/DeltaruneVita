# CapUnlocker e otimizações avaliadas para v0.78-5

## Integração

O executável tenta criar trabalhos pesados e independentes no quarto núcleo do
PS Vita (`0x00080000`). Esse núcleo só é aceito para o jogo quando o
CapUnlocker está instalado em `*KERNEL`.

Foram movidos para essa política:

- geração inicial do cache de texturas;
- decodificação dos PNGs usados como fallback da abertura;
- decodificação por frames usada como fallback de vídeo.

O CapUnlocker permanece opcional. Quando o sistema rejeita o quarto núcleo, o
mesmo trabalho é recriado sem afinidade fixa e segue nos núcleos normais. Áudio,
VM do GameMaker e chamadas VitaGL continuam com a política anterior para evitar
corridas e uso do contexto gráfico fora da thread proprietária.

## Verificação no Vita

Com Dev Mode ativo, procure no `butterscotch-probe.log`:

```text
CAPUNLOCKER=texture_generator core=4 enabled=1
CAPUNLOCKER=first_load_decoder core=4 enabled=1
CAPUNLOCKER=video_frame_decoder core=4 enabled=1
```

`core=default enabled=0 fallback=1` indica que o plugin não liberou o núcleo e
que o fallback compatível foi usado.

## Técnicas do MH3 avaliadas

Já presentes ou em desenvolvimento no DeltaruneVita:

- texturas comprimidas e conversão antecipada de assets;
- cache e preparação de texturas fora do primeiro uso visível;
- decodificação de vídeo por hardware;
- especialização de shaders e redução de cópias/desenhos;
- backend GXM nativo experimental com shaders offline e controle direto de
  memória gráfica.

Não aplicadas ao backend VitaGL desta build:

- render thread e pacotes de frame: o contexto VitaGL e as surfaces do
  GameMaker dependem de ordem estrita e pertencem à thread de renderização;
- PGO/ThinLTO: o toolchain atual usa GCC e ainda não existe perfil representativo
  coletado no hardware para orientar PGO; habilitar LTO sem medir aumentaria o
  risco de regressão e o tempo de build;
- substituição total de áudio, I/O e renderização: são mudanças arquiteturais e
  pertencem ao experimento GXM, não a uma correção incremental da v0.78.

O ganho esperado desta integração está nos carregamentos e decodificações que
podem ocorrer em paralelo. Cenas limitadas pela GPU ou pela VM principal não
ganham FPS apenas por instalar o CapUnlocker.
