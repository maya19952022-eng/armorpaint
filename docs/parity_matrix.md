# Matriz de paridade: ArmorPaint × Substance 3D Painter 12.2

Gerada na P0 a partir da seção 4 de [`PLANO_PARIDADE_SUBSTANCE_PAINTER_12.md`](../PLANO_PARIDADE_SUBSTANCE_PAINTER_12.md), com os mesmos IDs e status. Atualize a cada PR (regra 9 da seção 0 do plano).

Legenda: `✅ feito` · `🟡 parcial` · `❌ falta` · `⛔ fora de escopo`. Itens ⛔ não contam na meta.

Como atualizar:

1. Mude a coluna **Status** do item e escreva em **Notas** o PR e o que ficou de fora (quando 🟡).
2. Rode `python3 scripts/parity_matrix.py` para recalcular o placar abaixo (`--check` só confere, útil antes do commit).
3. Acrescente uma linha em **Histórico** com a fase, o PR e a variação do placar.

## Placar

<!-- placar:inicio -->

| Área | Itens em escopo | ✅ | 🟡 | ❌ | % ✅ |
|---|---|---|---|---|---|
| Projeto, texture sets e assets | 18 | 0 | 9 | 9 | 0% |
| Layer stack | 18 | 1 | 8 | 9 | 6% |
| Ferramentas e pincel | 28 | 8 | 14 | 6 | 29% |
| Projeções de fill | 7 | 2 | 1 | 4 | 29% |
| Procedural e biblioteca | 10 | 1 | 5 | 4 | 10% |
| Baking | 12 | 2 | 3 | 7 | 17% |
| Viewport, render e shading | 12 | 2 | 5 | 5 | 17% |
| Import e export | 13 | 1 | 10 | 2 | 8% |
| Interface, UX e scripting | 11 | 3 | 6 | 2 | 27% |
| **Total** | **129** | **20 (16%)** | **61** | **48** | meta: 117 ✅ |

Fora de escopo nas tabelas: 2. Faltam 97 itens ✅ para a meta.

<!-- placar:fim -->

Meta final: ≥ 117 itens ✅ (90% de 129).

## Histórico

| Fase | PR | ✅ | 🟡 | ❌ | O que mudou |
|---|---|---|---|---|---|
| P0 | (este PR) | 20 | 61 | 48 | Matriz criada a partir do plano; nenhum item muda de status na P0 (infraestrutura). |

## 4.1 Projeto, texture sets e assets

| ID | Feature SP | Status | Situação no início (P0) | Tarefa | Fase | Tam. | Notas |
|---|---|---|---|---|---|---|---|
| PRJ-01 | Diálogo New Project completo (mesh, template, resolução, normal format, UV tiles, auto-unwrap, câmeras) (12.0) | 🟡 parcial | só template | `box_new_project.c` novo fluxo em etapas; chamar `import_mesh` com opções; templates em `paint/assets/project_templates/*.json` | P5 | M |  |
| PRJ-02 | Project Configuration (trocar resolução, template, reimport mesh com checkbox) (12.0) | 🟡 parcial | reimport existe sem diálogo | `box_project_config.c`; reusar `layers_on_resized` | P5 | M |  |
| PRJ-03 | Reimport mesh com reprojeção de layers (UV mudou) | ❌ falta | — | Bake de position+normal do mesh antigo, reprojeção world-space por layer (`util_layer.c`, shader `layer_reproject.shader`) | P8 | L |  |
| PRJ-04 | Formato de normal do projeto (OpenGL/DirectX) | 🟡 parcial | só no export preset e na importação de texturas | Campo `normal_map_format` em `project_t`; aplicar em bake, viewport e export | P5 | S |  |
| PRJ-05 | Texture Set list (por atlas/material/objeto; resolução, canais, mesh maps por set) | 🟡 parcial | Atlases no export; layers por objeto | `tab_texture_sets.c`; modelo `texture_set_t` em `project_t` unificando atlas/objeto/material; seleção ativa | P5 | L |  |
| PRJ-06 | Canais custom (user0–7) e canais OpenPBR (12.1) | ❌ falta | canais fixos em 3 texturas | Texturas extras `texpaint_user[]` por layer alocadas sob demanda; registro de canais por set; sockets dinâmicos no Material Output; `make_paint`/`make_material`/`pipes`/export generalizados | P5 | XL |  |
| PRJ-07 | Adicionar vários canais de uma vez (12.1) | ❌ falta | — | Janela de seleção múltipla em `tab_texture_sets.c` | P5 | S |  |
| PRJ-08 | Instancing de layers entre texture sets + flatten de instâncias (12.1) | 🟡 parcial | "Shared" por objeto | Flag `instance_of` em `slot_layer_t`; propagação na composição; flatten em todos os sets | P5 | M |  |
| PRJ-09 | Nomes custom de UV tiles usados no export (11.0) | ❌ falta | — | Mapa tile→nome em `project_t`; token `$udim`/`$udimName` | P8 | S |  |
| PRJ-10 | Aviso ao pintar em outro texture set (12.1) | ❌ falta | — | Em `render_path_paint.c`: checar set do hit vs ativo; overlay de aviso | P5 | S |  |
| PRJ-11 | Highlight do set/UV tile sob o cursor (12.2) | ❌ falta | — | Reusar `render_path_base_draw_outline` com máscara por set | P5 | S |  |
| PRJ-12 | Assets panel unificado (tipos, tags, busca, escopos, thumbnails) | 🟡 parcial | abas separadas + browser + busca | `tab_assets.c` agregando Materials/Brushes/Textures/Fonts + novos tipos (smart materials, smart masks, filters, generators, alphas, envmaps, tool presets, post-effects presets); tags em JSON sidecar | P9 | L |  |
| PRJ-13 | Auto-update de recursos modificados em disco (11.0) | 🟡 parcial | Reimport Textures manual | Watcher por mtime (`iron_file.c`) com opção em preferências; aplica a texturas, fontes, materiais `.arm` | P9 | M |  |
| PRJ-14 | Auto-save, backups e recuperação | 🟡 parcial | plugin autosave | Mover para o core (`project.c`): intervalo, N backups rotativos, recuperação ao abrir | P9 | S |  |
| PRJ-15 | Copy/paste de layers entre projetos | ❌ falta | — | Clipboard interno serializando subárvore em `.arm` temporário (`export_arm.c` já serializa layers) | P9 | M |  |
| PRJ-16 | Templates OpenPBR, Metal/Rough, Spec/Gloss, Unity, Unreal | 🟡 parcial | Workflow PBR/base/sculpt | Templates definem canais, shader, export preset padrão | P5 | M |  |
| PRJ-17 | Import USD/USDZ/.geo.usd (12.0) | ❌ falta | — | Opcional: leitor USDA/USDC mínimo (mesh + UV + materiais) como plugin `paint/plugins/io_usd/` | P8 | L |  |
| PRJ-18 | Import de câmeras do mesh | ❌ falta | — | fbx/gltf já expõem câmeras via ufbx/cgltf; criar `camera_object` por câmera | P8 | S |  |
| PRJ-19 | 3D Assets Adobe | ⛔ fora de escopo | — | — | — | — |  |

## 4.2 Layer stack

| ID | Feature SP | Status | Situação no início (P0) | Tarefa | Fase | Tam. | Notas |
|---|---|---|---|---|---|---|---|
| LAY-01 | Blend mode e opacidade **por canal** | ❌ falta | um blend e uma opacidade por layer | Arrays `blending[ch]`/`opacity[ch]` em `slot_layer_t`; `layers_merge_layer` e `make_material.c` por canal (já há `pipes_merge_r/g/b`); UI em `tab_layers.c` com seletor de canal | P1 | M |  |
| LAY-02 | Pass-through em grupos | ❌ falta | — | Flag `passthrough` no grupo; compositor ignora o grupo como unidade | P1 | S |  |
| LAY-03 | Multi-seleção de layers (mover, apagar, agrupar, flatten) | ❌ falta | — | `g_context->layers_selected` (lista); ações aplicadas em lote; ctrl/shift-clique | P1 | M |  |
| LAY-04 | Lock de layer | ❌ falta | — | Flag `locked`; `render_path_paint` recusa pintura; ícone | P1 | S |  |
| LAY-05 | Effects stack em conteúdo **e** máscara (Fill, Paint, Generator, Filter, Levels, Compare Mask, Anchor, Color Selection) | 🟡 parcial | filtros só no conteúdo; fill mask; máscara pintável | Generalizar filtro para máscaras (`layers_create_filter` com alvo mask); "Paint effect" = sublayer pintável dentro do stack; UI do stack expandível por layer | P1 | L |  |
| LAY-06 | Levels | ❌ falta | nó não existe | Nó `levels_node.c` (in/out black/white, gamma, por canal) + efeito rápido | P1 | S |  |
| LAY-07 | HSL / Color Balance / Sharpen como efeitos rápidos | 🟡 parcial | HSV existe; sharpen e color balance não | Nós `color_balance_node.c`, `sharpen_node.c`; menu "Add effect" em `tab_layers.c` cria filtro com o nó pronto | P1 | S |  |
| LAY-08 | Compare Mask | ❌ falta | — | Nó `compare_mask_node.c` (fonte: camada abaixo/anchor, operador, threshold) | P1 | S |  |
| LAY-09 | Anchor points | 🟡 parcial | nós Layer/Layer Mask referenciam layers | Comando "Add Anchor Point" que nomeia e lista no menu de nós; nó `anchor_node.c` apontando por id | P1 | S |  |
| LAY-10 | Color Selection (máscara por color ID) como efeito | 🟡 parcial | Color ID tool → To Mask (destrutivo) | Nó `color_selection_node.c` (lista de cores com tolerância) usável em fill mask | P1 | S |  |
| LAY-11 | Geometry mask (UV tile/objeto/polígonos, include/exclude) | 🟡 parcial | object mask; fill por face | Máscara geométrica por layer: lista de tiles/objetos/ids de polígono, include/exclude; aplicada no `make_paint` e `make_material` | P1 | M |  |
| LAY-12 | Smart masks (criar, salvar, aplicar, merge/overwrite com ctrl) | ❌ falta | — | Serializar subárvore de máscara+efeitos para `.arm` (`export_arm.c`); categoria no Assets; drop em layer | P3 | M |  |
| LAY-13 | Smart materials (criar de grupo, salvar, aplicar) | ❌ falta | — | Serializar grupo (layers, máscaras, efeitos, fill materials, blends) para `.arm`; instanciar em outro projeto/set | P3 | M |  |
| LAY-14 | Flatten não destrutivo de grupo + export do resultado + salvar original como smart material (12.0) | 🟡 parcial | Merge Group destrutivo | `layers_flatten_copy(group)` cria layer "flattened" mantendo o grupo oculto; "Export flattened" chama `export_texture` com apenas esse layer; atalho ctrl+M | P1 | M |  |
| LAY-15 | Copy/paste de layer, máscara e efeitos | 🟡 parcial | duplicate | Clipboard interno (ver PRJ-15) + "Paste as mask" | P1 | S |  |
| LAY-16 | Fill effects em máscara começam brancos (12.1) | ❌ falta | — | Default no `layers_new_mask` para fill mask | P1 | S |  |
| LAY-17 | Thumbnails de layer/máscara, isolar máscara (alt+clique), desabilitar temporariamente (shift+clique) | 🟡 parcial | preview existe | Atalhos em `tab_layers.c`; modo viewport "mask solo" (já há `VIEWPORT_MODE_MASK`) | P1 | S |  |
| LAY-18 | Undo unificado bake+paint (12.1) | ✅ feito | — | — | — | — |  |

## 4.3 Ferramentas e pincel

| ID | Feature SP | Status | Situação no início (P0) | Tarefa | Fase | Tam. | Notas |
|---|---|---|---|---|---|---|---|
| TOOL-01 | Flow separado de opacity (acúmulo por dab) | ❌ falta | opacity único | `brush_flow` em `context_t`; acumular em `texpaint_live`/undo com max por stroke (`render_path_paint.c`, `make_brush.c`) | P4 | M |  |
| TOOL-02 | Spacing | 🟡 parcial | lazy step | `brush_spacing` (% do raio) na interpolação de `inplast` em `make_brush.c`; socket no nó Input | P4 | S |  |
| TOOL-03 | Jitter de size/flow/angle/position na UI | 🟡 parcial | via nó Random | Sliders no header que injetam no grafo do pincel (ou leem `brush_nodes_uses_random`) | P4 | S |  |
| TOOL-04 | Alignment (camera / tangent / UV / normal) | 🟡 parcial | direcional + camera align por tecla | Combo `brush_alignment` aplicado no cálculo da base do decal/stencil em `make_brush.c` | P4 | S |  |
| TOOL-05 | Backface, angle limit e distance limit no header | 🟡 parcial | nas preferências | Expor por ferramenta em `ui_header.c`; distância em `make_paint.c` (depth reject com limiar) | P4 | S |  |
| TOOL-06 | Simetria radial e offset de plano | 🟡 parcial | X/Y/Z no origem | `sym_radial_count`, `sym_offset[3]` em `context_t`; loop no shader de `make_brush.c`; gizmo de plano em `render_gizmo.c` | P4 | M |  |
| TOOL-07 | Simetria em fill layers e efeitos (11.1) | ❌ falta | — | Aplicar simetria no `make_paint` quando `fill_material` e no efeito fill | P4 | S |  |
| TOOL-08 | Tamanho físico (unidades) | ❌ falta | — | Calcular bounds do mesh; exibir raio em cm; opção "physical size" em pincel e em fill scale | P4 | S |  |
| TOOL-09 | Tool presets (ferramenta + parâmetros + material/alpha) | 🟡 parcial | Brushes = grafos | `tool_preset_t` serializado em `.arm`; aba/categoria no Assets; shortcuts | P4 | M |  |
| TOOL-10 | Polygon Fill: triângulo/polígono/UV island/objeto | 🟡 parcial | face=triângulo | Preservar ids de polígono (quads/ngons) no import (`import_obj.c`, `io_fbx.c`, gltf) em um vertex attribute; fill por polígono | P4 | M |  |
| TOOL-11 | Polygon Fill: loop, partial loop, patch, marquee, visible only, SHIFT+A, ALT preview (12.2) | ❌ falta | — | Adjacência de polígonos em `util_geom.c`; loop/patch por direção de aresta; marquee = select tool; preview no `render_path_paint` | P4 | L |  |
| TOOL-12 | Polygon Fill com simetria + preview de aresta (12.2) | ❌ falta | — | Espelhar ids via posição; desenhar aresta de simetria | P4 | S |  |
| TOOL-13 | Polygon Fill com blend/opacidade e todos os canais, resume previous stroke (12.2) | 🟡 parcial | — | Mesmo caminho do brush (`brush_blending`, `brush_opacity`) para fill | P4 | S |  |
| TOOL-14 | Blend aditivo para strokes e polygon fill (12.2) | ✅ feito | (`BLEND_TYPE_ADD`) | Garantir que fill respeita | P4 | S |  |
| TOOL-15 | Projection tool (stencil/projeção com manipulação) | 🟡 parcial | decal + stencil | Unificar como modo "Projection" do brush com grade de transform (ctrl) e tiling | P4 | S |  |
| TOOL-16 | Geometry Decal | 🟡 parcial | decal layer com `decal_mat` | Decal snap à normal com warp (ver FILL-05) | P6 | S |  |
| TOOL-17 | Smudge | ✅ feito | — | — | — | — |  |
| TOOL-18 | Clone | ✅ feito | — | — | — | — |  |
| TOOL-19 | Particles | ✅ feito | — | — | — | — |  |
| TOOL-20 | Material picker | ✅ feito | — | — | — | — |  |
| TOOL-21 | Path tools: paint/eraser/smudge ao longo de path, Filled Path (11.0) | 🟡 parcial | path/curve layers | `path_tool_mode` (paint, erase, smudge, fill) em `util_path_tool.c`; preenchimento de path fechado por scanline em UV | P4 | M |  |
| TOOL-22 | Ribbon (11.1): estica imagem ao longo do path, largura/opacidade por vértice, blend em sobreposição, fechado | ❌ falta | — | Geração de faixa UV (strip) ao longo da curva em `util_path_tool.c`; `texcoord` 0–1 ao longo do comprimento; atributos por ponto | P4 | L |  |
| TOOL-23 | Path improvements (11.0): snap a polígonos, trocar tipo, constraints de ângulo/linha, fechar com 1 clique, info, escalar/rotacionar vértices, copy/paste vértices, preview, backspace | 🟡 parcial | pontos + delete | Itens em `util_path_tool.c` + header; gizmo de vértices via `render_gizmo.c` | P4 | M |  |
| TOOL-24 | Lazy mouse, straight line | ✅ feito | — | — | — | — |  |
| TOOL-25 | Pressão por parâmetro | ✅ feito | — | — | — | — |  |
| TOOL-26 | Pintar no 2D view | ✅ feito | — | — | — | — |  |
| TOOL-27 | Properties agrupadas/colapsáveis (11.1) | 🟡 parcial | header linear | `tab_properties.c` (ver UI-02) | P9 | M |  |
| TOOL-28 | Color picker grayscale (12.1) | 🟡 parcial | — | Variante em `ui_color_wheel` | P4 | S |  |

## 4.4 Projeções de fill

| ID | Feature SP | Status | Situação no início (P0) | Tarefa | Fase | Tam. | Notas |
|---|---|---|---|---|---|---|---|
| FILL-01 | UV, triplanar | ✅ feito | — | — | — | — |  |
| FILL-02 | Planar (com gizmo) | 🟡 parcial | "Project" + decal_mat | Gizmo de plano com escala/rotação em `render_gizmo.c` | P6 | S |  |
| FILL-03 | Spherical, cylindrical | ❌ falta | — | `uv_type_t` + `make_texcoord.c` novas funções; gizmo | P6 | M |  |
| FILL-04 | Warp (grade deformável) | ❌ falta | — | Grade NxM de pontos em espaço de projeção; shader amostra via malha de quads (`make_texcoord.c`); edição de pontos no viewport | P6 | L |  |
| FILL-05 | Warp to Geometry (12.0): grade conforma à superfície, pontos novos grudam no mesh, mantém deformação | ❌ falta | — | Pontos da grade com raycast à superfície (`iron_raycast.c`); projeção por triângulo da grade em espaço tangente; opção toggle | P6 | L |  |
| FILL-06 | Reorientar normais ao projetar em warp (11.1) | ❌ falta | — | Rotação da normal pelo jacobiano da grade | P6 | S |  |
| FILL-07 | Tiling/offset/rotation da projeção | ✅ feito | (scale/angle) | Offset | P6 | S |  |

## 4.5 Procedural e biblioteca

| ID | Feature SP | Status | Situação no início (P0) | Tarefa | Fase | Tam. | Notas |
|---|---|---|---|---|---|---|---|
| LIB-01 | Filtros: blur ✅, blur slope, blur directional, sharpen, levels, HSL ✅, color balance, invert ✅, transform ✅, warp ✅, height→normal ✅ (bump), normal→height, baked lighting, contrast/luminosity, stylization, quantize ✅, anisotropic kuwahara, bevel smooth, directional distance, grayscale ✅, fill area mask, embroidery | 🟡 parcial | ~8 de 22 | Nós nativos novos: `slope_blur`, `directional_blur`, `sharpen`, `levels`, `color_balance`, `normal_to_height` (solver iterativo em compute, ou CPU com cache), `baked_lighting` (usa envmap + world normal), `kuwahara`, `bevel_smooth` (distance field), `directional_distance`, `stylization`; node groups `.arm` para fill area e embroidery | P3 | L |  |
| LIB-02 | Generators: mask builder, mask editor, metal edge wear, dirt, dripping rust, light, position ✅, curvature ✅, AO ✅, thickness ✅, world normal ✅, UV random color, 3D linear/distance, tile random, triangle grid, scratches | 🟡 parcial | 5 de 16 | Nó `Mesh Map` (lê slots da P2); node groups com Group Input para parâmetros; nativos: `uv_island_id` (novo bake), `scratches`, `tile_random`, `triangle_grid`, `dripping` (marcha em direção world-down usando position/normal) | P3 | L |  |
| LIB-03 | Biblioteca de noises/grunges (procedural, 2D Voronoi) | 🟡 parcial | nós base | ~40 node groups prontos (`paint/assets/materials/grunges/*.arm`) | P3 | M |  |
| LIB-04 | Alphas e hard surface stamps | 🟡 parcial | svg import | 60+ alphas CC0 (png/svg) em `paint/assets/alphas/`, categoria no Assets | P3 | S |  |
| LIB-05 | Smart masks prontos (~40) | ❌ falta | — | Autorar após LAY-12 | P3 | M |  |
| LIB-06 | Smart materials prontos (~60) | ❌ falta | — | Autorar após LAY-13 (metais, madeiras, plásticos, tecidos, pedras, pinturas, emissivos) | P3 | L |  |
| LIB-07 | Parâmetros expostos em node groups (equivalente a sbsar) | 🟡 parcial | Group Input | Painel de parâmetros do grupo no layer/efeito sem abrir o editor de nós | P3 | M |  |
| LIB-08 | Override de resolução de recurso (11.1) | ❌ falta | — | Campo por nó Image/Group | P3 | S |  |
| LIB-09 | Text resource (10.0) | ✅ feito | (text tool + Text Texture) | — | — | — |  |
| LIB-10 | Post-effects presets como assets (12.0) | ❌ falta | — | Serializar settings de pós-efeitos em JSON; categoria no Assets | P7 | S |  |

## 4.6 Baking

| ID | Feature SP | Status | Situação no início (P0) | Tarefa | Fase | Tam. | Notas |
|---|---|---|---|---|---|---|---|
| BAKE-01 | Mesh maps como slots por texture set (Normal, World Normal, ID, AO, Curvature, Position, Thickness, Bent Normals, Opacity) consumidos por generators | 🟡 parcial | bake por nó, salvo em layer/Textures | `mesh_map_t` por set em `project_t`; `util_bake.c` com job API headless; nó `Mesh Map` | P2 | L |  |
| BAKE-02 | Diálogo Baking mode: settings comuns separados dos por-mapa (12.1), seleção de mapas, bake all/selected | ❌ falta | — | `box_bake.c` (ou modo de workspace "Bake") | P2 | M |  |
| BAKE-03 | High-poly list, match by name (sufixos `_low/_high`), cage/offset global, distâncias frontal/rear, average normals, ignore backface, AA 2x2/4x4/8x8, dilation | 🟡 parcial | high-poly simples, dilate | Estender `render_path_paint.c` bake de high poly: raycast com cage e distâncias; supersample na resolução + downsample; parser de nomes | P2 | L |  |
| BAKE-04 | Auto cage (11.0) | ❌ falta | — | Cage = low-poly extrudido por normal com offset suavizado (`util_geom.c`) | P2 | M |  |
| BAKE-05 | Auto rebake incremental e quick rebake (12.1) | ❌ falta | — | Marcar região suja (bbox UV) ao mudar parâmetro; rebake só da região; botão "rebake visível" | P2 | M |  |
| BAKE-06 | Skew map painting (12.1): brush/eraser/polygon fill, preview shader, vetores de direção, edge protection, base normal mesh/per-triangle | ❌ falta | — | Skew map = textura RGBA por set; ferramenta de pintura reaproveita brush; bake de normal usa direção do raio = normal interpolada do skew; shader de preview | P2 | L |  |
| BAKE-07 | Cage painting (12.2): offset de cage por pixel sobre o global, visualização, select/fill, blend, HDR slider, reset | ❌ falta | — | Textura de offset (float) por set; bake lê offset por texel; visualização do cage em wireframe translúcido | P2 | M |  |
| BAKE-08 | Opacity bake, UV island ID bake | ❌ falta | — | `BAKE_TYPE_OPACITY`, `BAKE_TYPE_UV_ISLAND` em `make_bake.c` | P2 | S |  |
| BAKE-09 | Curvature por geometria (não screen-space) | 🟡 parcial | — | Curvatura por vértice via adjacência (`util_geom.c`) rasterizada; opção "Mesh" vs "Normal map" | P2 | M |  |
| BAKE-10 | Export preset "Mesh maps" grayscale (11.1) | ❌ falta | — | Preset JSON lendo slots de mesh map | P8 | S |  |
| BAKE-11 | Bakers GPU RT (AMD) | ✅ feito | (D3D12/Vulkan RT) | — | — | — |  |
| BAKE-12 | Histórico unificado | ✅ feito | — | — | — | — |  |

## 4.7 Viewport, render e shading

| ID | Feature SP | Status | Situação no início (P0) | Tarefa | Fase | Tam. | Notas |
|---|---|---|---|---|---|---|---|
| VIEW-01 | Pós-efeitos (12.0): DOF, bloom ✅, glare, lens flare, aberração lateral, vignette ✅, sharpen, film grain ✅ (noise grain), tone mapping selecionável, color correction | 🟡 parcial | 4 de 10 | Shaders kong em `paint/shaders/`: `dof_pass`, `glare_pass` (streaks), `lens_flare_pass`, `chromatic_pass`, `sharpen_pass`, `color_correction_pass` (exposure, contrast, saturation, lift/gamma/gain, temperature); tone mappers (Linear, Reinhard, Filmic, ACES, AgX) em `compositor_pass.shader`; ordem configurável; UI em `box_preferences.c`/menu Viewport reagrupado | P7 | L |  |
| VIEW-02 | Display settings reagrupados (12.2) + presets de pós-efeitos como assets | ❌ falta | — | Painel "Display Settings" próprio (`box_display.c`); JSON de preset | P7 | S |  |
| VIEW-03 | Exposição e color profile (sRGB/ACES/OCIO-lite) + LUT ✅ | 🟡 parcial | — | Exposure slider; working space linear → display transform selecionável | P7 | S |  |
| VIEW-04 | OpenPBR 1.1 shader (coat, fuzz, thin film, transmission, specular color/ior/anisotropy, subsurface, emission) (12.1) | 🟡 parcial | metal/rough + emission + subsurface + translucency | BRDF em `deferred_light.shader`/forward + path tracer `raytrace_brute_*`; sockets no Material Output por template; depende de PRJ-06 | P7 | L |  |
| VIEW-05 | Order Independent Transparency (12.2) | ❌ falta | — | Weighted-blended OIT no forward path (`render_path_forward.c`, shaders) | P7 | M |  |
| VIEW-06 | Sombras (luz direcional) | ❌ falta | só IBL | Luz sol opcional com shadow map (`render_path_deferred.c`) | P7 | M |  |
| VIEW-07 | Tessellation/displacement no viewport com unidade física (11.1) | 🟡 parcial | Apply Displacement destrutivo | Displacement no vertex shader (`make_mesh.c`) para meshes densos; slider em unidades físicas | P7 | M |  |
| VIEW-08 | Câmeras salvas / bookmarks | ❌ falta | — | Lista em `project_t`; menu Camera | P7 | S |  |
| VIEW-09 | Solo por texture set / UV tile, highlight | ❌ falta | — | Depende de PRJ-05 | P5 | S |  |
| VIEW-10 | Modos por canal e mesh maps | 🟡 parcial | canais sim, mesh maps não | Adicionar modos de mesh map ao `viewport_mode_t` | P2 | S |  |
| VIEW-11 | Path tracer | ✅ feito | (próprio) | OpenPBR na P7 | P7 | — |  |
| VIEW-12 | Wireframe, texels, grid, compass, envmap, split view | ✅ feito | — | — | — | — |  |
| VIEW-13 | Sparse virtual textures (texturas enormes) | ⛔ fora de escopo | (16K já suportado) | — | — | — |  |

## 4.8 Import e export

| ID | Feature SP | Status | Situação no início (P0) | Tarefa | Fase | Tam. | Notas |
|---|---|---|---|---|---|---|---|
| IO-01 | Output templates com tokens `$project $mesh $textureSet $sceneMaterial $udim $udimName $colorSpace` | 🟡 parcial | nome fixo + sufixo | Parser de tokens em `export_texture.c`; campo `name` do preset aceita tokens | P8 | S |  |
| IO-02 | Formato e bits por saída do template; "based on template" vs global | 🟡 parcial | global | Campos `format`/`bits` por textura no JSON; `box_export.c` | P8 | S |  |
| IO-03 | Formatos de export: png ✅, jpg ✅, exr ✅, tga, tiff, psd, webp | 🟡 parcial | 3 de 7 | Writers: TGA (trivial), TIFF 8/16/32 não comprimido, PSD (composto + layers opcionais); webp ⛔ | P8 | M |  |
| IO-04 | Mesh maps e mapas convertidos no template (mixed AO, normal+height, spec/gloss ✅, roughness→smoothness ✅) | 🟡 parcial | — | Fontes `mesh_map:<slot>` e operações `mix`, `combine_normal_height` em `export_texture.c` | P8 | S |  |
| IO-05 | Padding: dilation ✅, transparent, infinite | 🟡 parcial | — | Opções no `dilate` | P8 | S |  |
| IO-06 | Resolução por texture set no export | 🟡 parcial | global | Depende de PRJ-05 | P8 | S |  |
| IO-07 | Export de mesh: obj ✅, glb ✅, fbx, usd | 🟡 parcial | — | Writer FBX ASCII 7.4 mínimo (geometria, UV, normais, materiais) em `export_mesh.c`; USDA opcional | P8 | M |  |
| IO-08 | Nomes OpenPBR no export (12.1) | ❌ falta | — | Preset `openpbr.json` após PRJ-06 | P8 | S |  |
| IO-09 | Send To panel (11.0): Unity, Unreal, Blender, Sketchfab-like | 🟡 parcial | live-link wip | `box_send_to.c` que exporta com preset do alvo e chama bridge (scripts dos repositórios `armorpaint_unity/unreal/blender`) | P8 | M |  |
| IO-10 | Import de texturas: tga/bmp/gif/psd/hdr/svg/exr/tif ✅ (webp fora de escopo) | ✅ feito | — | — | — | — |  |
| IO-11 | Import de mesh: obj/fbx/blend/stl/gltf ✅, usd (12.0), ply, dae | 🟡 parcial | — | PLY (S), DAE ⛔, USD ver PRJ-17 | P8 | S |  |
| IO-12 | Export USD com materiais OpenPBR (12.1) | ❌ falta | — | Opcional, após PRJ-17 | P8 | M |  |
| IO-13 | glTF com texturas embutidas | 🟡 parcial | verificar `export_mesh.c` | Garantir texturas no glb | P8 | S |  |

## 4.9 Interface, UX e scripting

| ID | Feature SP | Status | Situação no início (P0) | Tarefa | Fase | Tam. | Notas |
|---|---|---|---|---|---|---|---|
| UI-01 | Docking, abas, temas, atalhos editáveis, localização | ✅ feito | — | — | — | — |  |
| UI-02 | Properties panel contextual (ferramenta + layer + efeito), grupos colapsáveis (11.1) | 🟡 parcial | header | `tab_properties.c` dockável espelhando `ui_header_draw_tool_properties` + propriedades do layer selecionado | P9 | M |  |
| UI-03 | History panel (lista) | 🟡 parcial | lista no menu Edit | `tab_history.c` | P9 | S |  |
| UI-04 | Log panel | 🟡 parcial | console/debug | `tab_log.c` com níveis | P9 | S |  |
| UI-05 | Texture Set list panel | ❌ falta | — | PRJ-05 | P5 | — |  |
| UI-06 | Preset de keymap "Substance" | ❌ falta | — | `paint/assets/keymap_presets/substance.json` (B/E/G/P/S/L/V/M/alt-rotate/shift-line/ctrl+G grupo/ctrl+M flatten/SHIFT+A loop) | P10 | S |  |
| UI-07 | Minimal UI / distraction free | ✅ feito | — | — | — | — |  |
| UI-08 | Shelf/Assets com tags e busca | 🟡 parcial | — | PRJ-12 | P9 | — |  |
| UI-09 | Undo de "value" com nome do parâmetro (12.1) | 🟡 parcial | — | Nomear ações de slider no histórico | P9 | S |  |
| API-01 | API de scripting: layer stack, export, bake, texture sets, geometry mask include/exclude, smart material save, auto-update, auto-cage | 🟡 parcial | 789 bindings; faltam os novos | Expor cada feature nova em `minic_api_list.h` ao implementá-la; documentar com `--api` | todas | S cada |  |
| API-02 | Plugin manager | ✅ feito | — | — | — | — |  |

## Fora de escopo (seção 3 do plano)

Não contam na meta de 90%. As linhas ⛔ das tabelas acima (PRJ-19, VIEW-13) seguem a mesma decisão.

| Item SP | Decisão |
|---|---|
| `.sbsar` / Substance Engine | ⛔ Proprietário. Substituto: node groups `.arm` com parâmetros expostos (Group Input). |
| Iray + MDL | ⛔ Substituto: path tracer próprio (já existe) com OpenPBR na P7. |
| Adobe 3D Assets / CC Libraries | ⛔ Substituto: cloud do armorpaint.org (já existe) + biblioteca local. |
| Python/JS API idênticas | Equivalência: API em C interpretado + CLI. Conta como ✅ se cobrir as mesmas operações (layer stack, export, bake, texture sets, geometry mask). |
| Import `.ai` | ⛔ `.svg` já existe. |
| Alembic `.abc` | ⛔ (sem biblioteca C permissiva pequena). USD é opcional (P8). |
