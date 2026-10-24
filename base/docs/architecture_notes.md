# Notas de arquitetura (P0)

Leitura do código feita na fase P0 do plano de paridade, antes de mexer em camadas (P1), baking (P2) e pincel (P4). Cada afirmação aponta para arquivo e linha no branch da P0 (as linhas andam com o tempo; procure pelo nome da função). Caminhos relativos à raiz do repositório.

Resumo do que muda os planos das fases seguintes:

- O canal alfa de `texpaint_nor` guarda `(3 × id do material + flag) / 255`, com flag 0 normal, 1 emissão, 2 subsurface. Não existe intensidade de emissão nem de subsurface por texel, só a flag. O export grava emissão e subsurface como máscaras binárias.
- A composição de camadas está implementada **quatro vezes**: shader do viewport (`make_mesh.c`), merge (`layers_merge_layer`), flatten (`layers_flatten`) e export (`export_texture_run_layers`). Hoje só a cor base usa o `blending` da camada; normal, oclusão, roughness e metallic são sempre "mix", e a altura é "replace" ou "add". O blend/opacidade por canal da P1 precisa mudar os quatro.
- O stroke já tem um buffer de acúmulo por stroke (`texpaint_blend0/1`, R8): cada passada faz `max` com o que já foi pintado e ainda soma 10% do restante. Dentro de um stroke a força tende a 1 mesmo com opacidade < 1. O pincel é uma cápsula entre a posição anterior e a atual, não uma sequência de dabs: não há spacing.
- O `.arm` da versão corrente é decodificado **por posição** (`armpack_decode` ignora os nomes das chaves). Campo novo em `layer_data_t` exige subir `manifest_version_project` e migrar arquivos antigos pelo caminho por chave de `import_legacy.c`. A regra 7 do plano ("campo opcional com default na leitura") só vale nesse caminho.
- Bug encontrado e corrigido na P0: a opacidade de grupos não era restaurada ao abrir um projeto (`paint/sources/io/import_arm.c`).

---

## 1. Codificação dos canais de uma camada

Cada camada de pintura (`slot_layer_t`, `paint/sources/types.h:47`) tem três render targets do mesmo tamanho:

| Textura | R | G | B | A |
|---|---|---|---|---|
| `texpaint` | base color R | base color G | base color B | opacidade da camada no texel (cobertura) |
| `texpaint_nor` | normal tangente X (0..1) | normal tangente Y | normal tangente Z | `(3·matid + flag) / 255` |
| `texpaint_pack` | occlusion | roughness | metallic | height |

Máscaras usam só `texpaint` (o valor da máscara está em R) e não têm `texpaint_nor` (`slot_layer_is_layer` testa `texpaint != NULL && texpaint_nor != NULL`, `paint/sources/slots/slot_layer.c:730`). Grupos não têm texturas. Um *filtro* é uma camada cujo `parent` é outra camada (`slot_layer_is_filter`, `slot_layer.c:754`).

Formato: os alvos de pintura seguem `base_bits` (8 → RGBA32, 16 → RGBA64, 32 → RGBA128), mais um R8 para o acúmulo do stroke (`make_paint_color_attachments`, `paint/sources/render/make_paint.c:14`).

### 1.1 Escrita (shader de pintura)

Gerado por `make_paint_run_context` (`make_paint.c:104`):

- `make_paint.c:542` `f32 matid = g_context->material->id / 255.0;` e `make_paint.c:546` escreve `float matid = <matid * 3.0>`. Ou seja, o valor base é `3·id/255`.
- `make_paint.c:549-561`: se o material pinta emissão (`paint_emis`) e o shader de emissão é não nulo, `matid += 1.0 / 255.0` quando `emis > 0`; senão, se pinta subsurface, `matid += 2.0 / 255.0` quando `subs > 0`. Emissão tem prioridade; as duas flags nunca coexistem.
- `make_paint.c:602`: `output[1] = float4(lerp(sample_nor_undo.rgb, nortan, str), matid);` — o alfa é **substituído** (não interpolado) em todo texel tocado pelo stroke.
- `make_paint.c:603`: `output[2] = lerp(sample_pack_undo, float4(occlusion, roughness, metallic, height), str);`
- `make_paint.c:606-645`: as máscaras de escrita de cor por canal vêm dos flags do **material** (`paint_base`, `paint_opac`, `paint_nor`, `paint_occ`, `paint_rough`, `paint_met`, `paint_height`). Em máscara só o R de `output[0]` é escrito.
- Com picker de máscara de material ligado, o id escrito é o do material sob o cursor (`make_paint.c:543-545`).

Consequências:

- `id` cabe em `floor(255/3) = 85` materiais distintos por projeto antes de estourar o canal de 8 bits. Em 16/32 bits o valor é float e o limite é o mesmo, porque as leituras convertem com `* 255`.
- Não há intensidade de emissão: ao escrever o G-buffer, a cor base de texels emissivos é multiplicada por 10 (`paint/sources/render/make_mesh.c:561-563`) e o `deferred_light.shader` usa essa **cor base como cor emitida** quando a flag é 1 (`paint/shaders/deferred_light.shader:269-274`). O caminho forward (`make_mesh.c:517-558`) não trata emissão.

### 1.2 Leitura

- Viewport (`make_mesh_run`, `make_mesh.c:92`): quando `make_material_emis_used`, cada camada com `texpaint_opac > 0` sobrescreve `matid` com o texel bruto de `texpaint_nor.a` lido com `texpaint_nor<id>[uint2(...)]` (sem filtragem, `make_mesh.c:387-395`). Ganha a camada visível mais alta que cobre o texel.
- G-buffer: `gbuffer0 = (n.xy octaédrico, roughness, pack_f32_i16(metallic, matid % 3))` (`make_mesh.c:661`; `pack_f32_i16` em `paint/sources/globals.h:496-500`). `gbuffer1 = (basecol, occlusion)` no modo Lit deferred (`make_mesh.c:564`). `gbuffer2` guarda o texcoord dividido em parte grossa e resíduo para não perder precisão em half float (`make_mesh.c:665-666`).
- `deferred_light.shader:225-228` desempacota `matid` e `metallic` de `g0.a`; só a flag 1 (emissão) é usada no deferred. A flag 2 (subsurface) aparece nos modos de viewport `VIEWPORT_MODE_SUBSURFACE` (`make_mesh.c:593-596`) e no path tracer.
- Modos de viewport Emission/Subsurface/Material ID: `make_mesh.c:588-617`.
- Picker: `paint/sources/render/render_path_paint.c:483-498` lê o byte e recupera `matid = (id - id % 3) / 3`.
- Export (`paint/sources/io/export_texture.c`): `emis` e `subs` chamam `export_texture_extract_channel(pixpaint_nor, 3, ..., 3, 1|2, ...)` (`export_texture.c:521-526`), que calcula `id = floor(v·255 + 0.5)` e escreve 1 se `id % 3 == flag`, senão 0 (`export_texture.c:191-202`). O mapa exportado é uma máscara binária.

### 1.3 Armadilhas para P1, P5 e P7

- **O merge interpola o alfa de `texpaint_nor`.** Com `blending 101` o shader faz `lerp(cola, col1, str)` nos quatro canais (`paint/shaders/layer_merge.shader:57-60`). Merge down, merge group e flatten com opacidade parcial produzem `matid` fracionário, que não decodifica para nenhum material. Com 102 (normal "whiteout") o alfa vira `max(col1.a, cola.a)` (`layer_merge.shader:61-69`). Ao separar canais na P1, trate o id/flags como dado discreto (escolha por `str > 0.5` ou pelo maior), nunca interpolado.
- Emissão com intensidade, cor própria ou canais OpenPBR (P5/P7) não cabem nesse alfa. Precisam de textura extra (`texpaint_user[]` do PRJ-06) e a flag atual deve continuar sendo lida para projetos antigos.
- Mudar o significado de `texpaint_nor.a` afeta viewport, deferred, picker, export, path tracer e o filtro por material (`make_discard_material_id`).

---

## 2. Composição de camadas

### 2.1 As quatro implementações

| Caminho | Onde | Quando roda | Saída |
|---|---|---|---|
| A. Viewport ao vivo | `make_mesh_run` (`paint/sources/render/make_mesh.c:92`), regenerado por `make_material_parse_mesh_material` (`paint/sources/render/make_material.c:98`) | toda mudança de camada, visibilidade, opacidade ou blend | shader de G-buffer, sem textura intermediária |
| B. Merge | `layers_merge_layer` (`paint/sources/util/util_layer.c:962`) | Merge Down (`util_layer.c:899`), Merge Group (`util_layer.c:926`), Apply Mask (`util_layer.c:886`), combinação de máscaras no export | escreve dentro de `l0` |
| C. Flatten | `layers_flatten` (`util_layer.c:1196`) | visão 2D (`paint/sources/ui/ui_view2d.c:617,626`), path tracer e bake RT (`render_path_raytrace.c:466,485`, `render_path_raytrace_bake.c:119`), snapshot das camadas visíveis do Clone (`render_path_paint.c:280`) | `layers_expa/expb/expc` |
| D. Export | `export_texture_run_layers` (`paint/sources/io/export_texture.c:238`) | export de texturas | `layers_expa/expb/expc` lidos para CPU |

B, C e D usam o mesmo shader, `paint/shaders/layer_merge.shader`, via os pipelines de `paint/sources/pipes.c:19-27` (`pipes_merge` escreve RGBA; `pipes_merge_r/g/b` escrevem um canal só, para o `texpaint_pack`). C e D são cópias quase idênticas uma da outra; D ainda trata UDIM e objeto.

### 2.2 Matemática por canal (igual nos quatro caminhos)

Força da camada no texel: `str = texpaint.a × opacidade × máscara`.

- No shader de merge: `float str = col0.a * constants.opac; str *= maskr;` (`layer_merge.shader:54-55`).
- No viewport: `texpaint_opac = texpaint_sample.a` (`make_mesh.c:322`), multiplicado pelas máscaras (`make_mesh.c:335-365`) e por `slot_layer_get_opacity(l)` (`make_mesh.c:367-370`).

`slot_layer_get_opacity` (`paint/sources/slots/slot_layer.c:714-720`) devolve `mask_opacity × parent->mask_opacity` quando a camada está num grupo, então a opacidade do grupo multiplica a dos filhos.

| Canal | Operação | Viewport | Merge/flatten/export |
|---|---|---|---|
| Base color (RGB) | `make_material_blend_mode(blending)`, 18 modos | `make_mesh.c:372-380`; a primeira camada do projeto é só `rgb * opac` (`make_mesh.c:373-375`) | `blending` 0..17 em `layer_merge.shader:89+`; com uma camada só usa 0 (`util_layer.c:1271`, `export_texture.c:358`) |
| Opacidade (A de `texpaint`) | no merge `max(col0.a * maskr, cola.a)` | não composta, `opacity = 1.0` | mesmos ramos 0..17 |
| Normal | `lerp` ou whiteout se `paint_nor_blend` | `make_mesh.c:397-410` | 101 ou 102 |
| Occlusion, roughness, metallic | sempre `lerp` (mix) | `make_mesh.c:418-426` | 101 com `pipes_merge_r/g/b` |
| Height | `=` ou `+=` se `paint_height_blend` | `make_mesh.c:427-445`; vira normal por diferenças finitas em `make_mesh.c:480-489` | 103 só quando occ, rough, met e height estão todos ligados (`util_layer.c:1059-1062`) |
| matid/flags | último que cobre | `make_mesh.c:387-395` | interpolado, veja 1.3 |

Os 18 modos estão escritos duas vezes: `make_material_blend_mode` (`make_material.c:408-491`) para shaders gerados e os ramos 0..17 de `layer_merge.shader`. Os dois precisam ficar iguais.

Divergências que existem hoje (não são da P0, mas a P1 vai esbarrar nelas):

- Com `paint_height` ligado mas algum de occ/rough/met desligado, merge, flatten e export **não compõem a altura** (o `else` em `util_layer.c:1063-1073` só trata R, G e B). O viewport compõe.
- Os modos Lighten, Screen, Soft Light, Linear Light e Divide não usam `lerp(cola, ..., str)` e se comportam de outro jeito com opacidade parcial.
- `layers_merge_layer` mistura a normal só se `l0->texpaint_nor != NULL` e o pack só se `l0->texpaint_pack != NULL` (`util_layer.c:1027,1051`), então mesclar numa máscara mexe só no R.

### 2.3 Grupos, máscaras e filtros

- Grupos têm um nível: a UI desabilita "New Group" numa camada que já está num grupo (`paint/sources/ui/tab_layers.c:1091-1093`) e a API de script recusa (`paint/sources/minic_impl.c:746`). O grupo não tem textura nem `blending` efetivo: só contribui com a opacidade (via `slot_layer_get_opacity`), a visibilidade (`slot_layer_is_visible`, `slot_layer.c:562`) e as máscaras.
- `slot_layer_get_masks(l, true)` (`slot_layer.c:600`) junta as máscaras da camada e as do grupo pai. No viewport cada máscara entra como `mask *= lerp(1, amostra, opacidade_da_máscara)` (`make_mesh.c:358-360`). No merge as máscaras primeiro são combinadas numa só (`util_layer.c:978-995`, `util_layer.c:1226-1237`, `export_texture.c:322-333`).
- Merge Group faz merge down sucessivo dos filhos, aplica as máscaras do grupo no filho que sobra, renomeia esse filho com o nome do grupo e apaga o grupo (`util_layer.c:926-960`). Se o filho era fill layer, ele vira paint layer.
- Filtros: a camada com filtro visível é pulada nos quatro caminhos (`slot_layer_has_visible_filter`, `slot_layer.c:701`); quem entra é o filtro, que é uma fill layer cujo material lê o pai e usa o `blending` do pai (`slot_layer_get_blending`, `slot_layer.c:726-728`).

### 2.4 Limites do viewport

- `GPU_MAX_TEXTURES` é 16 (`base/sources/iron_gpu.h:21`). Cada camada consome 3 texturas mais 1 por máscara; quando passa de `GPU_MAX_TEXTURES - 3`, o viewport abre outro passe de G-buffer que lê o resultado do passe anterior (`make_mesh.c:277-293`, leitura em `make_mesh.c:189-206`). Texturas extras por camada (P5) aumentam o número de passes.
- O shader inteiro é regenerado e recompilado a cada mudança de blend, opacidade, visibilidade ou ordem (`make_material_parse_mesh_material`). Opacidade e blend entram como constantes no código gerado, não como uniforms.

### 2.5 Lista de pontos a mudar para blend/opacidade por canal (LAY-01)

Leitores de `slot_layer_t.blending`:

- `paint/sources/slots/slot_layer.c:77,131` (defaults), `:404` (duplicate), `:727` (`slot_layer_get_blending`).
- `paint/sources/render/make_mesh.c:377` (viewport, só base).
- `paint/sources/util/util_layer.c:1020` (merge), `:1271` (flatten).
- `paint/sources/io/export_texture.c:358` (export).
- `paint/sources/ui/tab_layers.c:338-345` (combo).
- `paint/sources/history.c:344,398,478-479,794-795,937` (undo/redo e `history_push`).
- `paint/sources/io/export_arm.c:249`, `paint/sources/io/import_arm.c:1055`, `paint/sources/io/import_legacy.c:198`, `paint/sources/util/util_encode.c:400` (formato `.arm`).

Leitores de `mask_opacity` / `slot_layer_get_opacity`: `make_mesh.c:358,367-369,649`; `util_layer.c:988,1002,1018,1041,1060-1071,1269,1292,1309-1320`; `export_texture.c:356,378,395-404`; `tab_layers.c:759-766`; `history.c:470-475,786-791` e `history_push`; `.arm` como acima.

Os passes do `texpaint_pack` passam **uma** opacidade para R, G e B juntos (`layers_commands_merge_pack`, `util_layer.c:263-278`). Para opacidade por canal o mais simples é passar um `float4` de opacidades (e blends) ao `layer_merge.shader` em vez de três pipelines.

---

## 3. Como um stroke é executado

### 3.1 Linha do tempo

1. **Botão pressionado.** `util_brush_update` (`paint/sources/util/util_brush.c:5`) detecta o início (`brush_time == 0`) e marca `history_push_undo = true` (`util_brush.c:59`). A cada frame com o botão abaixado soma `brush_time` e chama `brush_output_node_run` (`util_brush.c:74-75`).
2. **Posição.** O nó Input calcula a posição com lazy mouse (`paint/sources/nodes_brush/input_node.c:73-150`) e o nó Brush Output grava `g_context->paint_vec` (`paint/sources/nodes_brush/brush_output_node.c:24`). Na primeira vez `last_paint_vec = paint_vec` (`brush_output_node.c:113-117`). A posição repetida conta em `painted` e só suja o frame quando `painted <= 1` (`brush_output_node.c:166-196`): segurar o mouse parado não continua pintando. O resultado é `g_context->pdirty = 1`.
3. **Início do frame de pintura.** `render_path_paint_begin` (`paint/sources/render/render_path_paint.c:967`): se `history_push_undo`, chama `history_paint` (`render_path_paint.c:988-989`), que copia as três texturas da camada para o slot de undo `texpaint_undo<i>` (`history_copy_to_undo`, `paint/sources/history.c:580-619`) e empilha `HISTORY_ACTION_PAINT` (`history.c:954-961`). Essa cópia é a **base de todo o stroke**: o shader sempre mistura sobre o estado de antes do stroke.
4. **Desenho.** `render_path_paint_draw` (`render_path_paint.c:1060`) chama a simetria (`render_path_paint_commands_symmetry`, `render_path_paint.c:708`) e depois `render_path_paint_commands_paint` (`render_path_paint.c:348`):
   - copia o acúmulo do stroke `texpaint_blend0` → `texpaint_blend1` (`render_path_paint.c:585-587`);
   - liga a camada como alvo MRT: `texpaint<id>`, `texpaint_nor<id>`, `texpaint_pack<id>`, `texpaint_blend0` (`render_path_paint_additional`, `render_path_paint.c:3-10`, uso em `:611-616`);
   - liga profundidade (`gbufferD`), `gbuffer0` quando precisa da normal, e `texpaint_blend1` como `paintmask` (`render_path_paint.c:618-622`);
   - desenha a malha no espaço de UV (`tpos` a partir de `tex`, `make_paint.c:213-215`) e no fim dilata as bordas (`render_path_paint_dilate`, `render_path_paint.c:1316`).
5. **Fim do frame.** `render_path_paint_end` (`render_path_paint.c:1005`) guarda `last_paint_vec = paint_vec` (o próximo segmento começa onde este terminou) e atualiza camadas ligadas.
6. **Botão solto.** `util_brush.c:79-85` zera `brush_time` e marca `brush_blend_dirty`; no frame seguinte `texpaint_blend0` e `texpaint_blend1` são limpos (`render_path_paint.c:1141-1146`).

### 3.2 Forma e força por frame

O pincel é uma **cápsula** entre a posição anterior e a atual (`paint/sources/render/make_brush.c:68-82`): em espaço de mundo `dist = length(pa - ba * h)`, ou em espaço de tela quando "camera align" está ligado. Fora do raio, `discard`. Não existem dabs discretos nem spacing: um frame pinta o segmento inteiro.

Com `dist`:

```
t   = dist / brush_radius
t2  = clamp((t - hardness) / max(1 - hardness, 0.001), 0, 1)
str = (1 - t2² (3 - 2·t2)) · opacity                    // make_paint.c:515-517
```

`opacity` é a opacidade do material vezes `brush_opacity` (`make_paint.c:390-401`; o uniform `_brush_opacity` já inclui pressão em `paint/sources/uniforms.c:66-72`), vezes máscara, stencil ou texto quando existem (`make_paint.c:416-507`). Texels com `opacity <= brush_alpha_discard` são descartados (`make_paint.c:509`).

### 3.3 Acúmulo dentro do stroke (resposta para TOOL-01, flow)

Existe acúmulo por stroke, em `texpaint_blend0/1` (R8, criados em `render_path_paint.c:44-52`):

```
sample_mask = paintmask (o acumulado até o frame anterior)
base_str    = max(str, sample_mask)
str         = base_str + str · (1 - base_str) · 0.1    // make_paint.c:535-536
output[3]   = str                                      // grava o novo acumulado
```

e a mistura final é sempre contra o undo: `out = (basecol·str + undo.rgb·undo.a·(1 - str)) / out_a` para Mix (`make_paint.c:593-596`) ou `make_material_blend_mode(..., "sample_undo.rgb", "basecol", "str")` nos outros modos (`make_paint.c:597-601`).

Respondendo à pergunta da P4: **sim, passar de novo no mesmo ponto dentro do stroke passa da opacidade.** Com `opacity = 0.5`, a força vai 0.5 → 0.525 → 0.549 → … e tende a 1. Como o `max` vem antes da soma, o efeito é pequeno por passada mas não tem teto. Para o flow da P4 o buffer já existe: basta trocar a fórmula por `acumulado = acumulado + flow·(1 - acumulado)` e limitar a mistura por `min(acumulado, opacity)`. Smudge/Blur usam outra regra (`str *= 0.5`, `make_paint.c:531-533`).

### 3.4 Outros detalhes

- Simetria: `render_path_paint_commands_symmetry` redesenha o mesmo stroke com a escala do objeto negada em cada combinação de eixos ligados, até 7 passadas extras (`render_path_paint.c:708-753`). O plano de simetria é sempre a origem do objeto. Com simetria ligada o depth reject é desligado (`make_paint.c:286-289`).
- Profundidade: `if (sp.z > gbufferD - 0.00008) discard` quando não há X-ray (`make_paint.c:279-293`). Rejeição por ângulo e plano em `make_brush.c` quando `brush_angle_reject` ou X-ray.
- Tamanho: `_brush_radius = radius · brush_nodes_radius / 15 · 2`, com pressão (`uniforms.c:15-30`).
- Máscara de máscara: pintando numa máscara de grupo, os alvos de normal e pack são os do primeiro filho do grupo (`render_path_paint.c:602-613`), e só o R é escrito.
- Bake usa o mesmo caminho com `TOOL_TYPE_BAKE` e saídas próprias (`make_paint.c:647-649`, `render_path_paint.c:1077-1135`).
- Visão 2D: `render_path_paint_set_plane_mesh` troca a malha por um plano (`render_path_paint.c:995-997`, `:1153`).

### 3.5 Armadilhas para a P4

- O undo de um stroke é uma cópia completa das três texturas por passo (`g_config->undo_steps` slots). Buffers novos por stroke custam memória proporcional à resolução.
- `texpaint_blend0/1` são compartilhados por todas as camadas; mudar de camada no meio do stroke não é suportado.
- Spacing e jitter por dab exigem pintar vários carimbos por frame ao longo do segmento, o que hoje não existe.

---

## 4. Estender `slot_layer_t`: undo e formato `.arm`

### 4.1 Como o `.arm` é lido de verdade

- `export_arm_encode_project` monta um `layer_data_t` por camada (`paint/sources/io/export_arm.c:234-271`) e `util_encode_project` escreve as chaves **numa ordem fixa** (`paint/sources/util/util_encode.c:372-447`, `armpack_encode_map(37)` na linha 376).
- Na leitura, se a versão do arquivo é a atual (`manifest_version_project = "21"`, `paint/sources/globals.h:10`), `import_arm_run_project` chama `armpack_decode(b)` (`paint/sources/io/import_arm.c:724-731`). Esse decodificador **não olha os nomes das chaves**: grava os valores em sequência num bloco de memória com o layout do struct C (`read_store_map`, `base/sources/iron_armpack.c:215-230`). A ordem e os tipos de `layer_data_t` (`paint/sources/types.h:690-727`) precisam ser exatamente os do encoder.
- Se a versão é outra, `import_arm_from_old` (`paint/sources/io/import_legacy.c:524-539`) decodifica para um mapa por chave (`armpack_decode_to_map`) e passa pela cadeia `import_arm_from_version_N` → `import_arm_from_map_to_arm` (`import_legacy.c:104-323`), que lê cada campo por nome. Chave ausente vira 0 (`armpack_map_get_i32/f32`, `iron_armpack.c:901-917`).

Logo, para um campo novo de camada:

1. Struct de runtime: campo em `slot_layer_t` (`types.h:47-87`), default em `slot_layer_create` (`paint/sources/slots/slot_layer.c:77` é o exemplo do `blending`), cópia em `slot_layer_duplicate` (`slot_layer.c:404`).
2. Formato: acrescentar o campo **no fim** de `layer_data_t` e **no fim** do bloco de `util_encode.c`, atualizando a contagem de `armpack_encode_map(37)`. Preencher em `export_arm.c:234-271`, aplicar em `import_arm.c:975-1075`.
3. Versão: subir `manifest_version_project` para `"22"`, acrescentar `import_arm_from_version_21` (que só chama a versão nova) ao array de `import_arm_from_old` e ler o campo em `import_arm_from_map_to_arm` com default explícito quando a chave falta (0 raramente é o default certo: opacidade 1.0, por exemplo).
4. Undo: campo novo em `history_step_t` (`types.h:539-565`), valor gravado em `history_push` (`paint/sources/history.c:895-945`) e troca no undo/redo (`history.c:470-482` e `:786-798` são o modelo de opacidade e blending; propriedades simples passam por `history_swap_layer_props`, `history.c:119-174`). Nova ação em `history_action_t` (`paint/sources/enums.h:543-582`) e função `history_<ação>` que chama `history_push`.
5. Script: `MINIC_*` em `MINIC_STRUCT(slot_layer_t)` (`paint/sources/minic_api.c:671-686`).
6. Teste: o fixture `base/tests/paint_headless/fixtures/p0_project.arm` (versão 21) passa a usar o caminho legado depois do passo 3; o smoke test confere que ele continua abrindo igual.

Outros pontos que leem `layer_data_t`: timeline (`timeline_layer_keyframe_data_t`, `types.h:729+`) e `import_arm.c:1163-1171` (materiais de fill e path).

### 4.2 Ações de undo existentes

`history_action_t` (`enums.h:543-582`): PAINT, EDIT_NODES, NEW_LAYER, NEW_BLACK_MASK, NEW_WHITE_MASK, NEW_FILL_MASK, NEW_GROUP, DELETE_LAYER, CLEAR_LAYER, DUPLICATE_LAYER, ORDER_LAYERS, MERGE_LAYERS, APPLY_MASK, INVERT_MASK, APPLY_FILTER, TO_FILL_LAYER, TO_FILL_MASK, TO_PAINT_LAYER, TO_PAINT_MASK, LAYER_OPACITY, LAYER_BLENDING, DELETE_NODE_GROUP, NEW_MATERIAL, DELETE_MATERIAL, DUPLICATE_MATERIAL, LAYER_VISIBLE, LAYER_NAME, LAYER_OBJECT, LAYER_SCALE, LAYER_ANGLE, LAYER_UV_TYPE, NEW_BRUSH, DELETE_BRUSH, DUPLICATE_BRUSH, NEW_SWATCH, DELETE_SWATCH, DUPLICATE_SWATCH, REPLACE_SWATCHES, EDIT_SWATCH, OBJECT_TRANSFORM.

O passo guarda a camada por **índice** (`step->layer`) e copia texturas para os slots `texpaint_undo<i>` em anel de `g_config->undo_steps` (`history.c:580-619`). Operações que mudam a ordem das camadas precisam guardar o bastante para refazer a ordem.

### 4.3 Bug corrigido na P0

`import_arm.c` restaurava `mask_opacity` só dentro de `if (!is_group)`, mas a UI mostra o slider de opacidade também para grupos (`paint/sources/ui/tab_layers.c:759-766`) e o export gravava o valor. Um grupo com 80% abria com 100%. Agora `mask_opacity` é restaurado junto com `visible` para todos os tipos. O smoke test cobre esse caso (`p0_group`).

---

## 5. Como validar mudanças nessas áreas

- `base/tests/paint_headless/run.sh` (Linux, também no cloud com llvmpipe) e `run.bat` (Windows): abre o fixture, confere estrutura e médias dos canais exportados, salva e reabre.
- `paint/assets/plugins/dev/test.c`: teste upstream maior, roda com `--script` (veja `base/docs/building_cloud.md`).
- O que depende de GPU de verdade (aparência no viewport, path tracer, bakes RT) continua no roteiro manual de cada PR.
