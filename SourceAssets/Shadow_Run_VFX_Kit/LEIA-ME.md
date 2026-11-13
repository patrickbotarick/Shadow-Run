# Shadow Run · Kit VFX

O kit inclui 12 texturas PNG RGBA, seis malhas FBX com UV, um script opcional para gerar 14 materiais no Unreal Editor e 13 receitas de Niagara. As texturas estáticas têm 512 × 512 px. Os flipbooks têm 1024 × 1024 px e grade 4 × 4. O quadro inicial está no canto superior esquerdo e a sequência avança por linhas. A última célula é transparente.

## Importação na Unreal

1. Extraia o ZIP. Importe os arquivos de Malhas como **Static Meshes**. Use escala de importação 1 como ponto de partida. O quad, o anel, a esfera e a cúpula têm 1 m de largura na origem. Confirme o tamanho na Unreal antes de montar os emissores.
2. Ative **Python Editor Script Plugin** e reinicie o Editor se solicitado. Execute o arquivo `Unreal/Criar_Materiais_VFX.py` por **File → Execute Python Script**, se o comando estiver disponível na versão instalada. O script importa as texturas e cria os materiais em `/Game/ShadowRun/VFX`. Os materiais que já têm o mesmo caminho são preservados.
3. Em caso de incompatibilidade do script com sua versão, monte os quatro tipos de material pelo guia em `Guias/Materiais.md`. Para os flipbooks, preserve o alfa e use UV clamp na borda externa. Desative o streaming para os flipbooks pequenos se aparecerem quadros borrados durante o efeito.
4. Monte os sistemas no Niagara seguindo `Guias/Receitas_Niagara.md`. Os números de duração, partículas e tamanho são pontos de partida. As cores são definidas no material, em uma Material Instance ou pela cor da partícula conforme a receita.

As texturas e os FBX são arquivos de origem. Os materiais passam a existir como assets após executar o script no projeto. Os sistemas Niagara são montados no editor com as receitas fornecidas.

## Malhas

| Malha | Uso | Tamanho de origem |
|---|---|---|
| SM_VFX_Quad | Flash, pulso no terminal e impacto plano | 1 × 1 m, plano XY |
| SM_VFX_Anel | Onda rente ao piso e pulso circular no terminal | Diâmetro de 1 m, plano XY |
| SM_VFX_Escudo_Esfera | Bolha de escudo em volta do jogador ou drone | Diâmetro de 1 m |
| SM_VFX_Onda_Cupula | Onda de energia em expansão | 1 m de diâmetro e 0,5 m de altura |
| SM_VFX_Tracante_Cruzado | Traçante visto de vários ângulos, com comprimento em X | 1 m de comprimento |
| SM_VFX_Fragmento_Metal | Destroços do drone ou impacto pesado | Cerca de 14 cm de comprimento |

## Paleta

- Energia, terminais e escudo em ciano `#22E6F2`.
- Flash de tiro e faíscas em âmbar `#FFB54D`.
- Alerta do Sentinela e do Caçador em vermelho `#FF4D4F`.
- Carga do Bastião em âmbar. Pane do subchefe com faíscas brancas e núcleo enfraquecido.
