# Plano de paridade: ArmorPaint → Substance 3D Painter 12.2 (meta ≥ 90% das features)

Repositório alvo: `maya19952022-eng/armorpaint` (fork de `armory3d/armorpaint`, branch `main`, commit `06c4535` de 2026-10-24).
Referência de produto: Adobe Substance 3D Painter 12.2.0 (lançado em 2026-10-21), incluindo tudo que entrou nas versões 10.x, 11.0, 11.1, 12.0, 12.1 e 12.2.
Fontes: código-fonte do ArmorPaint, manual oficial (`armorpaint.org/manual`), notas de versão oficiais da Adobe (Experience League) e cobertura de imprensa (CG Channel, 80.lv) para 11.x/12.x.

---

## 0. Como usar este documento (prompt para a sessão)

Você é o engenheiro responsável por levar o ArmorPaint à paridade funcional com o Substance 3D Painter 12.2. Este arquivo é a sua especificação. Leia-o inteiro antes de tocar no código.

Regras de trabalho:

1. **Ordem.** Execute as fases na ordem da seção 5 (P0 → P10). Dentro de uma fase, faça primeiro as tarefas marcadas `[bloqueante]`. Não pule para fases seguintes enquanto a atual não compilar nos três alvos de CI.
2. **Uma feature = um commit (ou poucos).** Mensagens no padrão do repositório: prefixo da área em minúsculas, por exemplo `paint: add per-channel layer blending`. Branch por fase: `feat/p1-layer-stack`, `feat/p2-mesh-maps`, etc. Abra um PR por fase contra `main` do fork. Nunca reescreva histórico publicado.
3. **Compilar sempre.** Antes de cada commit: `cd paint && ../base/make --compile` (Linux: precisa de `clang`, `libvulkan-dev`, `libgtk-3-dev`, `libasound2-dev`, `make`). Nenhum warning novo. O ambiente cloud não tem GPU: o que depende de GPU é validado visualmente pelo dono do projeto no Windows (Visual Studio + `base\make`); liste no PR exatamente o que precisa ser testado à mão e como.
4. **Linguagem e estilo.** Todo o código é C (C11/C23), sem C++. Formatação por `.clang-format` da raiz (tabs, alinhamento de declarações). Nomes `snake_case`, prefixo do módulo (`layers_`, `slot_layer_`, `ui_header_`...). Sem dependências novas, exceto bibliotecas single-file em C com licença permissiva (MIT/zlib/BSD/public domain), colocadas em `base/sources/libs/` ou como plugin em `paint/plugins/`.
5. **Strings de interface** sempre via `tr("...")`. Depois de adicionar strings, regenere `paint/assets/locale/en.txt` com `base/tools/extract_locales.c` (`amake --c base/tools/extract_locales.c`) e adicione a tradução em `pt.txt`.
6. **Undo.** Toda operação destrutiva nova tem entrada em `history_action_t` (`paint/sources/enums.h`) e função `history_*` em `paint/sources/history.c`.
7. **Formato de projeto `.arm`.** Campos novos são sempre *opcionais* na leitura (`paint/sources/io/import_arm.c` aplica default quando o campo não existe) e escritos em `export_arm.c`. Projetos antigos precisam continuar abrindo. Teste salvar/abrir após cada mudança de estrutura.
8. **Experimental.** Funcionalidade incompleta fica atrás de `g_config->experimental` até ficar estável.
9. **Matriz de paridade.** Mantenha `docs/parity_matrix.md` (crie na P0 a partir da seção 4) com o status de cada item (`✅ feito`, `🟡 parcial`, `❌ falta`, `⛔ fora de escopo`). Atualize a cada PR. A meta é `✅` em pelo menos 90% dos itens em escopo.
10. **Documentação.** Cada feature nova ganha uma seção em `base/docs/features/<nome>.md` (curta, com os controles e atalhos). O manual público vive em outro repositório (`armorpaint_web`), não o edite aqui.
11. **Não copie nada da Adobe.** Nomes de recursos, texturas, shaders e conteúdo devem ser originais ou CC0. Termos genéricos (smart material, mesh map, polygon fill) podem ser usados.

---

## 1. Estado atual do ArmorPaint (o que já existe)

### 1.1 Arquitetura e mapa do repositório

| Caminho | O que é |
|---|---|
| `base/sources/*.c` | Motor próprio "iron" em C: janela/input (`iron_system.c`, `iron_input.c`), GPU abstrata (`iron_gpu.c/h`), UI imediata (`iron_ui.c`, `iron_ui_ext.c`), editor de nós (`iron_ui_nodes.c`), desenho 2D (`iron_draw.c`), física (`iron_physics.c`), armpack/json, imagens, áudio, raycast. `engine.c` é a cena (objetos, câmeras, render path). |
| `base/sources/backends/` | Direct3D12, Vulkan, Metal, WebGPU, Windows/Linux/macOS/iOS/Android/WASM. |
| `base/sources/kong/` | Compilador da linguagem de shader própria (`.shader` → HLSL/MSL/SPIR-V/WGSL). Todo shader novo é escrito em kong. |
| `base/sources/libs/` | Single-file libs: `minic` (interpretador C usado por plugins e scripts), stb, nfd, bc7enc, lz4, sdefl/sinfl, jsmn, untar. |
| `base/shaders/raytrace/` | Path tracer "brute" e bakers ray-traced (AO, bent normal, thickness, lightmap). |
| `base/tools/amake` | Build tool (gera projeto VS/Xcode/Makefile, compila shaders e assets). `base/tools/iris` é a engine neural local (Flux, DA3, ESRGAN, Qwen). |
| `paint/project.c` | Configuração do build do app (flags, assets, shaders). |
| `paint/sources/enums.h`, `types.h`, `globals.h`, `functions.h` | Enums, structs, globais (`g_context`, `g_config`, `g_project`, `g_ui`, `g_keymap`) e protótipos de tudo. |
| `paint/sources/context.c` | Estado do app (ferramenta, pincel, bake, seleção...). `config.c` = preferências persistidas. `project.c` = projeto. |
| `paint/sources/slots/` | `slot_layer.c` (camadas/máscaras/grupos), `slot_material.c`, `slot_brush.c`, `slot_font.c`, `slot_sound.c`. |
| `paint/sources/util/util_layer.c` | Merge, flatten, criação de layers/máscaras/grupos/filtros, fill layers, UDIM remap (`layers_*`). |
| `paint/sources/render/` | Geração de shaders por nós: `make_paint.c` (shader de pintura por ferramenta), `make_brush.c` (simetria, hardness, stencil), `make_material.c` (shader do viewport + compositor de layers, `make_material_blend_mode` com 18 modos), `make_mesh.c`, `make_bake.c`, `make_sculpt.c`, `make_texcoord.c`; render paths `render_path_base/deferred/forward/paint/raytrace/raytrace_bake.c`; `render_gizmo.c`, `render_grid.c`, `render_compass.c`. |
| `paint/sources/parser_material.c`, `node_shader.c` | Parser do grafo de nós → código kong. |
| `paint/sources/nodes_material/` (62 nós), `nodes_brush/` (15), `nodes_mesh/` (19), `nodes_neural/` (8) | Nós. Registro das categorias em `nodes_material.c`, `nodes_brush.c`, `nodes_mesh.c`. |
| `paint/sources/ui/` | `ui_toolbar.c`, `ui_header.c` (propriedades da ferramenta), `ui_menubar.c`, `ui_dock.c` (docking; `dock_default_leaf`), `ui_view2d.c`, `ui_nodes.c`, `ui_files.c`, abas `tab_*.c`, diálogos `box_*.c`. |
| `paint/sources/io/` | Import/export: `.arm` (armpack), obj, blend, pasta PBR, envmap, LUT, keymap, plugin, fontes; export de texturas (`export_texture.c` + presets JSON), obj/glb (`export_mesh.c`), exr, player web. Plugins compilados em `paint/plugins/` (fbx via ufbx, gltf via cgltf, svg, exr, psd, tiff). |
| `paint/sources/history.c` | Undo/redo (lista `history_action_t`). |
| `paint/sources/plugin.c`, `minic_api.c`, `minic_api_list.h` | API de scripting em C interpretado (789 linhas de bindings). Plugins em `paint/assets/plugins/*.c`. |
| `paint/sources/agent.c`, `console.c` | Agente de IA que gera scripts (Qwen local ou Claude/Grok/Codex). |
| `paint/shaders/*.shader` | Pós-processo e compositor: bloom, ssao, taa, supersample, dilate, layer_merge, mask_apply, outline, cursor, deferred_light, compositor_pass, histogram. |
| `paint/assets/` | `export_presets/*.json`, `keymap_presets/{blender,touch}.json`, `locale/*.txt` (14 idiomas), ícones (`icons.png`, atlas 12 colunas, `resource_tile50`), plugins de exemplo, meshes padrão. |
| `.github/workflows/` | CI: compila em Linux (Vulkan), macOS (Metal), Windows (D3D12) com `../base/make --compile`. Não há testes unitários do app; `base/tests/` tem projetos mínimos. |

Modelo de dados essencial:

- Cada layer (`slot_layer_t`) tem 3 texturas RGBA: `texpaint` (base RGB + opacidade A), `texpaint_nor` (normal RGB + id de material/flags em A), `texpaint_pack` (occlusion R, roughness G, metallic B, height A). Emissão e subsurface são codificadas junto (verificar `make_material.c`/`parser_material.c` antes de mexer). Um layer tem **um** `blending`, **uma** `mask_opacity`, flags `paint_*` por canal, `object_mask`, `uv_type` (UV/triplanar/project), `scale`, `angle`, `uv_map`, `fill_material`, dados de path (`path_points*`), `texpaint_sculpt`.
- Tipos de slot: layer, mask, group (`layer_slot_type_t`). Filtros são layers com `parent` e nós aplicados ao pai (`layers_create_filter`).
- Materiais e pincéis são grafos de nós; o shader de pintura é regenerado ao trocar ferramenta/parâmetros (`make_material_parse_paint_material`).
- Texturas por projeto em `g_project->_->assets`; "Atlases" agrupam objetos em um texture set por atlas.

Receitas (onde mexer para cada tipo de adição):

- **Nova ferramenta:** `tool_type_t` em `enums.h` (manter `TOOL_TYPE_BAKE` por último e a faixa de object tools contígua, ver `util_tool_is_object_tool`), nomes/tooltips em `main.c:289`, ícone em `icons.png` + `icon_t`, atalho padrão `tool_<nome>` em `keymap.c`, propriedades em `ui_header.c`, seleção em `context_select_tool`, shader em `make_paint.c`/`make_brush.c`, cursor em `util_cursor.c`, undo em `history.c`, strings de locale.
- **Novo nó de material:** `paint/sources/nodes_material/<nome>_node.c` com `*_init` (definição `ui_node_t`) e `*_value` (gera kong via `parser_material_write`), protótipos em `functions.h`, categoria em `nodes_material.c`, dispatch por `type` em `parser_material.c`. Modelo: `curvature_bake_node.c`, `blur_node.c`, `map_range_node.c`.
- **Novo tipo de bake:** `bake_type_t` em `enums.h`, código em `make_bake.c` (raster) ou `render_path_raytrace_bake.c` + `base/shaders/raytrace/` (RT), combo em `nodes_material/bake_texture_node.c`.
- **Nova aba:** `tab_id_t` em `enums.h` (append-only, é salvo na config), `ui/tab_<nome>.c`, registro em `ui_dock.c`, entrada no menu Window (`ui_menubar.c`).
- **Nova preferência:** campo em `config_t` (`types.h`), default/load/save em `config.c`, UI em `box_preferences.c`.
- **Novo campo de layer:** `slot_layer_t`, defaults em `slot_layer_create`, cópia em `slot_layer_duplicate`, swap em `history_swap_layer_props`, serialização em `export_arm.c`/`import_arm.c`.
- **Novo campo de export preset:** JSON em `paint/assets/export_presets/`, UI em `box_export.c`, aplicação em `export_texture.c`.
- **Shader novo:** `paint/shaders/<nome>.shader` (kong), carregado com `render_path_load_shader` e desenhado com `render_path_draw_shader`.

### 1.2 Inventário do que já existe (resumo por área)

- **Ferramentas (modo Paint):** Brush, Eraser, Fill (object/face/angle/UV island/mesh/submesh), Decal, Text, Clone, Blur/Smudge, Particle (física), Color ID, Picker, Material (preview), Cursor/gizmo, Select (retângulo 2D/3D, crop), Path e Curve (camadas de caminho com pontos de controle). Modo Sculpt (12 pincéis) e modo Object (primitivas, text mesh, booleanos, modificadores por nós).
- **Pincel:** raio, UV scale, ângulo, opacidade, hardness, 18 modos de blend, texcoord (UV/triplanar/project), X-ray, simetria X/Y/Z, régua (shift), grid snap, stencil com transform (ctrl) e ocultar (z), máscara de pincel (imagem), jitter via nó Random, lazy mouse (Lazy Radius/Step no nó Input), direcional, alinhar à câmera (z), pressão de caneta (raio/hardness/opacidade/ângulo), live brush preview, depth/angle reject, alpha discard.
- **Layers:** paint, fill (de material), decal, path, curve, text, black/white/fill mask, filter (nós sobre o pai), group; blend por layer, opacidade, canais afetados, object mask, UV map, resolução e bits por layer, to fill/paint, merge down/group, duplicate, clear, export, apply/invert mask, drag-and-drop, "Shared", atalhos alt+número.
- **Materiais/nós:** estilo Cycles (noise, voronoi, wave, magic, brick, checker, gradient, gabor, color ramp, RGB/float/vector curves, math, mix, bump, normal map, mix normal, blur, warp, quantize, replace color, HSV, brightness/contrast, invert, gamma, map range, mapping, vector rotate/transform, text texture, tilesheet, camera texture, image, layer, layer mask, picker, geometry, object info, attribute, UV map, wireframe, bake texture, curvature texture, script node, group). Nós neurais locais (text to image, edit image, image to PBR, upscale). Swatches. Workflow PBR/base/sculpt.
- **Baking:** nó Bake Texture: curvature (screen-space), normal/height/derivative de high-poly, object normal, position, texcoord, material ID, object ID, vertex color; ray-traced: AO, lightmap, bent normal, thickness; samples, eixo, up axis; dilate.
- **Viewport:** deferred/forward + path tracer (D3D12/Vulkan/Metal), envmap HDR (intensidade, ângulo, blur, esferas), SSAO, bloom, vignette, noise grain, LUT .cube, supersample 0.25x–4x, TAA, cull backfaces, wireframe, texels, grid, compass, split view, modos por canal, screenshot/vídeo, câmera orbit/rotate/fly, pivot, FoV, perspectiva/orto, 6 vistas, outline de seleção, shader de viewport customizável por plugin.
- **Import:** obj, fbx, blend, stl, gltf/glb (append, split por object/material/UDIM, skinning), pasta PBR → material, jpg/png/tga/bmp/gif/psd/hdr/svg/exr/tif, fontes ttf, .arm (projeto/material/brush/swatches), .cube, keymap, tema, plugin.
- **Export:** texturas (2k–16k/custom, 8/16/32 bits, png/jpg/exr, visíveis/selecionadas/por objeto/por UDIM, presets JSON com swizzle de canais e DirectX normal, pack no projeto, padding), mesh obj/glb, material/brush .arm, player web, bake material.
- **Projeto:** novo projeto por template, save/save as/recent, pack assets, reimport mesh/textures, cloud (conta armorpaint.org), browser de arquivos com bookmarks, undo com histórico listado, keymap editor + presets, temas, 14 idiomas, touch UI, CLI (`--background`, `--export-textures`, `--script`, `--api`).
- **Scripting:** plugins em C interpretado (minic), Scripts tab com editor, console com agente de IA, live-link Unreal/Unity/Blender (wip, repositórios separados), nó Script, nós de mesh por script.
- **Extras que o SP não tem:** sculpt, modelagem por nós, timeline/animação, player, IA local.

---

## 2. Referência: o que o Substance 3D Painter 12.2 oferece

Lista consolidada (doc da Adobe + notas de versão). Os itens marcados com versão entraram recentemente e são a prioridade declarada pelo dono do projeto.

**Projeto e assets**
- Novo projeto: mesh (fbx, obj, gltf/glb, usd/usdz/geo.usd, abc, ply, dae), template (OpenPBR padrão desde 12.1, Metal/Rough, Spec/Gloss, Unity, Unreal, Adobe Standard...), resolução, formato de normal (OpenGL/DirectX), tangentes por fragmento, UV tiles (UDIM), auto-unwrap (organic / **hard surface 12.1**), import de câmeras. **Diálogo reorganizado (12.0).**
- Project configuration: trocar mesh com reprojeção de layers, resolução, template; **checkbox de reimport (12.0)**.
- Texture sets: lista, por set resolução/canais/mesh maps, **adicionar vários canais de uma vez (12.1)**, canais custom (user0–7), instancing de layers entre sets, **flatten de instâncias em todos os sets (12.1)**, nomes custom de UV tiles usados no export (11.0), **aviso ao pintar em outro texture set (12.1)**, **highlight do set/UV tile no viewport (12.2)**.
- Assets panel: filtros por tipo, tags, busca, escopos (project/session/shelf), thumbnails, smart materials/masks, filtros, generators, alphas, texturas, envmaps, tool presets, post-effects presets (12.0); **auto-update de recursos modificados (11.0)**; import de .ai (10.0); 3D Assets (Adobe cloud).
- Auto-save, backups, recuperação.

**Layer stack**
- Paint/fill layers, grupos, máscaras (preta/branca, cor selecionada), effects stack em conteúdo e máscara: Fill, Paint, Generator, Filter, Levels, Compare Mask, Anchor Point, Color Selection; blend e opacidade **por canal**; pass-through; multi-seleção; lock; copy/paste layers e efeitos; instancing; geometry masks (por UV tile/objeto/polígonos, inclusão/exclusão); smart materials/masks (criar e salvar); **Flatten não destrutivo de grupo + export do resultado + salvar original como smart material (12.0)**; fill effects em máscara iniciam em branco (12.1); undo unificado bake+paint (12.1).

**Ferramentas e pincel**
- Paint, Eraser, Projection, Polygon Fill (**patch, loop, partial loop, marquee, visible only, simetria com loop/patch, blend/opacidade, todos os canais, ALT preview, SHIFT+A loop 12.2**), Smudge, Clone, Material Picker, Geometry Decal, Particles, Path tools (paint/eraser/smudge path, **Filled Path 11.0**, **Ribbon 11.1**: estica imagem ao longo do caminho, opacidade/largura por vértice, blend em sobreposição, 75 presets), improvements de path (snap a polígonos, troca de tipo, constraints de ângulo/linha, fechar com um clique, info, escalar/rotacionar vértices, copy/paste vértices conteúdo↔máscara, preview, foco).
- Pincel: size, flow, opacity, spacing, angle, follow path, jitters (size/flow/angle/position), hardness, alpha, stencil, alignment (camera/tangent/UV), backface culling, angle/distance limits, lazy mouse, straight line, simetria planar/radial com offset, **simetria em fill layers/effects (11.1)**, tamanho físico, tool presets, pressão por parâmetro, **blend aditivo para strokes e polygon fill (12.2)**, pintar em 2D/3D.
- Projeções de fill: UV, triplanar, planar, spherical, cylindrical, warp (grade deformável, **Warp to Geometry 12.0**: a grade se conforma à superfície, pontos novos grudam no mesh).

**Procedural / conteúdo**
- Filtros (blur, blur slope/directional, sharpen, levels, HSL, color balance, invert, transform, warp, height to normal, normal to height, baked lighting, MatFx, **stylization, quantize, anisotropic kuwahara, bevel smooth, directional distance, grayscale conversion (11.0)**, fill area mask, embroidery (10.1)).
- Generators (mask builder, mask editor, metal edge wear, dirt, dripping rust, light, position, curvature, AO, thickness, world space normals, UV random color, 3D linear/distance, **tile random, triangle grid, scratches (11.0)**), biblioteca de noises/grunges (atualizada 11.0 com 2D Voronoi), alphas, hard surface stamps, smart masks (~dezenas), smart materials (centenas), .sbsar com parâmetros, override de resolução de substance (11.1), text resource (10.0).

**Baking**
- Mesh maps: Normal, World Space Normal, ID, AO, Curvature, Position, Thickness, Bent Normals, Opacity. Settings comuns (resolução, AA 2x2/4x4/8x8, dilation, high-poly list, cage/offset, match by name, distâncias frontal/rear, average normals, ignore backface) e por baker. **Auto cage (11.0, experimental)**, **bakers GPU mais rápidos com RT AMD (11.1)**, **auto rebake incremental + quick rebake + UI reorganizada em settings comuns vs mesh map (12.1)**, **skew map painting (brush/eraser/polygon fill, preview shader, vetores de direção, edge protection, base normal mesh/per-triangle) (12.1)**, **cage painting: offset de cage por pixel pintado sobre o offset global, visualização, select/fill, blend, HDR slider, reset (12.2)**, export preset de mesh maps em grayscale (11.1).

**Viewport e render**
- Viewport PBR em tempo real + Iray; display settings (envmap, rotação, exposição, sombras, color profile/ACES/OCIO, LUT); **10 pós-efeitos: DOF, bloom, glare, lens flare, aberração lateral, vignette, sharpen, film grain, tone mapping (novo tone mapper), color correction; presets de pós-efeitos como assets (12.0)**; **display settings reagrupados (12.2)**; **OIT para transparências (12.2)**; shader settings (**OpenPBR 1.1 padrão 12.1**: coat, fuzz, thin film, transmission, specular ior/anisotropy, emission...), tessellation/displacement (**em unidade física 11.1**), sparse virtual textures, wireframe, modos por canal/mesh map, solo, câmeras salvas, Vulkan/Metal (11.1/11.0).

**Export e integração**
- Output templates com tokens (`$project $mesh $textureSet $sceneMaterial $udim $colorSpace`), packing RGBA por saída incluindo mesh maps e mapas convertidos, formato e bits por template, png/jpg/tga/tiff/exr/psd/webp, padding (dilation/transparent/infinite), resolução por set, export de mesh (fbx/obj/glb/usd), **nomes OpenPBR no export (12.1)**, **Send To panel (11.0)** (Unity, Unreal, Designer, Stager, Sketchfab), USD com materiais (10.1/12.0/12.1), glTF.

**Scripting**
- Python API (layer stack 10.0, geometry mask include/exclude 12.1, auto-update, bake auto-cage, smart material save...), JavaScript API, plugin manager, logs.

**Fora de escopo (não implementável ou proprietário):** engine Substance `.sbsar`, Iray/MDL, Adobe 3D Assets/Creative Cloud, Illustrator `.ai` nativo, Python/JS da Adobe como API idêntica. Esses itens não contam na meta de 90%.

---

## 3. Fora de escopo e equivalências aceitas

| Item SP | Decisão |
|---|---|
| `.sbsar` / Substance Engine | ⛔ Proprietário. Substituto: node groups `.arm` com parâmetros expostos (Group Input). |
| Iray + MDL | ⛔ Substituto: path tracer próprio (já existe) com OpenPBR na P7. |
| Adobe 3D Assets / CC Libraries | ⛔ Substituto: cloud do armorpaint.org (já existe) + biblioteca local. |
| Python/JS API idênticas | Equivalência: API em C interpretado + CLI. Conta como ✅ se cobrir as mesmas operações (layer stack, export, bake, texture sets, geometry mask). |
| Import `.ai` | ⛔ `.svg` já existe. |
| Alembic `.abc` | ⛔ (sem biblioteca C permissiva pequena). USD é opcional (P8). |

---

## 4. Matriz de gap (feature → status hoje → tarefa → fase)

Legenda de status hoje: ✅ existe | 🟡 parcial | ❌ falta | ⛔ fora de escopo. Tamanho: S (≤1 dia), M (2–5 dias), L (1–3 semanas), XL (>3 semanas), estimados para um dev sênior; use para priorizar.

### 4.1 Projeto, texture sets e assets

| ID | Feature SP | Hoje | Tarefa | Fase | Tam. |
|---|---|---|---|---|---|
| PRJ-01 | Diálogo New Project completo (mesh, template, resolução, normal format, UV tiles, auto-unwrap, câmeras) (12.0) | 🟡 só template | `box_new_project.c` novo fluxo em etapas; chamar `import_mesh` com opções; templates em `paint/assets/project_templates/*.json` | P5 | M |
| PRJ-02 | Project Configuration (trocar resolução, template, reimport mesh com checkbox) (12.0) | 🟡 reimport existe sem diálogo | `box_project_config.c`; reusar `layers_on_resized` | P5 | M |
| PRJ-03 | Reimport mesh com reprojeção de layers (UV mudou) | ❌ | Bake de position+normal do mesh antigo, reprojeção world-space por layer (`util_layer.c`, shader `layer_reproject.shader`) | P8 | L |
| PRJ-04 | Formato de normal do projeto (OpenGL/DirectX) | 🟡 só no export preset e na importação de texturas | Campo `normal_map_format` em `project_t`; aplicar em bake, viewport e export | P5 | S |
| PRJ-05 | Texture Set list (por atlas/material/objeto; resolução, canais, mesh maps por set) | 🟡 Atlases no export; layers por objeto | `tab_texture_sets.c`; modelo `texture_set_t` em `project_t` unificando atlas/objeto/material; seleção ativa | P5 | L |
| PRJ-06 | Canais custom (user0–7) e canais OpenPBR (12.1) | ❌ canais fixos em 3 texturas | Texturas extras `texpaint_user[]` por layer alocadas sob demanda; registro de canais por set; sockets dinâmicos no Material Output; `make_paint`/`make_material`/`pipes`/export generalizados | P5 | XL |
| PRJ-07 | Adicionar vários canais de uma vez (12.1) | ❌ | Janela de seleção múltipla em `tab_texture_sets.c` | P5 | S |
| PRJ-08 | Instancing de layers entre texture sets + flatten de instâncias (12.1) | 🟡 "Shared" por objeto | Flag `instance_of` em `slot_layer_t`; propagação na composição; flatten em todos os sets | P5 | M |
| PRJ-09 | Nomes custom de UV tiles usados no export (11.0) | ❌ | Mapa tile→nome em `project_t`; token `$udim`/`$udimName` | P8 | S |
| PRJ-10 | Aviso ao pintar em outro texture set (12.1) | ❌ | Em `render_path_paint.c`: checar set do hit vs ativo; overlay de aviso | P5 | S |
| PRJ-11 | Highlight do set/UV tile sob o cursor (12.2) | ❌ | Reusar `render_path_base_draw_outline` com máscara por set | P5 | S |
| PRJ-12 | Assets panel unificado (tipos, tags, busca, escopos, thumbnails) | 🟡 abas separadas + browser + busca | `tab_assets.c` agregando Materials/Brushes/Textures/Fonts + novos tipos (smart materials, smart masks, filters, generators, alphas, envmaps, tool presets, post-effects presets); tags em JSON sidecar | P9 | L |
| PRJ-13 | Auto-update de recursos modificados em disco (11.0) | 🟡 Reimport Textures manual | Watcher por mtime (`iron_file.c`) com opção em preferências; aplica a texturas, fontes, materiais `.arm` | P9 | M |
| PRJ-14 | Auto-save, backups e recuperação | 🟡 plugin autosave | Mover para o core (`project.c`): intervalo, N backups rotativos, recuperação ao abrir | P9 | S |
| PRJ-15 | Copy/paste de layers entre projetos | ❌ | Clipboard interno serializando subárvore em `.arm` temporário (`export_arm.c` já serializa layers) | P9 | M |
| PRJ-16 | Templates OpenPBR, Metal/Rough, Spec/Gloss, Unity, Unreal | 🟡 Workflow PBR/base/sculpt | Templates definem canais, shader, export preset padrão | P5 | M |
| PRJ-17 | Import USD/USDZ/.geo.usd (12.0) | ❌ | Opcional: leitor USDA/USDC mínimo (mesh + UV + materiais) como plugin `paint/plugins/io_usd/` | P8 | L |
| PRJ-18 | Import de câmeras do mesh | ❌ | fbx/gltf já expõem câmeras via ufbx/cgltf; criar `camera_object` por câmera | P8 | S |
| PRJ-19 | 3D Assets Adobe | ⛔ | — | — | — |

### 4.2 Layer stack

| ID | Feature SP | Hoje | Tarefa | Fase | Tam. |
|---|---|---|---|---|---|
| LAY-01 | Blend mode e opacidade **por canal** | ❌ um blend e uma opacidade por layer | Arrays `blending[ch]`/`opacity[ch]` em `slot_layer_t`; `layers_merge_layer` e `make_material.c` por canal (já há `pipes_merge_r/g/b`); UI em `tab_layers.c` com seletor de canal | P1 | M |
| LAY-02 | Pass-through em grupos | ❌ | Flag `passthrough` no grupo; compositor ignora o grupo como unidade | P1 | S |
| LAY-03 | Multi-seleção de layers (mover, apagar, agrupar, flatten) | ❌ | `g_context->layers_selected` (lista); ações aplicadas em lote; ctrl/shift-clique | P1 | M |
| LAY-04 | Lock de layer | ❌ | Flag `locked`; `render_path_paint` recusa pintura; ícone | P1 | S |
| LAY-05 | Effects stack em conteúdo **e** máscara (Fill, Paint, Generator, Filter, Levels, Compare Mask, Anchor, Color Selection) | 🟡 filtros só no conteúdo; fill mask; máscara pintável | Generalizar filtro para máscaras (`layers_create_filter` com alvo mask); "Paint effect" = sublayer pintável dentro do stack; UI do stack expandível por layer | P1 | L |
| LAY-06 | Levels | ❌ nó não existe | Nó `levels_node.c` (in/out black/white, gamma, por canal) + efeito rápido | P1 | S |
| LAY-07 | HSL / Color Balance / Sharpen como efeitos rápidos | 🟡 HSV existe; sharpen e color balance não | Nós `color_balance_node.c`, `sharpen_node.c`; menu "Add effect" em `tab_layers.c` cria filtro com o nó pronto | P1 | S |
| LAY-08 | Compare Mask | ❌ | Nó `compare_mask_node.c` (fonte: camada abaixo/anchor, operador, threshold) | P1 | S |
| LAY-09 | Anchor points | 🟡 nós Layer/Layer Mask referenciam layers | Comando "Add Anchor Point" que nomeia e lista no menu de nós; nó `anchor_node.c` apontando por id | P1 | S |
| LAY-10 | Color Selection (máscara por color ID) como efeito | 🟡 Color ID tool → To Mask (destrutivo) | Nó `color_selection_node.c` (lista de cores com tolerância) usável em fill mask | P1 | S |
| LAY-11 | Geometry mask (UV tile/objeto/polígonos, include/exclude) | 🟡 object mask; fill por face | Máscara geométrica por layer: lista de tiles/objetos/ids de polígono, include/exclude; aplicada no `make_paint` e `make_material` | P1 | M |
| LAY-12 | Smart masks (criar, salvar, aplicar, merge/overwrite com ctrl) | ❌ | Serializar subárvore de máscara+efeitos para `.arm` (`export_arm.c`); categoria no Assets; drop em layer | P3 | M |
| LAY-13 | Smart materials (criar de grupo, salvar, aplicar) | ❌ | Serializar grupo (layers, máscaras, efeitos, fill materials, blends) para `.arm`; instanciar em outro projeto/set | P3 | M |
| LAY-14 | Flatten não destrutivo de grupo + export do resultado + salvar original como smart material (12.0) | 🟡 Merge Group destrutivo | `layers_flatten_copy(group)` cria layer "flattened" mantendo o grupo oculto; "Export flattened" chama `export_texture` com apenas esse layer; atalho ctrl+M | P1 | M |
| LAY-15 | Copy/paste de layer, máscara e efeitos | 🟡 duplicate | Clipboard interno (ver PRJ-15) + "Paste as mask" | P1 | S |
| LAY-16 | Fill effects em máscara começam brancos (12.1) | ❌ | Default no `layers_new_mask` para fill mask | P1 | S |
| LAY-17 | Thumbnails de layer/máscara, isolar máscara (alt+clique), desabilitar temporariamente (shift+clique) | 🟡 preview existe | Atalhos em `tab_layers.c`; modo viewport "mask solo" (já há `VIEWPORT_MODE_MASK`) | P1 | S |
| LAY-18 | Undo unificado bake+paint (12.1) | ✅ | — | — | — |

### 4.3 Ferramentas e pincel

| ID | Feature SP | Hoje | Tarefa | Fase | Tam. |
|---|---|---|---|---|---|
| TOOL-01 | Flow separado de opacity (acúmulo por dab) | ❌ opacity único | `brush_flow` em `context_t`; acumular em `texpaint_live`/undo com max por stroke (`render_path_paint.c`, `make_brush.c`) | P4 | M |
| TOOL-02 | Spacing | 🟡 lazy step | `brush_spacing` (% do raio) na interpolação de `inplast` em `make_brush.c`; socket no nó Input | P4 | S |
| TOOL-03 | Jitter de size/flow/angle/position na UI | 🟡 via nó Random | Sliders no header que injetam no grafo do pincel (ou leem `brush_nodes_uses_random`) | P4 | S |
| TOOL-04 | Alignment (camera / tangent / UV / normal) | 🟡 direcional + camera align por tecla | Combo `brush_alignment` aplicado no cálculo da base do decal/stencil em `make_brush.c` | P4 | S |
| TOOL-05 | Backface, angle limit e distance limit no header | 🟡 nas preferências | Expor por ferramenta em `ui_header.c`; distância em `make_paint.c` (depth reject com limiar) | P4 | S |
| TOOL-06 | Simetria radial e offset de plano | 🟡 X/Y/Z no origem | `sym_radial_count`, `sym_offset[3]` em `context_t`; loop no shader de `make_brush.c`; gizmo de plano em `render_gizmo.c` | P4 | M |
| TOOL-07 | Simetria em fill layers e efeitos (11.1) | ❌ | Aplicar simetria no `make_paint` quando `fill_material` e no efeito fill | P4 | S |
| TOOL-08 | Tamanho físico (unidades) | ❌ | Calcular bounds do mesh; exibir raio em cm; opção "physical size" em pincel e em fill scale | P4 | S |
| TOOL-09 | Tool presets (ferramenta + parâmetros + material/alpha) | 🟡 Brushes = grafos | `tool_preset_t` serializado em `.arm`; aba/categoria no Assets; shortcuts | P4 | M |
| TOOL-10 | Polygon Fill: triângulo/polígono/UV island/objeto | 🟡 face=triângulo | Preservar ids de polígono (quads/ngons) no import (`import_obj.c`, `io_fbx.c`, gltf) em um vertex attribute; fill por polígono | P4 | M |
| TOOL-11 | Polygon Fill: loop, partial loop, patch, marquee, visible only, SHIFT+A, ALT preview (12.2) | ❌ | Adjacência de polígonos em `util_geom.c`; loop/patch por direção de aresta; marquee = select tool; preview no `render_path_paint` | P4 | L |
| TOOL-12 | Polygon Fill com simetria + preview de aresta (12.2) | ❌ | Espelhar ids via posição; desenhar aresta de simetria | P4 | S |
| TOOL-13 | Polygon Fill com blend/opacidade e todos os canais, resume previous stroke (12.2) | 🟡 | Mesmo caminho do brush (`brush_blending`, `brush_opacity`) para fill | P4 | S |
| TOOL-14 | Blend aditivo para strokes e polygon fill (12.2) | ✅ (`BLEND_TYPE_ADD`) | Garantir que fill respeita | P4 | S |
| TOOL-15 | Projection tool (stencil/projeção com manipulação) | 🟡 decal + stencil | Unificar como modo "Projection" do brush com grade de transform (ctrl) e tiling | P4 | S |
| TOOL-16 | Geometry Decal | 🟡 decal layer com `decal_mat` | Decal snap à normal com warp (ver FILL-05) | P6 | S |
| TOOL-17 | Smudge | ✅ | — | — | — |
| TOOL-18 | Clone | ✅ | — | — | — |
| TOOL-19 | Particles | ✅ | — | — | — |
| TOOL-20 | Material picker | ✅ | — | — | — |
| TOOL-21 | Path tools: paint/eraser/smudge ao longo de path, Filled Path (11.0) | 🟡 path/curve layers | `path_tool_mode` (paint, erase, smudge, fill) em `util_path_tool.c`; preenchimento de path fechado por scanline em UV | P4 | M |
| TOOL-22 | Ribbon (11.1): estica imagem ao longo do path, largura/opacidade por vértice, blend em sobreposição, fechado | ❌ | Geração de faixa UV (strip) ao longo da curva em `util_path_tool.c`; `texcoord` 0–1 ao longo do comprimento; atributos por ponto | P4 | L |
| TOOL-23 | Path improvements (11.0): snap a polígonos, trocar tipo, constraints de ângulo/linha, fechar com 1 clique, info, escalar/rotacionar vértices, copy/paste vértices, preview, backspace | 🟡 pontos + delete | Itens em `util_path_tool.c` + header; gizmo de vértices via `render_gizmo.c` | P4 | M |
| TOOL-24 | Lazy mouse, straight line | ✅ | — | — | — |
| TOOL-25 | Pressão por parâmetro | ✅ | — | — | — |
| TOOL-26 | Pintar no 2D view | ✅ | — | — | — |
| TOOL-27 | Properties agrupadas/colapsáveis (11.1) | 🟡 header linear | `tab_properties.c` (ver UI-02) | P9 | M |
| TOOL-28 | Color picker grayscale (12.1) | 🟡 | Variante em `ui_color_wheel` | P4 | S |

### 4.4 Projeções de fill

| ID | Feature SP | Hoje | Tarefa | Fase | Tam. |
|---|---|---|---|---|---|
| FILL-01 | UV, triplanar | ✅ | — | — | — |
| FILL-02 | Planar (com gizmo) | 🟡 "Project" + decal_mat | Gizmo de plano com escala/rotação em `render_gizmo.c` | P6 | S |
| FILL-03 | Spherical, cylindrical | ❌ | `uv_type_t` + `make_texcoord.c` novas funções; gizmo | P6 | M |
| FILL-04 | Warp (grade deformável) | ❌ | Grade NxM de pontos em espaço de projeção; shader amostra via malha de quads (`make_texcoord.c`); edição de pontos no viewport | P6 | L |
| FILL-05 | Warp to Geometry (12.0): grade conforma à superfície, pontos novos grudam no mesh, mantém deformação | ❌ | Pontos da grade com raycast à superfície (`iron_raycast.c`); projeção por triângulo da grade em espaço tangente; opção toggle | P6 | L |
| FILL-06 | Reorientar normais ao projetar em warp (11.1) | ❌ | Rotação da normal pelo jacobiano da grade | P6 | S |
| FILL-07 | Tiling/offset/rotation da projeção | ✅ (scale/angle) | Offset | P6 | S |

### 4.5 Procedural e biblioteca

| ID | Feature SP | Hoje | Tarefa | Fase | Tam. |
|---|---|---|---|---|---|
| LIB-01 | Filtros: blur ✅, blur slope, blur directional, sharpen, levels, HSL ✅, color balance, invert ✅, transform ✅, warp ✅, height→normal ✅ (bump), normal→height, baked lighting, contrast/luminosity, stylization, quantize ✅, anisotropic kuwahara, bevel smooth, directional distance, grayscale ✅, fill area mask, embroidery | 🟡 ~8 de 22 | Nós nativos novos: `slope_blur`, `directional_blur`, `sharpen`, `levels`, `color_balance`, `normal_to_height` (solver iterativo em compute, ou CPU com cache), `baked_lighting` (usa envmap + world normal), `kuwahara`, `bevel_smooth` (distance field), `directional_distance`, `stylization`; node groups `.arm` para fill area e embroidery | P3 | L |
| LIB-02 | Generators: mask builder, mask editor, metal edge wear, dirt, dripping rust, light, position ✅, curvature ✅, AO ✅, thickness ✅, world normal ✅, UV random color, 3D linear/distance, tile random, triangle grid, scratches | 🟡 5 de 16 | Nó `Mesh Map` (lê slots da P2); node groups com Group Input para parâmetros; nativos: `uv_island_id` (novo bake), `scratches`, `tile_random`, `triangle_grid`, `dripping` (marcha em direção world-down usando position/normal) | P3 | L |
| LIB-03 | Biblioteca de noises/grunges (procedural, 2D Voronoi) | 🟡 nós base | ~40 node groups prontos (`paint/assets/materials/grunges/*.arm`) | P3 | M |
| LIB-04 | Alphas e hard surface stamps | 🟡 svg import | 60+ alphas CC0 (png/svg) em `paint/assets/alphas/`, categoria no Assets | P3 | S |
| LIB-05 | Smart masks prontos (~40) | ❌ | Autorar após LAY-12 | P3 | M |
| LIB-06 | Smart materials prontos (~60) | ❌ | Autorar após LAY-13 (metais, madeiras, plásticos, tecidos, pedras, pinturas, emissivos) | P3 | L |
| LIB-07 | Parâmetros expostos em node groups (equivalente a sbsar) | 🟡 Group Input | Painel de parâmetros do grupo no layer/efeito sem abrir o editor de nós | P3 | M |
| LIB-08 | Override de resolução de recurso (11.1) | ❌ | Campo por nó Image/Group | P3 | S |
| LIB-09 | Text resource (10.0) | ✅ (text tool + Text Texture) | — | — | — |
| LIB-10 | Post-effects presets como assets (12.0) | ❌ | Serializar settings de pós-efeitos em JSON; categoria no Assets | P7 | S |

### 4.6 Baking

| ID | Feature SP | Hoje | Tarefa | Fase | Tam. |
|---|---|---|---|---|---|
| BAKE-01 | Mesh maps como slots por texture set (Normal, World Normal, ID, AO, Curvature, Position, Thickness, Bent Normals, Opacity) consumidos por generators | 🟡 bake por nó, salvo em layer/Textures | `mesh_map_t` por set em `project_t`; `util_bake.c` com job API headless; nó `Mesh Map` | P2 | L |
| BAKE-02 | Diálogo Baking mode: settings comuns separados dos por-mapa (12.1), seleção de mapas, bake all/selected | ❌ | `box_bake.c` (ou modo de workspace "Bake") | P2 | M |
| BAKE-03 | High-poly list, match by name (sufixos `_low/_high`), cage/offset global, distâncias frontal/rear, average normals, ignore backface, AA 2x2/4x4/8x8, dilation | 🟡 high-poly simples, dilate | Estender `render_path_paint.c` bake de high poly: raycast com cage e distâncias; supersample na resolução + downsample; parser de nomes | P2 | L |
| BAKE-04 | Auto cage (11.0) | ❌ | Cage = low-poly extrudido por normal com offset suavizado (`util_geom.c`) | P2 | M |
| BAKE-05 | Auto rebake incremental e quick rebake (12.1) | ❌ | Marcar região suja (bbox UV) ao mudar parâmetro; rebake só da região; botão "rebake visível" | P2 | M |
| BAKE-06 | Skew map painting (12.1): brush/eraser/polygon fill, preview shader, vetores de direção, edge protection, base normal mesh/per-triangle | ❌ | Skew map = textura RGBA por set; ferramenta de pintura reaproveita brush; bake de normal usa direção do raio = normal interpolada do skew; shader de preview | P2 | L |
| BAKE-07 | Cage painting (12.2): offset de cage por pixel sobre o global, visualização, select/fill, blend, HDR slider, reset | ❌ | Textura de offset (float) por set; bake lê offset por texel; visualização do cage em wireframe translúcido | P2 | M |
| BAKE-08 | Opacity bake, UV island ID bake | ❌ | `BAKE_TYPE_OPACITY`, `BAKE_TYPE_UV_ISLAND` em `make_bake.c` | P2 | S |
| BAKE-09 | Curvature por geometria (não screen-space) | 🟡 | Curvatura por vértice via adjacência (`util_geom.c`) rasterizada; opção "Mesh" vs "Normal map" | P2 | M |
| BAKE-10 | Export preset "Mesh maps" grayscale (11.1) | ❌ | Preset JSON lendo slots de mesh map | P8 | S |
| BAKE-11 | Bakers GPU RT (AMD) | ✅ (D3D12/Vulkan RT) | — | — | — |
| BAKE-12 | Histórico unificado | ✅ | — | — | — |

### 4.7 Viewport, render e shading

| ID | Feature SP | Hoje | Tarefa | Fase | Tam. |
|---|---|---|---|---|---|
| VIEW-01 | Pós-efeitos (12.0): DOF, bloom ✅, glare, lens flare, aberração lateral, vignette ✅, sharpen, film grain ✅ (noise grain), tone mapping selecionável, color correction | 🟡 4 de 10 | Shaders kong em `paint/shaders/`: `dof_pass`, `glare_pass` (streaks), `lens_flare_pass`, `chromatic_pass`, `sharpen_pass`, `color_correction_pass` (exposure, contrast, saturation, lift/gamma/gain, temperature); tone mappers (Linear, Reinhard, Filmic, ACES, AgX) em `compositor_pass.shader`; ordem configurável; UI em `box_preferences.c`/menu Viewport reagrupado | P7 | L |
| VIEW-02 | Display settings reagrupados (12.2) + presets de pós-efeitos como assets | ❌ | Painel "Display Settings" próprio (`box_display.c`); JSON de preset | P7 | S |
| VIEW-03 | Exposição e color profile (sRGB/ACES/OCIO-lite) + LUT ✅ | 🟡 | Exposure slider; working space linear → display transform selecionável | P7 | S |
| VIEW-04 | OpenPBR 1.1 shader (coat, fuzz, thin film, transmission, specular color/ior/anisotropy, subsurface, emission) (12.1) | 🟡 metal/rough + emission + subsurface + translucency | BRDF em `deferred_light.shader`/forward + path tracer `raytrace_brute_*`; sockets no Material Output por template; depende de PRJ-06 | P7 | L |
| VIEW-05 | Order Independent Transparency (12.2) | ❌ | Weighted-blended OIT no forward path (`render_path_forward.c`, shaders) | P7 | M |
| VIEW-06 | Sombras (luz direcional) | ❌ só IBL | Luz sol opcional com shadow map (`render_path_deferred.c`) | P7 | M |
| VIEW-07 | Tessellation/displacement no viewport com unidade física (11.1) | 🟡 Apply Displacement destrutivo | Displacement no vertex shader (`make_mesh.c`) para meshes densos; slider em unidades físicas | P7 | M |
| VIEW-08 | Câmeras salvas / bookmarks | ❌ | Lista em `project_t`; menu Camera | P7 | S |
| VIEW-09 | Solo por texture set / UV tile, highlight | ❌ | Depende de PRJ-05 | P5 | S |
| VIEW-10 | Modos por canal e mesh maps | 🟡 canais sim, mesh maps não | Adicionar modos de mesh map ao `viewport_mode_t` | P2 | S |
| VIEW-11 | Path tracer | ✅ (próprio) | OpenPBR na P7 | P7 | — |
| VIEW-12 | Wireframe, texels, grid, compass, envmap, split view | ✅ | — | — | — |
| VIEW-13 | Sparse virtual textures (texturas enormes) | ⛔ (16K já suportado) | — | — | — |

### 4.8 Import e export

| ID | Feature SP | Hoje | Tarefa | Fase | Tam. |
|---|---|---|---|---|---|
| IO-01 | Output templates com tokens `$project $mesh $textureSet $sceneMaterial $udim $udimName $colorSpace` | 🟡 nome fixo + sufixo | Parser de tokens em `export_texture.c`; campo `name` do preset aceita tokens | P8 | S |
| IO-02 | Formato e bits por saída do template; "based on template" vs global | 🟡 global | Campos `format`/`bits` por textura no JSON; `box_export.c` | P8 | S |
| IO-03 | Formatos de export: png ✅, jpg ✅, exr ✅, tga, tiff, psd, webp | 🟡 3 de 7 | Writers: TGA (trivial), TIFF 8/16/32 não comprimido, PSD (composto + layers opcionais); webp ⛔ | P8 | M |
| IO-04 | Mesh maps e mapas convertidos no template (mixed AO, normal+height, spec/gloss ✅, roughness→smoothness ✅) | 🟡 | Fontes `mesh_map:<slot>` e operações `mix`, `combine_normal_height` em `export_texture.c` | P8 | S |
| IO-05 | Padding: dilation ✅, transparent, infinite | 🟡 | Opções no `dilate` | P8 | S |
| IO-06 | Resolução por texture set no export | 🟡 global | Depende de PRJ-05 | P8 | S |
| IO-07 | Export de mesh: obj ✅, glb ✅, fbx, usd | 🟡 | Writer FBX ASCII 7.4 mínimo (geometria, UV, normais, materiais) em `export_mesh.c`; USDA opcional | P8 | M |
| IO-08 | Nomes OpenPBR no export (12.1) | ❌ | Preset `openpbr.json` após PRJ-06 | P8 | S |
| IO-09 | Send To panel (11.0): Unity, Unreal, Blender, Sketchfab-like | 🟡 live-link wip | `box_send_to.c` que exporta com preset do alvo e chama bridge (scripts dos repositórios `armorpaint_unity/unreal/blender`) | P8 | M |
| IO-10 | Import de texturas: tga/bmp/gif/psd/hdr/svg/exr/tif ✅ (webp fora de escopo) | ✅ | — | — | — |
| IO-11 | Import de mesh: obj/fbx/blend/stl/gltf ✅, usd (12.0), ply, dae | 🟡 | PLY (S), DAE ⛔, USD ver PRJ-17 | P8 | S |
| IO-12 | Export USD com materiais OpenPBR (12.1) | ❌ | Opcional, após PRJ-17 | P8 | M |
| IO-13 | glTF com texturas embutidas | 🟡 verificar `export_mesh.c` | Garantir texturas no glb | P8 | S |

### 4.9 Interface, UX e scripting

| ID | Feature SP | Hoje | Tarefa | Fase | Tam. |
|---|---|---|---|---|---|
| UI-01 | Docking, abas, temas, atalhos editáveis, localização | ✅ | — | — | — |
| UI-02 | Properties panel contextual (ferramenta + layer + efeito), grupos colapsáveis (11.1) | 🟡 header | `tab_properties.c` dockável espelhando `ui_header_draw_tool_properties` + propriedades do layer selecionado | P9 | M |
| UI-03 | History panel (lista) | 🟡 lista no menu Edit | `tab_history.c` | P9 | S |
| UI-04 | Log panel | 🟡 console/debug | `tab_log.c` com níveis | P9 | S |
| UI-05 | Texture Set list panel | ❌ | PRJ-05 | P5 | — |
| UI-06 | Preset de keymap "Substance" | ❌ | `paint/assets/keymap_presets/substance.json` (B/E/G/P/S/L/V/M/alt-rotate/shift-line/ctrl+G grupo/ctrl+M flatten/SHIFT+A loop) | P10 | S |
| UI-07 | Minimal UI / distraction free | ✅ | — | — | — |
| UI-08 | Shelf/Assets com tags e busca | 🟡 | PRJ-12 | P9 | — |
| UI-09 | Undo de "value" com nome do parâmetro (12.1) | 🟡 | Nomear ações de slider no histórico | P9 | S |
| API-01 | API de scripting: layer stack, export, bake, texture sets, geometry mask include/exclude, smart material save, auto-update, auto-cage | 🟡 789 bindings; faltam os novos | Expor cada feature nova em `minic_api_list.h` ao implementá-la; documentar com `--api` | todas | S cada |
| API-02 | Plugin manager | ✅ | — | — | — |

Contagem (itens em escopo, excluindo ⛔): 129 itens. Hoje: ✅ 20, 🟡 61, ❌ 48. Meta: ≥ 117 itens em ✅ (90%).

---

## 5. Plano de implementação por fases

Cada fase tem objetivo, tarefas (IDs da seção 4), pontos técnicos e critérios de aceite. O ambiente cloud compila mas não roda GPU: cada PR deve listar o roteiro de teste manual no Windows.

### P0 — Base, infraestrutura e scoreboard `[bloqueante]`

Tarefas:
- Confirmar build Linux (`cd paint && ../base/make --compile`) e corrigir qualquer quebra. Registrar em `base/docs/building_cloud.md` o comando exato e os pacotes.
- Criar `docs/parity_matrix.md` a partir da seção 4 (mesmos IDs e status). Criar `base/docs/features/README.md`.
- Adicionar um alvo de "smoke test" headless: script `base/tests/paint_headless/run.c` que usa a CLI (`--background --script`) para abrir um `.arm` de exemplo, criar layer, salvar e exportar; documentar que só roda em máquina com GPU. Adicionar `scripts/ci_compile.sh` que compila os três alvos quando disponíveis.
- Ler e anotar em `base/docs/architecture_notes.md` exatamente como `texpaint_nor.a` codifica material id/emission/subsurface, como `layers_merge_layer` e `make_material.c` compõem camadas, e como `render_path_paint.c` faz o stroke. Esses três pontos são pré-requisitos de P1, P2 e P4.

Aceite: CI verde, documentos criados, matriz com 129 linhas.

### P1 — Layer stack (LAY-01 … LAY-17)

Ordem: LAY-01 (per-channel blend/opacity) `[bloqueante]` → LAY-14 (flatten) → LAY-03/04/15/16/17 → LAY-05 (effects stack) → LAY-06/07/08/09/10 (nós e efeitos rápidos) → LAY-02 → LAY-11.

Pontos técnicos:
- `slot_layer_t`: substituir `blending` por `blend_type_t blending[LAYER_CH_COUNT]` e `f32 opacity[LAYER_CH_COUNT]` onde `LAYER_CH_COUNT` cobre base, opacity, normal, occlusion, roughness, metallic, height, emission, subsurface (ordem fixa; canais custom entram na P5). Manter getters `slot_layer_get_blending(l, ch)`; o antigo campo vira `blending[CH_BASE]` na importação de projetos antigos.
- `layers_merge_layer`: já usa `pipes_merge_r/g/b` para canais do pack; estender para passar blending/opacidade por canal (uniforms `pipes_blending_ch[4]`, `pipes_opacity_ch[4]`) e atualizar `layer_merge.shader`. `make_material.c`: `make_material_blend_mode` por canal no compositor live.
- Flatten (LAY-14): nova função `layers_flatten_group_copy(group)` que compõe o grupo em um layer irmão acima, marca o grupo como `visible=false` e guarda `flatten_source_id` no layer; menu "Flatten" (ctrl+M), "Export flattened..." (abre `box_export` com modo `EXPORT_MODE_SELECTED` nesse layer), "Save group as smart material" (depende de LAY-13; nesta fase apenas exportar o grupo como `.arm` usando `export_arm` parcial).
- Effects stack (LAY-05): representar efeitos como filhos do layer/máscara com `layer_slot_type_t` novo `LAYER_SLOT_TYPE_EFFECT` e `effect_type_t` {fill, paint, filter, levels, generator, compare, anchor, color_selection}. Composição: ao compor uma máscara, aplicar a cadeia de efeitos em ordem (cada efeito é um nó-grafo ou um layer pintável). Reaproveitar `layers_create_filter`. UI em `tab_layers.c`: linha "+ Add effect" abaixo de cada layer/máscara com lista expansível.
- Geometry mask (LAY-11): `geometry_mask_t {mode include/exclude; i32_array tiles; i32_array objects; i32_array polygons}` por layer; textura de máscara gerada por `make_mask_geometry` e multiplicada em `make_paint` e no compositor.

Aceite: projeto antigo abre com blends iguais; alterar blend só do canal Normal não afeta base color; flatten gera layer idêntico ao Merge Group e o grupo original continua editável; undo/redo para cada operação; export flattened gera arquivos; efeitos Levels/HSL/Sharpen/Compare/Color Selection aplicáveis em máscara e conteúdo; multi-seleção move/apaga/agrupa.

### P2 — Mesh maps e baking (BAKE-01 … BAKE-10, VIEW-10)

Ordem: BAKE-01 `[bloqueante]` → BAKE-02 → BAKE-03 → BAKE-08/09 → BAKE-05 → BAKE-04 → BAKE-06 → BAKE-07 → VIEW-10 → BAKE-10.

Pontos técnicos:
- `mesh_map_t {slot (enum 9 tipos); gpu_texture_t *tex; bake_settings_t settings; bool dirty; char *file}` por texture set (enquanto PRJ-05 não existir, um set = projeto inteiro + por atlas).
- `util_bake.c`: `bake_job_t` com fila, progresso, cancelamento e callback; extraído de `bake_texture_node.c` (que passa a chamar o job). Resultado grava no slot e opcionalmente no Textures tab. API em `minic_api_list.h` (`bake_run(slot, settings)`).
- Nó `Mesh Map` (`mesh_map_node.c`): combo do slot; `*_value` amostra a textura do slot ativo do set. Todos os generators da P3 usam este nó.
- BAKE-03: raycast high-poly com cage: para cada texel, origem = posição do cage (low-poly deslocado por `cage_offset` global + offset pintado da BAKE-07), direção = normal (ou skew da BAKE-06), limites frontal/rear; acelerar com BVH já existente do path tracer (`render_path_raytrace_bake.c`) quando RT disponível, com fallback CPU em `util_raycast.c` por tiles e threads (`iron_thread.h`). Match by name: parear objetos `nome_low` ↔ `nome_high`. AA: bake em 2x/4x/8x da resolução e downsample em `dilate_pass`.
- BAKE-05: parâmetros de baker alterados marcam `dirty_rect` em UV; `auto_rebake` dispara job com `rect`; "Quick rebake" rebakeia o slot visível.
- BAKE-06: skew map RGBA por set; modo "Bake" do workspace com ferramentas brush/eraser/polygon fill escrevendo no skew map (reusa `render_path_paint` com alvo trocado); preview shader colore por desvio; "Edge protection" zera skew a `n` texels de arestas duras (precisa de `mesh_hard_edges`, calcule em `util_geom.c`); "base normal: mesh / per-triangle".
- BAKE-07: offset map float por set, ferramenta de pintura com blend add/sub e slider HDR; visualização do cage como mesh extrudido translúcido via `render_path_base`.

Aceite: bake de todos os slots num mesh de teste com high-poly e cage; auto rebake atualiza só a região alterada; skew pintado corrige um cilindro chanfrado (caso clássico); matriz BAKE-* em ✅ exceto onde marcado.

### P3 — Filtros, generators e biblioteca (LIB-01 … LIB-08, LAY-12, LAY-13)

Ordem: nós nativos de filtro → nós nativos de generator → LAY-12/LAY-13 (formatos de smart mask/material) → autoria de conteúdo (grunges, alphas, smart masks, smart materials) → LIB-07/08.

Pontos técnicos:
- Nós nativos em `paint/sources/nodes_material/`. Normal→height: compute shader de integração (Poisson por Jacobi, N iterações) ou CPU em `util_texture.c` com cache por hash do input. Kuwahara anisotrópico e bevel smooth: multi-sample no fragment shader (custo alto: limitar raio e usar `sample_lod`).
- Smart mask = `.arm` com `layer_datas` da subárvore (máscara + efeitos); smart material = `.arm` com grupo completo. Reaproveitar `export_arm.c` (`export_arm_run_material` já serializa materiais; adicionar `export_arm_run_layers(subtree)`) e `import_arm.c` (`import_arm_run_layers(subtree, target)`). Drop no `tab_layers.c`: criar máscara preta se não houver, merge de efeitos; ctrl = overwrite.
- Conteúdo: `paint/assets/library/{smart_materials,smart_masks,filters,generators,grunges,alphas}/`. Registrar em `paint/project.c` com `add_assets`. Todo conteúdo original ou CC0 com arquivo de licença.

Aceite: ≥ 20 filtros, ≥ 16 generators, ≥ 40 grunges, ≥ 60 alphas, ≥ 40 smart masks, ≥ 60 smart materials; arrastar smart material em um mesh com mesh maps bakeados produz resultado plausível (edge wear nas bordas, dirt nas cavidades).

### P4 — Pincel, ferramentas e paths (TOOL-01 … TOOL-28)

Ordem: TOOL-01 flow `[bloqueante]` → TOOL-02/03/04/05/08/28 → TOOL-06/07 simetria → TOOL-10 polígonos `[bloqueante para 11/12/13]` → TOOL-11/12/13 → TOOL-21/23 paths → TOOL-22 ribbon → TOOL-09 tool presets → TOOL-15.

Pontos técnicos:
- Flow: hoje a opacidade do stroke é aplicada por dab contra o `texpaint_undo` (verificar em `render_path_paint.c` se há acumulação por stroke). Implementar buffer `texpaint_stroke_alpha` (R8) zerado no início do stroke; cada dab escreve `max(alpha, flow)`; a composição final usa `min(stroke_alpha, opacity)`.
- Polígonos: no import, gerar atributo por vértice `poly_id` (u32) e tabela `poly_adjacency` (arestas compartilhadas) em `mesh_data`; salvar no `.arm` (opcional). Loop: a partir do polígono e da aresta clicada, andar pelos quads opostos até fechar ou encontrar não-quad; partial loop: parar em N; patch: flood fill limitado por arestas duras ou seleção; marquee: reusar `TOOL_TYPE_SELECT`; "visible only": testar com gbuffer de profundidade.
- Ribbon: amostragem do path em `util_path_tool.c` → strip de triângulos em UV com coordenada `u` ao longo do comprimento (0–1, ou repetida) e `v` transversal; largura/opacidade por ponto interpoladas; render do strip no `render_path_paint` com o material; blend configurável onde a strip se sobrepõe (depth por ordem do path).
- Tool preset: struct com `tool`, snapshot dos campos `brush_*`/`fill_*`/`sym_*` de `context_t`, referências a brush/material/alpha; serializar com armpack.

Aceite: flow 10% com opacidade 100% satura em várias passadas e nunca passa de 100%; loop/patch selecionam no mesh de teste com quads; ribbon segue curva com largura variável; simetria radial 6x pinta 6 cópias; presets salvam e restauram.

### P5 — Texture sets, canais e projeto (PRJ-01 … PRJ-11, PRJ-16, VIEW-09)

Ordem: PRJ-05 `[bloqueante]` → PRJ-06 `[bloqueante]` → PRJ-16 → PRJ-07/08/10/11 → PRJ-01/02/04 → VIEW-09.

Pontos técnicos:
- `texture_set_t {name; objects[]; resolution; channels[]; mesh_maps[]; uv_tiles[]}`. Fonte: atlases + materiais do mesh + objetos. Layers passam a ter `texture_set_id` (hoje `object_mask`).
- Canais custom (PRJ-06): `channel_def_t {name; type color/grayscale/normal; bits; default}`. Layer ganha `gpu_texture_t **texpaint_user` (um RGBA por até 4 canais grayscale ou 1 por canal color). `make_paint.c`: color writes e `output[n]` adicionais (MRT; D3D12/Vulkan/Metal suportam 8 RTs). `parser_material.c`: sockets dinâmicos no Material Output por template. `layers_merge_layer`: loop pelos `texpaint_user`. Export: nomes de canal no preset. Memória: alocar só quando o canal existe no set.
- Templates (PRJ-16): JSON com canais, shader, export preset e normal format; OpenPBR como default quando a P7 entregar o shader; até lá, "PBR Metal/Rough".
- New Project (PRJ-01): mesh file + template + resolução + normal format + UV tiles + auto-unwrap (organic / hard surface da P8) + import cameras; Project Configuration (PRJ-02) com "reimport mesh" (reprojeção vem na P8).

Aceite: projeto com 3 materiais no mesh mostra 3 texture sets, cada um com resolução própria; canal custom "user0" pintável, visível no modo de viewport e exportado; projetos antigos abrem com um set único.

### P6 — Projeções de fill e warp (FILL-02 … FILL-07, TOOL-16)

Pontos técnicos:
- `uv_type_t` += `UV_TYPE_PLANAR`, `UV_TYPE_SPHERICAL`, `UV_TYPE_CYLINDRICAL`, `UV_TYPE_WARP`. `make_texcoord.c` gera as funções; `decal_mat` vira `projection_mat` genérica; gizmos em `render_gizmo.c`.
- Warp: grade `warp_grid_t {cols, rows, points[] (uv do plano de projeção), world_points[]}`. Shader: localizar a célula via busca na grade (grade regular no espaço de origem → amostra uma textura de deslocamento RG32 de `cols x rows` com interpolação bilinear, barato). Warp to Geometry: ao criar/mover pontos, raycast ao mesh e armazenar posição 3D; o deslocamento é calculado no espaço tangente local do triângulo atingido; opção para "manter deformação" ao mover.
- FILL-06: rotacionar normal map pelo jacobiano 2x2 da deformação.

Aceite: logo projetado em um capô curvo acompanha a curvatura sem fade nas bordas com Warp to Geometry ligado; spherical/cylindrical com gizmo.

### P7 — Viewport, pós-efeitos, OpenPBR (VIEW-01 … VIEW-08, LIB-10)

Pontos técnicos:
- Pipeline de pós: `render_path_base.c` ganha lista ordenada de passes habilitáveis; cada pass é um `.shader`; parâmetros em `config_t` (globais) e override por projeto; presets JSON (LIB-10). DOF: CoC por profundidade (gbufferD) + blur separável; glare: downsample + streaks direcionais; lens flare: ghosts invertidos + halo; aberração: deslocamento RGB radial; sharpen: unsharp; color correction: exposure/contrast/saturation/lift-gamma-gain/temperatura; tone mapping: Linear/Reinhard/Filmic/ACES/AgX com exposição.
- OpenPBR: parâmetros do Material Output por template (PRJ-06): base_weight/color/metalness/diffuse_roughness, specular_weight/color/roughness/anisotropy/ior, coat_weight/color/roughness/ior, fuzz_weight/color/roughness, emission_color/luminance, thin_film_weight/thickness/ior, transmission_weight/color, subsurface_weight/color/radius, geometry_opacity/normal/coat_normal. BRDF em `deferred_light.shader` (camadas: coat sobre base, fuzz sheen, thin film por aproximação) e no path tracer (`raytrace_brute_full`). Export preset `openpbr.json`.
- OIT: forward pass com weighted-blended OIT (dois RTs, resolve no compositor).
- Sombras: sol direcional opcional com shadow map 2048 PCF.

Aceite: 10 pós-efeitos ligáveis com presets; material OpenPBR com coat e fuzz visível no raster e no path tracer; duas superfícies translúcidas sobrepostas renderizam corretamente com OIT.

### P8 — Import/export e reprojeção (IO-01 … IO-13, PRJ-03, PRJ-09, PRJ-17, PRJ-18, BAKE-10)

Pontos técnicos:
- Tokens no nome do arquivo e no preset; `box_export.c` redesenhado: lista de sets com resolução/formato/bits por set, lista de outputs do template com preview de packing, padding, destino.
- Writers: TGA (RLE opcional), TIFF baseline (strips, 8/16/32), PSD (composite + layers 8-bit RGBA, útil para "Export flattened"), FBX ASCII 7.4 (Geometry, LayerElementNormal/UV/Material, Model, Material) em `export_fbx.c`.
- Reprojeção (PRJ-03): ao reimportar mesh, bake de position/normal world do mesh antigo por texel (já existe `make_bake_position_normal`), depois para cada layer: shader que, para cada texel do novo mesh, busca o texel mais próximo no mesh antigo por posição world (grade/hash em textura ou BVH) e copia. Opcional "tolerância".
- Auto-unwrap hard surface (12.1): `util_uv_unwrap.c` ganha modo com seams por ângulo de aresta (> 60°) + alinhamento ortográfico das ilhas (PCA das normais) + packing. Entra em New Project/Reimport.
- Send To: exporta com preset do alvo para pasta temp e dispara o bridge (http local já usado pelos live-links; ver `iron_net.h`).
- USD opcional: leitor USDA (texto) para meshes e materiais simples; USDC fora de escopo salvo biblioteca C permissiva.

Aceite: export com template usando todos os tokens gera nomes corretos por set/UDIM; tga/tiff/psd abrem no GIMP; fbx abre no Blender com UVs e materiais; reimportar mesh com UVs novas mantém a pintura no lugar.

### P9 — Assets, projeto e UX (PRJ-12 … PRJ-15, UI-02 … UI-04, UI-09, TOOL-27)

Pontos técnicos:
- `tab_assets.c`: fonte de dados unificada (`asset_entry_t {type; name; path; tags[]; thumb}`), filtros por tipo/tag, busca (reusar `ui_search.c`), escopos (projeto, biblioteca); thumbnails já existem para materiais/brushes/texturas.
- Properties panel: função única `ui_properties_draw()` chamada pelo header (modo compacto) e pela aba (modo completo com grupos colapsáveis via `ui_panel`).
- History panel, Log panel, autosave/backups no core, auto-update de recursos, copy/paste entre projetos.

Aceite: Assets mostra todas as categorias com busca e tags; Properties mostra ferramenta e layer; autosave gera backups rotativos; auto-update recarrega textura alterada em disco.

### P10 — Polimento, keymap Substance, documentação, performance

- `keymap_presets/substance.json`; revisão de todas as strings e traduções pt; `base/docs/features/*.md` completos; passagem de performance em 8K/16K (medir tempo de merge e de stroke antes/depois de P1/P5); bug bash guiado pela matriz; atualizar `docs/parity_matrix.md` final e calcular a porcentagem.

Aceite: ≥ 90% dos 129 itens em ✅ (≥ 117); CI verde nos três alvos; projetos de exemplo salvos abrindo em todas as fases.

### Transversal (em toda fase)

- API de scripting (`minic_api_list.h`) exposta para cada feature nova + exemplo em `paint/assets/plugins/`.
- Undo para cada operação.
- Serialização `.arm` com defaults.
- Strings `tr()` + locale en/pt.
- Documento em `base/docs/features/`.
- Roteiro de teste manual no PR.

---

## 6. Riscos e decisões já tomadas

| Risco | Mitigação |
|---|---|
| PRJ-06 (canais custom) muda o modelo de dados central (3 texturas por layer) | Fazer depois de P1–P4 para não bloquear valor; alocar texturas extras só quando o set tem canais custom; manter caminho antigo intacto quando não há canais extras; testes de round-trip `.arm` |
| Memória GPU em 16K com canais extras e mesh maps | Mesh maps compartilhados por set (não por layer); canais custom grayscale empacotados 4 por textura; respeitar `Undo Steps` |
| Ambiente cloud sem GPU | Tudo compila; validação visual delegada ao dono com roteiro; preferir lógica testável em CPU (geometria, parsers, writers) com `base/tests` |
| Compatibilidade de projetos antigos | Toda leitura com default; `import_legacy.c` como referência |
| Performance do compositor com blend por canal e effects stack | Cache de composição por layer (já existe `layer_preview`/`texpaint_live`); só recompor layers sujos |
| Licenças de conteúdo | Apenas original ou CC0, com `paint/assets/licenses/library.md` |
| Warp to Geometry e Ribbon são algoritmicamente novos | Implementar primeiro atrás de `experimental`; prototipar em 2D view |

---

## 7. Scoreboard inicial (copiar para `docs/parity_matrix.md`)

| Área | Itens | ✅ | 🟡 | ❌ |
|---|---|---|---|---|
| Projeto/texture sets/assets | 18 | 0 | 9 | 9 |
| Layer stack | 18 | 1 | 8 | 9 |
| Ferramentas/pincel | 28 | 8 | 14 | 6 |
| Projeções | 7 | 2 | 1 | 4 |
| Procedural/biblioteca | 10 | 1 | 5 | 4 |
| Baking | 12 | 2 | 3 | 7 |
| Viewport/render | 12 | 2 | 5 | 5 |
| Import/export | 13 | 1 | 10 | 2 |
| UI/scripting | 11 | 3 | 6 | 2 |
| **Total em escopo** | **129** | **20 (16%)** | **61** | **48** |

Meta final: ≥ 117 itens ✅ (90%).

---

## 8. Prompt curto para iniciar a sessão

> Leia `PLANO_PARIDADE_SUBSTANCE_PAINTER_12.md` na raiz do repositório inteiro antes de começar. Siga a seção 0 à risca. Comece pela fase P0 e abra um PR por fase. A cada PR, atualize `docs/parity_matrix.md` e liste o que precisa de teste manual no Windows. Não pule fases e não faça duas fases no mesmo PR.
