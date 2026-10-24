# Blend e opacidade por canal

IDs da matriz: LAY-01 · Fase: P1

Cada camada de pintura tem um modo de blend e uma opacidade próprios para cada canal: cor base, normal, oclusão, roughness, metallic e altura. Assim uma camada pode escurecer só a oclusão com Multiply, ou mexer na roughness sem tocar na cor.

## Onde fica

- **Menu de contexto da camada** (clique direito na camada) → seção **Channels**. Cada linha tem: caixa para ligar/desligar o canal, modo de blend e opacidade (0 a 1).
- **Linha da camada na aba Layers**: o combo ao lado do nome edita o blend do canal escolhido em **Edit → Blending Channel** (padrão: Base Color).

## Controles

| Controle | O que faz |
|---|---|
| Caixa do canal | Liga ou desliga a contribuição da camada nesse canal (antes já existia) |
| Blend (cor base, oclusão, roughness, metallic, altura) | Os 18 modos: Mix, Darken, Multiply, Burn, Lighten, Screen, Dodge, Add, Overlay, Soft Light, Linear Light, Difference, Subtract, Divide, Hue, Saturation, Color, Value. Em canais de um valor só, Hue/Saturation/Color não mudam nada e Value equivale a Mix |
| Blend do canal Normal | **Mix** (interpola) ou **Combine** (soma de detalhes, whiteout). Substitui a antiga caixa "Normal Blend" |
| Blend do canal Height | Qualquer modo; **Add** é o padrão e substitui a antiga caixa "Height Blend" |
| Opacidade do canal | Multiplica a opacidade da camada (e do grupo) só nesse canal |
| Opacity (slider de cima) | Opacidade da camada inteira, igual a antes |

Padrões de uma camada nova: todos os canais em Mix com opacidade 1, Normal em Combine, Height em Add (o mesmo comportamento de antes desta mudança).

## Atalhos

Nenhum atalho novo. Ctrl+Z/Ctrl+Y desfazem e refazem mudanças de blend e de opacidade por canal.

## Projeto e export

- Projetos salvos a partir desta versão usam o formato `.arm` versão 22, com os campos `blending_ch` e `opacity_ch` por camada.
- Projetos antigos (versão 21 ou anterior) abrem igual: o blend da camada vai para a cor base, "Normal Blend" vira Combine e "Height Blend" vira Add.
- O export de texturas, o merge de camadas, o flatten da visão 2D e o viewport usam as mesmas regras por canal.
- Correção junto: com a altura ligada e algum de oclusão/roughness/metallic desligado, o export ignorava a altura da camada; agora ela entra.

## Script

```c
slot_layer_t *l = script_layer_new_color(0xffff0000, 0.2, 1.0, 1.0);
script_layer_set_channel(l, LAYER_CH_OCC, BLEND_TYPE_MULTIPLY, 1.0); // canal, modo, opacidade
script_layer_set_channel(l, LAYER_CH_BASE, -1, 0.0);                 // -1 mantém o modo
int   mode    = script_layer_get_channel_blending(l, LAYER_CH_OCC);
float opacity = script_layer_get_channel_opacity(l, LAYER_CH_BASE);
script_undo();
script_redo();
```

`l->blending` continua lendo e escrevendo o blend da cor base. Os enums `layer_channel_t` (`LAYER_CH_BASE` … `LAYER_CH_HEIGHT`) e `blend_type_t` (`BLEND_TYPE_MIX` … `BLEND_TYPE_NORMAL_COMBINE`) estão disponíveis nos scripts.

## Limitações

- Opacidade (alfa), emissão e subsurface só ligam/desligam; não têm blend próprio (emissão e subsurface são flags no canal alfa da normal, veja `base/docs/architecture_notes.md`).
- O canal Normal oferece Mix e Combine; os modos de detalhe do Substance Painter (Normal Map Detail/Inverse Detail) ainda não existem.
- Filtros usam o blend e a opacidade por canal da camada pai.
