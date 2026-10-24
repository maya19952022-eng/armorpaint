# Features do plano de paridade

Uma página curta por feature nova do plano de paridade com o Substance 3D Painter 12.2 (regra 10 da seção 0 de `PLANO_PARIDADE_SUBSTANCE_PAINTER_12.md`). O status de cada item fica em `docs/parity_matrix.md`; aqui fica como usar.

O manual público mora em outro repositório (`armorpaint_web`) e não é editado aqui.

## Convenções

- Arquivo: `base/docs/features/<nome_em_snake_case>.md`, por exemplo `per_channel_blending.md`. Uma página pode cobrir vários IDs da matriz quando eles são a mesma feature vista pelo usuário.
- Escreva para quem usa o ArmorPaint: o que a feature faz, onde fica na interface, os controles, os atalhos e o que salva no projeto. Detalhes de implementação vão no PR ou em `base/docs/architecture_notes.md`.
- Nomes de botões e menus exatamente como aparecem na interface em inglês (são as chaves de `tr()`), com a tradução em português entre parênteses quando ajudar.
- Funcionalidade atrás de `g_config->experimental` diz isso logo no começo.
- Ao criar uma página, acrescente uma linha no índice abaixo.

## Modelo

```markdown
# <Nome da feature>

IDs da matriz: LAY-01, ... · Fase: P1 · Desde: <PR>

<Uma ou duas frases: o que faz e quando usar.>

## Onde fica

<Aba, menu ou painel. Ex.: aba Layers → menu de contexto da camada → Blending.>

## Controles

| Controle | O que faz |
|---|---|
| ... | ... |

## Atalhos

| Atalho | Ação |
|---|---|
| ... | ... |

## Projeto e export

<O que é salvo no `.arm`, como projetos antigos abrem, efeito no export de texturas.>

## Script

<Funções da API de scripts (`--api`) que controlam a feature, com um exemplo curto.>

## Limitações

<O que ainda não faz, diferenças conhecidas para o Substance Painter.>
```

## Índice

| Página | IDs | Fase |
|---|---|---|
| [per_channel_blending.md](per_channel_blending.md) | LAY-01 | P1 |
| [group_flatten.md](group_flatten.md) | LAY-14 | P1 |
| [layer_multi_selection.md](layer_multi_selection.md) | LAY-03 | P1 |
| [layer_lock.md](layer_lock.md) | LAY-04 | P1 |
| [layer_clipboard.md](layer_clipboard.md) | LAY-15 | P1 |
| [mask_tools.md](mask_tools.md) | LAY-16, LAY-17 | P1 |
| [effect_nodes.md](effect_nodes.md) | LAY-06, LAY-07, LAY-08, LAY-10 | P1 |
| [effect_stack.md](effect_stack.md) | LAY-05, LAY-07, LAY-09 | P1 |
