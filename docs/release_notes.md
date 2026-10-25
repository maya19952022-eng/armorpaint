# ArmorPaint — paridade com o Substance 3D Painter, fases P0, P1 e P2

Pré-lançamento com o que foi feito do plano `PLANO_PARIDADE_SUBSTANCE_PAINTER_12.md` até agora. Os PRs de cada fase ([P0](https://github.com/maya19952022-eng/armorpaint/pull/1), [P1](https://github.com/maya19952022-eng/armorpaint/pull/2), [P2](https://github.com/maya19952022-eng/armorpaint/pull/3)) ainda não foram mergeados; este build é do branch da P2, que contém as três.

## Como usar

Baixe `ArmorPaint-windows.zip`, descompacte numa pasta e abra `ArmorPaint.exe`. É portátil: não tem instalador, e a pasta `data` precisa ficar ao lado do exe. Requer Windows 10/11 com Direct3D 12.

## Placar da matriz de paridade

| | ✅ | 🟡 | ❌ |
|---|---|---|---|
| Início (P0) | 20 | 61 | 48 |
| Agora | **46** | 49 | 34 |

Layer stack: 16 de 18 ✅ (LAY-12/13 são da P3). Baking: 12 de 12 ✅. Meta final: 117 de 129 itens ✅.

## P0 — infraestrutura

- Testes automáticos sem GPU (Linux com Vulkan por software) que abrem, pintam, salvam e exportam projetos; rodam no CI a cada push.
- Matriz de paridade (`docs/parity_matrix.md`) com placar recalculado por script.
- Correção: a opacidade de grupos não voltava ao abrir um projeto.

## P1 — pilha de camadas

- **Blend e opacidade por canal** (base, normal, oclusão, roughness, metallic, altura).
- **Grupos pass-through**, **multi-seleção** de camadas (ctrl/shift-clique: mover, apagar, agrupar, achatar), **trava de camada**.
- **Pilha de efeitos** em camadas e máscaras: Fill, **Paint** (traços pintáveis dentro da pilha, por cima do que está abaixo), **Generator** (Edges, 3D Gradient, Facing Up, Noise), Levels, Hue/Saturation/Value, Color Balance, Sharpen, Blur, Compare Mask, Color Selection, Anchor Point.
- Nós novos: Levels, Color Balance, Sharpen, Compare Mask, Color Selection.
- **Máscara geométrica** por objeto, tile UDIM e **faces**: a ferramenta Select seleciona no viewport triângulos, polígonos, partes conectadas, ilhas UV ou objetos (ctrl remove), com destaque e undo; Include/Exclude.
- **Achatar grupo sem destruir** (o grupo continua editável), export do grupo achatado e Export as .arm.
- **Copiar e colar** camadas, grupos, máscaras e efeitos; colar como máscara.
- Fill Mask começa branca; alt+clique isola a máscara, shift+clique liga/desliga.

## P2 — mesh maps e baking

- **Nove mesh maps** por projeto (Normal, World Space Normal, ID, Ambient Occlusion, Curvature, Position, Thickness, Bent Normals, Opacity), assados na CPU sem travar a interface, com undo; o nó **Mesh Map** os usa em qualquer material.
- **Aba Baking** (File → Bake Mesh Maps...): ajustes comuns e por mapa, Bake Selected / Bake All, progresso e cancelar.
- **High poly → low poly**: lista de high polys ou casamento pelo nome (`_low`/`_high`), cage fixa ou **automática**, distâncias, normais médias, ignorar verso, antialiasing 2x2/4x4/8x8.
- **Skew map** e **cage map** pintados como camadas especiais, com Base Normal e Edge Protection.
- **Mapas desatualizados**, Rebake Outdated, **Auto Rebake** com prévia rápida e **rebake incremental** só dos objetos que mudaram.
- Curvatura pela geometria; ID por ilha UV; tipos Opacity e UV Island no nó Bake Texture.
- Modos de viewport para cada mesh map e para a direção dos raios.
- Preset de export **mesh_maps** (mapas em tons de cinza) e canais `mesh_*` em qualquer preset.

## Limitações conhecidas

- Texture sets separados ainda não existem: um conjunto de mesh maps por projeto (PRJ-05).
- O bake roda na CPU: oclusão e espessura em 4K com muitos raios levam minutos.
- Os geradores Dirt, Metal Edge Wear e Thickness ainda não existem (o Generator tem Edges, 3D Gradient, Facing Up e Noise).
- O efeito Paint, o Generator e a seleção de faces ainda não passaram por uma rodada de revisão independente.
- Os PRs ainda precisam de teste manual no Windows; as listas estão na descrição de cada PR.

## Formato de arquivo

Projetos salvos nesta versão usam o `.arm` versão 23. Projetos de versões anteriores abrem normalmente, inclusive os salvos pelos builds de teste da P2; versões antigas do ArmorPaint não abrem os projetos novos.
