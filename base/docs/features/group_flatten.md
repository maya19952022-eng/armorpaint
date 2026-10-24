# Flatten de grupo

IDs da matriz: LAY-14 · Fase: P1

Achata um grupo numa camada só **sem destruir o grupo**: a camada achatada fica logo acima do grupo, e o grupo continua no stack, oculto e editável. É útil para exportar ou pintar por cima do resultado e ainda poder voltar ao grupo original.

## Onde fica

Aba **Layers** → clique direito num grupo:

| Item | O que faz |
|---|---|
| **Flatten** | Cria `<nome do grupo> Flattened` acima do grupo com o mesmo resultado do **Merge Group** e oculta o grupo. Desabilitado em grupo vazio |
| **Export** (já existia) | Exporta as texturas do grupo composto (modo "selecionado") sem criar camada |
| **Export as .arm** | Salva o grupo, suas camadas e máscaras num `.arm` que contém só eles (mais malha e materiais do projeto) |
| **Merge Group** (já existia) | Junta o grupo numa camada e apaga o grupo (destrutivo) |

A camada achatada é uma camada de pintura comum: dá para exportá-la sozinha pelo **Export** dela.

## Atalhos

Sem atalho padrão: `ctrl+M` já abre o menu de modo de viewport no ArmorPaint. O preset de atalhos "Substance" (fase P10) vai mapear o flatten.

## Undo

Ctrl+Z remove a camada achatada e devolve a visibilidade que o grupo tinha; Ctrl+Y achata de novo.

## Projeto e export

A camada achatada e o grupo oculto são salvos normalmente no projeto. O `.arm` do grupo abre como projeto com só aquelas camadas; importar esse arquivo dentro de outro projeto (smart material) é da fase P3 (LAY-13).

## Script

```c
slot_layer_t *flat = script_layer_flatten_group(group); // NULL se não for grupo ou estiver vazio
script_layer_export_arm(group, "pasta/grupo.arm");
```

## Limitações

- O resultado é igual ao do Merge Group. Quando o grupo tem opacidade abaixo de 100% ou a camada de baixo do grupo é parcialmente transparente, o Merge Group (e portanto o flatten) não reproduz exatamente a aparência do grupo, porque a opacidade do grupo só é aplicada às camadas de cima durante o merge.
- A camada achatada não acompanha mudanças posteriores no grupo; achate de novo para atualizar.
