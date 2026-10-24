# Nós de efeito: Levels, Color Balance, Sharpen, Compare Mask e Color Selection

IDs da matriz: LAY-06, LAY-07, LAY-08, LAY-10 · Fase: P1 · Desde: PR da P1

Cinco nós novos no editor de nós de material para ajustar cor e gerar máscaras dentro de uma camada de preenchimento, de uma máscara de preenchimento ou de um filtro. Use **Levels**, **Color Balance** e **Sharpen** para corrigir a cor de um material ou de uma camada, **Compare Mask** para gerar uma máscara a partir de dois valores e **Color Selection** para selecionar regiões de um mapa de ID por cor.

## Onde fica

- Editor de nós de material → menu de adicionar nós (ou busca com `space`) → categoria **Color** (Cor): `Levels`, `Color Balance`, `Sharpen`, `Color Selection`.
- Categoria **Utilities** (Utilitários): `Compare Mask`.
- Para aplicar sobre uma camada já pintada: aba Layers → **New** → **Filter**. O material do filtro lê a camada pai com o nó **Layer**; ligue `Layer → Levels → Material Output` (Base Color). Para máscaras: aba Layers → **New** → **Fill Mask** e ligue a saída `Mask` na Base Color do material da máscara.

## Controles

### Levels (Níveis)

`v = clamp((cor - In Black) / (In White - In Black), 0, 1)`, depois `v = v^(1 / Gamma)` e `saída = Out Black + (Out White - Out Black) · v`.

| Controle | O que faz |
|---|---|
| Color | Cor de entrada. |
| In Black / In White (Preto/Branco de Entrada) | Faixa de entrada que vira 0..1. `In White` 0.5 dobra os tons escuros. |
| Gamma (Gama) | Curva dos meios-tons; acima de 1 clareia, abaixo de 1 escurece. |
| Out Black / Out White (Preto/Branco de Saída) | Faixa de saída. |
| Channel (Canal) | `RGB` ajusta os três canais; `R`, `G` ou `B` ajusta só aquele canal e mantém os outros. |

### Color Balance (Equilíbrio de Cor)

Cada faixa tonal desloca a cor por `(cor da faixa - 0.5)`, pesada pela luminância Rec. 709 `l` da entrada: sombras `(1 - l)²`, realces `l²`, meios-tons o resto (`1 - sombras - realces`). Cinza 0.5 nas três faixas não muda nada.

| Controle | O que faz |
|---|---|
| Color | Cor de entrada. |
| Shadows / Midtones / Highlights (Sombras/Meios-tons/Realces) | Cor de deslocamento de cada faixa. Cinza neutro (0.5) não altera; 1.0 num canal soma até 0.5 naquele canal. |
| Preserve Luminosity (Preservar Luminosidade) | Ligado por padrão. Reescala o resultado para manter a luminância da entrada. |

### Sharpen (Nitidez)

Máscara de nitidez (unsharp mask): `saída = cor + (cor - borrada) · Amount`, limitada a 0..1. Como o nó Blur, o Sharpen renderiza a entrada numa textura no espaço UV (na resolução de textura do projeto) e borra essa textura com um kernel gaussiano. Uma cor uniforme passa sem mudança.

| Controle | O que faz |
|---|---|
| Color | Cor de entrada. Deve depender só das UVs (textura de imagem, nó Layer, texturas procedurais com `Texture Coordinate → UV`). |
| Amount (Quantidade) | Força da nitidez. 0 desliga. |
| Radius (Raio) | Raio do borrão em texels (sigma = raio / 2), até 16. |

### Compare Mask (Máscara de Comparação)

Saída `Mask` de 0 a 1. A transição começa no limiar e chega a 1 depois de `Smoothness`.

| Controle | O que faz |
|---|---|
| A, B | Valores comparados (uma cor ligada usa o canal R). |
| Threshold (Limiar) | Margem da comparação. `Equal` é `|A - B| <= Threshold`; `Greater` é `A - B > Threshold`. |
| Smoothness (Suavidade) | Largura da borda suave; 0 dá uma máscara binária. |
| Operation (Operação) | `Greater` (Maior), `Less` (Menor), `Equal` (Igual), `Not Equal` (Diferente). |

### Color Selection (Seleção de Cor)

Versão com várias cores do nó **Color Mask**, pensada para mapas de ID. A máscara é o máximo das máscaras de cada cor usada.

| Controle | O que faz |
|---|---|
| Color | Cor testada, normalmente a textura do mapa de ID. |
| Color 1..4 | Cores selecionadas. |
| Count (Contagem) | Quantas das cores acima entram na seleção (1 a 4, a partir da Color 1). |
| Tolerance (Tolerância) | Distância RGB até a qual a cor conta como selecionada. |
| Smoothness (Suavidade) | Largura da borda suave depois da tolerância. |
| Invert (Inverter) | Inverte a máscara. |

## Atalhos

| Atalho | Ação |
|---|---|
| `space` (no editor de nós) | Busca de nós; digite o nome do nó. |

## Projeto e export

Os nós ficam no grafo do material, salvo no `.arm` como os outros nós. O formato do projeto não mudou e projetos antigos abrem igual. Numa versão do ArmorPaint sem esses nós, o nó aparece no grafo mas não gera código, e a saída vale 0. O export de texturas grava o resultado já calculado da camada. O Sharpen guarda a textura intermediária só em memória (prévia de nó), refeita quando o material é recompilado.

## Script

Os tipos para `script_material_create_node_at` são `LEVELS`, `COLOR_BALANCE`, `SHARPEN`, `COMPARE_MASK` e `COLOR_SELECTION`; a lista de entradas e botões aparece em `ArmorPaint --background --api`. Exemplo (dobra um cinza com Levels):

```c
slot_material_t *m   = script_material_create("levels");
ui_node_t       *rgb = script_material_create_node_at("RGB", -600.0, 0.0);
ui_node_t       *lv  = script_material_create_node_at("LEVELS", -300.0, 0.0);
script_material_set_color(rgb, 0, 0, 0.25, 0.25, 0.25, 1.0);
script_material_connect(rgb, 0, lv, 0);
script_material_set_float(lv, 1, 2, 0.5); // In White
script_material_set_button(lv, 0, 0);     // Channel: 0 RGB, 1 R, 2 G, 3 B
script_material_connect(lv, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
script_material_update(); // Recompila e refaz a textura do Sharpen
script_layer_new_fill();
```

O teste `base/tests/paint_headless/p1_nodes.c` (`base/tests/paint_headless/run.sh p1_nodes.c`) cobre os cinco nós.

## Limitações

- Ainda não há o menu de "efeitos rápidos" da aba Layers (LAY-07): os nós são adicionados à mão no material de um filtro, de uma camada de preenchimento ou de uma máscara de preenchimento.
- Sharpen tem as mesmas restrições do Blur: a entrada é avaliada no espaço UV, então entradas que dependem de posição ou normal (coordenadas `Generated`/`Object`, modo triplanar) não batem com a textura borrada. O raio é medido em texels da resolução de textura do projeto, e perto de costuras de UV o borrão mistura texels de fora da ilha (como o Blur sem `Seamless`).
- Compare Mask compara dois valores ligados à mão (por exemplo dois nós **Layer**); não existe ainda a fonte automática "camada abaixo" ou âncora do Substance Painter.
- Color Selection compara distância em RGB, como o Color Mask, e aceita no máximo 4 cores; não há conta-gotas dedicado para o mapa de ID.
- Color Balance usa um deslocamento simples por faixa (até ±0.5 por canal), não a curva exata de outros programas.
