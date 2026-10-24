# Atalhos de máscara e fill mask branca

IDs da matriz: LAY-16, LAY-17 · Fase: P1

Atalhos para ver uma máscara sozinha e para desligar uma máscara por um momento, e máscaras de preenchimento que começam brancas.

## Onde fica

- Miniatura da máscara na aba Layers (atalhos com Alt e Shift).
- Botão **New** da aba Layers → **Fill Mask**.

## Controles

| Controle | O que faz |
|---|---|
| Fill Mask | Cria uma máscara de preenchimento com um material próprio, chamado "<camada> Mask", de cor branca: a máscara nova deixa a camada toda visível. Para dar forma à máscara, edite esse material (por exemplo, ligue um nó de ruído na cor) |
| To Fill Mask (menu da máscara) | Igual a antes: transforma a máscara em preenchimento com o material atual |

Antes, Fill Mask usava o material selecionado, quase sempre cinza, e a camada ficava parcialmente escondida.

## Atalhos

| Atalho | Ação |
|---|---|
| Alt+clique na miniatura da máscara | Mostra só a máscara no viewport (modo de visualização Mask). Repetir na mesma máscara volta ao modo anterior |
| Shift+clique na miniatura da máscara | Desliga ou religa a máscara (o mesmo que o olho), passo de undo |

## Projeto e export

O material da fill mask é um material comum do projeto e é salvo com ele. Nada muda no formato `.arm`.

## Script

```c
slot_layer_t *m = script_layer_new_mask(l, 3); // 3: fill mask branca com material próprio
// 0 preta, 1 branca, 2 preenchida com o material atual (comportamento antigo)
```

## Limitações

- Cada Fill Mask cria um material novo na lista de materiais; apagar a máscara não apaga o material.
- O isolamento usa o modo de visualização Mask global do viewport, não um modo por máscara.
