# GXM41 — Chapter 5: cores, alfa e Room Navigator

Análise do log físico de 14/10/2026: GXM39 (mask=0x1FFF) saltou da room54 para room225, denominada room_dw_fcastle_final_save. Após o salto, mediu cerca de 12,12 FPS em 5 segundos, ativou shd_castlereflect e shd_castlereflect_layerblend e o log parou sem dump. Não prova se a causa é GPU ou runner. Cache antes do salto: ~46 MiB, pico ~52 MiB com mais de 110 evictions.

## Comparação visual direta Vita3K
room_town_mid (room22), RGBA4444 GXM39 shaders OFF versus RGBA8888 GXM30 shaders OFF: diferença absoluta média R 3,2151 / G 5,5161 / B 3,3485, média 4,0266 por canal de 0–255; zero pixels com divergência superior a 15. Empacotamento RGBA4444 R/G/B/A da GXM39 está correto nesta sala. Não alterar novamente a ordem dos canais.
GXM39 shader ON vs OFF em room22: diferenças médias R 0,1806 / G 0,1579 / B 0,1069; 0,647% dos pixels têm diferença > 15, possivelmente animação. Na sala22 não foram reproduzidas as cores azuis/rosas da interface apontadas pelo usuário, sendo necessário identificar essa interface específica.

Capturas: artifacts/experiments/v0.78-native-gxm/GXM39-Vita3K-Ch5-TownMid-RGBA4444-NoShaders.png e ...-ShaderON.png, referência RGBA8888 em Preview Shaders/GXM30-Vita3K-Ch5-TownMid-RGBA8888-NoShaders.png.

## Reproduções de sala225
- GXM39: sala225 carregada diretamente com shaders ON e OFF, sem travar no Vita3K.
- GXM40: salto sequencial 1→54→225 com shaders ON, rodando ~21 FPS após a transição; congelamento não reproduzido.
- GXM41: mesmo salto, mais proteção de memória. Log: dev_transition_purge pages=20 bytes_before=29532160 bytes_after=0. Foram liberados 28,16 MiB de atlas antes da nova sala. Recarregamento por demanda, missing=0; sala225 rodando por múltiplas janelas após o salto, aproximadamente 21–29 FPS. Captura: artifacts/experiments/v0.78-native-gxm/GXM41-Vita3K-Ch5-CastleTown-To-Room225-ShaderON.png.

## Correções GXM41
- Mantidos os 13 shaders GXP, cores RGBA4444 e RGBA8888 das fontes, sem desativar efeitos.
- Finaliza o indicador frameIsActive após gxmSwapBuffer.
- Exclusivamente para saltos DEV Room Navigator, espera GPU terminar (sceGxmFinish), coleta aposentados e libera atlas de texturas da sala antiga com limpeza dos samplers auxiliares. Mantém surfaces de propriedade GameMaker e dados originais; páginas necessárias serão recarregadas na sala destino. Transições normais permanecem inalteradas.
- Novos logs dev_jump_phase gpu_fence_begin / gpu_fence_complete, dev_transition_purge e castle_phase nas primeiras etapas da room225.
- Suporte ao marcador temporário de teste no emulador gxm-emulator-room.txt com conteúdo 54,225.

## Build
VPK: artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm41.vpk
Title ID DLTAGXM41. Tamanho 1.450.910 bytes.
SHA256 86cc90464807844c2a5ea3d80abc317e959672694cc9b4acfecb6ffdd04d50ff.
CRC ZIP e scripts de regressão PASS.
GXM39 e GXM40 preservadas.

## Próxima validação
Testar a GXM41 no Vita físico, da Castle Town à room225 via DEV Navigator por ao menos 30 segundos. O congelamento original não foi reproduzido no Vita3K, portanto a solução ainda não está comprovada em hardware.
Caso persistam cores rosas/azuis, indicar a sala e uma foto da interface. Para isolar o formato de textura: gxm-texture-mode.txt com 2 força RGBA8888, porém consome o dobro de memória de textura. Verificar com shaders ON/OFF em testes separados.
O Room Navigator salta preparação narrativa, então room225 não é prova da batalha final jogada normalmente.
