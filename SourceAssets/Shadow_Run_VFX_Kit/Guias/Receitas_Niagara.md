# Receitas de Niagara · Shadow Run

Para efeitos únicos, use Burst e termine o sistema ao fim das partículas. Use os tempos e as quantidades abaixo como ponto de partida. Quando houver Sprite e Mesh no mesmo efeito, use emissores distintos. Para os flipbooks, configure a grade 4 × 4 no Sprite Renderer.

| Sistema | Render e parâmetros iniciais | Gatilho |
|---|---|---|
| NS_Tiro_Flash | Sprite Flash Tiro, 1 partícula, 0,045 s, 18–28 cm, âmbar. Anexe ao socket da boca da carabina. | Disparo confirmado |
| NS_Tiro_Tracante | Mesh Traçante Cruzado com material Traçante, 1 partícula, 0,045 s. Oriente +X da boca ao ponto final do trace. Use a distância para a escala em X e posicione no meio do segmento. | Mesmo evento de disparo |
| NS_Impacto_Metal | Sprite Impacto Metal, 0,12 s, 16 cm. Emissor de 6 faíscas âmbar orientadas pela normal do hit, 0,18 s. | Projétil ou trace acertou metal |
| NS_Impacto_Escudo | Quad hexagonal ciano, 22 cm, 0,18 s. Oriente pela normal do impacto. Faça a esfera de Fresnel piscar por 0,25 s. | Dano absorvido pelo escudo |
| NS_Escudo_Quebra | Anel Onda ciano, 0,25 s, 50 → 130 cm. Acrescente 8 faíscas curtas. Para uma onda em volume, use a Cúpula com Fresnel em lugar do anel. | Escudo cruza de positivo para zero |
| NS_Drone_Propulsor | Sprite Brilho Radial, 2–4 partículas/s por propulsor, ciano discreto, 10 cm, 0,24 s, espaço local. | Enquanto o motor do drone está ativo |
| NS_Drone_Carga | Flare Energia, 1 sprite anexado ao núcleo. Cresça durante o tempo real de aviso do ataque. Vermelho no Sentinela e no Caçador, âmbar no Bastião. | Início da preparação do ataque |
| NS_Drone_Pulso | Brilho Radial, 1 sprite anexado ao projétil. Use 14 cm no comum e 24 cm no pesado. | Spawn do projétil |
| NS_Drone_Pane | 4–7 faíscas brancas e 1 sprite de fumaça, 0,25–0,6 s. Reduza o brilho do núcleo enquanto a janela durar. | Estado de vulnerabilidade após rajada |
| NS_Drone_Explosao | Flipbook Explosão âmbar, 0,32 s. Flipbook Fumaça, 0,65 s. Mesh Fragmento Metal, 4–6 peças, 0,5 s. | Destruição, uma vez por drone |
| NS_Celula_Coleta | Anel Energia com 1 sprite ciano que cresce de 22 a 72 cm, 0,28 s. Use o ponto do pickup e faça o alfa cair no fim. | Coleta da célula confirmada |
| NS_Terminal_Ativar | Mesh Anel com textura Anel Energia, 30 → 100 cm, 0,38 s. Flare ciano curto no socket do terminal. | Inserção aceita pelo terminal |
| NS_Porta_Abrir | 2 sprites do flipbook Fumaça, 0,55 s, próximos às guias laterais. Acione após a trava soltar. | Início da animação da porta |

## Integração com as salas e o combate

- Aplique o dano pelo sistema de combate. Os efeitos respondem aos resultados de disparo, acerto, absorção, quebra e destruição.
- Vincule emissores contínuos aos drones e pare a emissão quando o ator morrer ou quando o módulo for descarregado. Efeitos únicos podem permanecer no mundo até terminar, desde que o sistema encerre sozinho.
- Para impactos e explosões, combine os efeitos com os WAVs do kit de som existente. Na porta, dispare o VFX de fumaça pelo evento da animação.
- Na primeira passada, valide tiro, impacto, morte do drone, escudo e coleta da célula. Revise o tamanho e a exposição no corredor escuro antes de aumentar a intensidade.

## Referências

- Niagara Renderers: https://dev.epicgames.com/documentation/en-us/unreal-engine/render-module-reference-for-niagara-effects-in-unreal-engine
- Flipbook no Niagara: https://dev.epicgames.com/documentation/unreal-engine/niagara-flipbook-baker-quick-start-guide-in-unreal-engine
- Modos de material: https://dev.epicgames.com/documentation/en-us/unreal-engine/material-blend-modes-in-unreal-engine
