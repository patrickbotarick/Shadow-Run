# Materiais VFX · Montagem manual

## Aditivo para luz, energia, faíscas e explosão

- Material Domain Surface, Blend Mode Additive, Shading Model Unlit e Two Sided ligado.
- `Emissive Color = Texture.RGB × Texture.A × Tint × Intensity × Opacity × Particle Color.RGB × Particle Color.A`.
- A intensidade inicial é 3. Use o alfa do PNG e da partícula na cor emissiva. O material aplica a máscara nesse ponto.
- Para usar uma versão estática em um ator fora do Niagara, use uma variante do material com a cor e o alfa da partícula substituídos por constantes iguais a 1.

## Translúcido para fumaça

- Surface, Translucent, Unlit e Two Sided ligado.
- `Emissive Color = Texture.RGB × Tint × Intensity × Particle Color.RGB`.
- `Opacity = Texture.A × OpacityParameter × Particle Color.A`.
- Comece com Intensity 0,8 e OpacityParameter 0,75. Reduza emissividade se a fumaça ficar clara demais. Um Depth Fade suave pode ajudar em interseções com o piso.

## Escudo Fresnel

- Surface, Translucent e Unlit. Comece com faces externas da esfera e Two Sided desligado.
- `Emissive Color = Fresnel × TintCiano × Intensity`.
- `Opacity = Fresnel × OpacityParameter`.
- Intensidade inicial 2,5 e opacidade 0,48. Anime o parâmetro de opacidade para a bolha aparecer no acerto e desaparecer em seguida.
- Para marcar onde o escudo foi atingido, sobreponha a textura Escudo Hex em um quad tangente ao ponto de impacto. O quad pode usar o material aditivo.

## Fragmento metálico

- Surface, Opaque, Default Lit.
- Base Color cinza grafite, Metallic 0,85 e Roughness 0,50.

## Flipbooks

Em um Sprite Renderer, configure Sub Image Size como **4 × 4** e avance SubImageIndex de 0 a 15 com a idade normalizada. O material usa o PNG completo. Para uma primeira montagem, mantenha a interpolação entre quadros desativada e valide a ordem.
