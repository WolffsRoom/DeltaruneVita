# Organização do repositório para v0.78

A preparação da v0.78-11 removeu somente saídas reproduzíveis ou cópias
obsoletas. Código-fonte, documentação, logs de teste, ferramentas de análise e
assets usados pelo pacote foram preservados.

## Removido

- caches do Python, Gradle e ferramentas locais;
- diretórios de build do VitaGL e Native GXM;
- objetos e bibliotecas compiladas soltas na raiz;
- arquivos de backup `.bak` substituídos pelo código atual;
- VPKs intermediários v0.78-6 a v0.78-10 em `artifacts/current`;
- backups repetidos, preservando somente o último de cada linha de versão;
- stubs Docker genéricos que não faziam parte do build VitaSDK.

## Backups preservados

- v0.68-140
- v0.69-13
- v0.70-114
- v0.71-118
- v0.72-56
- v0.73-25
- v0.74-22
- v0.75-37
- v0.76-12
- v0.77-17
- v0.78-11

Os backups auxiliares de LiveArea e Vita3K foram mantidos por não representarem
builds repetidos do VPK.

## Dependências externas

O checkout local modificado do VitaGL continua fora do índice principal. Suas
alterações estão preservadas como patch reproduzível em
`third_party/patches/vitaGL-nosplash-df63ce8.patch`.
