# Histórico de versões

O desenvolvimento foi feito em ciclos curtos no hardware real. As versões antigas são preservadas como registro técnico e podem estar incompletas ou instáveis.

## v0.33 — enquadramento e navegação

- perfil 113,36 / 115% definido como padrão dos capítulos;
- seletor inicial permanece centralizado em 0,0 / 100%;
- resposta visual do joystick touch acelerada e coordenadas touch corrigidas;
- botão móvel `spr_control_return` ocultado quando Touch está desligado;
- Chapter Select reconhece `launcher` e retorna ao seletor inicial;
- fallback de segurança retorna ao seletor para solicitações inválidas de troca;
- crédito `PSVita port by Woff` adicionado ao rodapé do seletor.

## v0.32 — tela, idioma e touch

- posição e zoom passam a afetar cenário, HUD, diálogos e bordas dinâmicas juntos;
- Game Settings e controles touch permanecem fixos na tela física;
- troca PT-BR/Original aplica imediatamente e reinicia o capítulo aberto;
- arquivos do mod recebem prioridade sobre arquivos equivalentes do save;
- analógico virtual acompanha o movimento com interpolação;
- botões touch aumentam, acendem e afundam quando pressionados.

## v0.31 — jogável

- ajuste manual de posição e zoom da imagem;
- contorno visível durante a calibração;
- valores de tela persistidos;
- Change Chapter retorna ao seletor inicial;
- controles do jogo ficam suspensos durante o ajuste.

## v0.30

- controles touch passam a usar os sprites originais do jogo;
- refinamentos no menu Game Settings e no enquadramento.

## v0.29

- touch funcional e interface de ajustes ampliada;
- correções no carregamento e seleção de mods PT-BR;
- revisão do alinhamento da área de jogo.

## v0.28

- menu Game Settings bilíngue aberto por SELECT;
- opções de touch, mod/idioma, tela e volume;
- preparação de mods por capítulo.

## v0.27

- novo cálculo de resolução e proporção baseado na área real do jogo;
- centralização do framebuffer sem deformar a cena.

## v0.26

- correções de projeção e viewport nos capítulos;
- ajustes das bordas de fundo e interface.

## v0.25

- capítulos 3, 4 e 5 passam pelo mesmo fluxo estável de inicialização;
- melhorias de surfaces e texturas em cenas maiores.

## v0.24

- X definido como confirmação e Círculo como cancelar;
- correções de cenas pretas e efeitos do Chapter 2;
- revisão do enquadramento nativo.

## v0.23

- capítulos 1 e 2 confirmados em gameplay no Vita real;
- retorno seguro entre o seletor e os jogos;
- perfil ampliado de memória validado.

## v0.22

- restauração segura do contexto VitaGL ao trocar de capítulo;
- correção da tela preta após `Initializing Chapter`.

## v0.20

- áudio funcional no seletor;
- inicialização dos capítulos revisada após regressão de contexto gráfico.

## v0.19

- assets da LiveArea e manual incluídos;
- ajustes do template XML;
- primeiro fluxo completo do seletor com assets finais.

## v0.18

- `game_change` passa a abrir chapter1 até chapter5 por reinício limpo do eboot;
- `ATTRIBUTE2=12` aplicado ao VPK.

## v0.17

- seletor de capítulos renderizado e controlável no Vita real;
- renderer de sprites, fontes e primitivas convertido para o VitaGL.

## v0.16

- primeiro VitaRenderer baseado em client arrays;
- remoção dos caminhos imediatos incompatíveis com VitaGL.

## v0.15

- mitigação de crashes na preparação gráfica;
- stack e alocação de memória revistas.

## v0.14

- loop jogável do Butterscotch e mapeamento inicial dos controles;
- mais builtins GameMaker implementados.

## v0.13

- primeiro runner completo empacotado no VPK;
- criação de salas, step e draw ligados no Vita.

## v0.12

- carregamento preguiçoso de salas e texturas;
- redução do pico de RAM dos capítulos.

## v0.11

- páginas de textura maiores carregadas pelo VitaGL;
- logging gráfico detalhado.

## v0.10

- primeira página de textura exibida no Vita;
- ponte inicial entre Butterscotch e VitaGL.

## v0.09

- animação e apresentação VitaGL validadas sem desligar o console;
- isolamento dos primeiros problemas do renderer.

## v0.08

- primeiro VPK de prova usando Butterscotch e VitaGL;
- parser e inicialização gráfica integrados.

## Antes da v0.08

Provas com YoYo Loader, SoLoader e parser de metadados. Foram úteis para validar os dados, logs, LiveArea e limites de memória, mas ainda não formavam um port jogável.
