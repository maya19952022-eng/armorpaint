# Máscara geométrica

IDs da matriz: LAY-11 · Fase: P1

Restringe onde uma camada ou um grupo vale pela geometria: objetos do projeto, tiles UDIM e faces (polígonos) escolhidas no viewport. Em **Include** a camada só aparece na geometria selecionada; em **Exclude** aparece em toda a geometria menos nela. Fora da máscara a camada não contribui no viewport, no merge, no flatten e no export de texturas, e pintar ou preencher ali não muda nada.

## Onde fica

Aba Layers → menu de contexto da camada ou do grupo (clique direito) → seção **Geometry Mask**, no fim do menu. Máscaras e filtros seguem a máscara geométrica da camada a que pertencem. Camadas de sculpt não mostram a seção.

As faces são escolhidas com a ferramenta **Select** no alvo **Faces** (barra do topo da ferramenta), clicando ou arrastando sobre a malha no viewport 3D. A seleção vale para a camada ou o grupo atual; numa máscara ou num filtro, para a camada dele. O botão **Select Faces** do menu da camada já abre a ferramenta assim.

## Controles

| Controle | O que faz |
|---|---|
| **Include** (incluir) / **Exclude** (excluir) | Include: a camada vale só na geometria selecionada. Exclude: vale em toda a geometria menos na selecionada |
| **Objects** (objetos) | Uma caixa por objeto do projeto, com os nomes da aba Meshes |
| **UV Tiles** (tiles UV) | Aparece quando há objetos com sufixo de tile UDIM (`.1001`, `.1002`..., como o import com split por UDIM cria). Uma caixa por tile |
| **Faces: n** | Quantas faces estão selecionadas. `(mesh changed)` aparece quando a malha de um objeto mudou de triangulação desde a seleção (veja Limitações) |
| **Select Faces** | Torna a camada atual e abre a ferramenta Select no alvo Faces |
| **Clear Faces** | Tira todas as faces da máscara; objetos e tiles ficam |
| **Clear Geometry Mask** | Desmarca tudo, inclusive as faces, e volta para Include: a camada volta a valer em toda a geometria |

Na barra da ferramenta **Select**:

| Controle | O que faz |
|---|---|
| **Region** / **Faces** | Region é a seleção retangular de sempre (Clear, Crop). Faces edita as faces da máscara geométrica |
| Nome da camada | Camada ou grupo cuja máscara o clique edita |
| **Select**: **Triangle**, **Polygon**, **Connected**, **UV Island**, **Object** | O que um clique pega: o triângulo, o polígono de onde ele veio, a parte conectada da malha, a ilha UV (do UV map da camada) ou o objeto inteiro |
| **Add** / **Remove** | Clicar acrescenta ou tira faces. Segurar Ctrl faz o contrário durante o clique |
| **Include** / **Exclude** | O modo da máscara, o mesmo do menu da camada |
| **Clear Faces** | Como no menu da camada |

As faces selecionadas aparecem no viewport com um tom da cor de destaque do tema enquanto a ferramenta está no alvo Faces. Arrastar seleciona tudo o que o mouse cruza; cada clique ou arrasto é um passo de undo.

- Sem nada marcado nem selecionado não há máscara, qualquer que seja o modo.
- Objetos e faces se somam: um objeto marcado vale como todas as faces dele. Os tiles restringem os dois (por exemplo, o objeto `corpo` só no tile 1002). Objetos sem sufixo de tile contam como tile 1001.
- Tirar faces de um objeto marcado desmarca o objeto e deixa as outras faces dele selecionadas. Tirar faces de uma máscara vazia em Include seleciona antes todos os objetos, para que o resto continue valendo.
- A máscara do grupo vale para as camadas dele, junto com a máscara de cada camada: a camada aparece onde as duas deixam.
- Pintar numa camada (ou numa máscara dela) só muda os texels dentro da máscara geométrica; o Bake também não limpa os texels de fora.

## Atalhos

Nenhum atalho novo. Ctrl+Z/Ctrl+Y desfazem e refazem cada mudança; no histórico o passo se chama "Geometry Mask". Com a ferramenta Select no alvo Faces, Ctrl durante o clique troca Add por Remove (e vice-versa) e Alt+clique continua girando a câmera. O undo fica bloqueado enquanto um arrasto de seleção está em andamento.

## Projeto e export

- O `.arm` guarda campos opcionais por camada: `geometry_mask_mode` (0 include, 1 exclude), `geometry_mask_tiles` (lista de tiles), `geometry_mask_objects` (nomes dos objetos) e, para as faces, `geometry_mask_face_objects` (um nome de objeto por entrada), `geometry_mask_face_counts` (número de triângulos do objeto quando as faces foram escolhidas) e `geometry_mask_faces` (os bits de cada entrada em sequência, `(n + 7) / 8` bytes cada; o triângulo `t` é o bit `t & 7` do byte `t >> 3`). Listas vazias são gravadas como null.
- Uma face é um triângulo do index array do objeto (os importadores triangulam), por isso a seleção é guardada por triângulo.
- Projetos da versão 21 ou anterior e projetos da versão 22 salvos antes da máscara geométrica abrem sem máscara e com o mesmo resultado; os salvos antes das faces abrem com objetos e tiles e sem faces. A versão do formato continua 22, e versões anteriores do app abrem projetos com faces ignorando os campos novos. Campos de faces inconsistentes são descartados com um aviso no console.
- Export de texturas, Merge Down, Merge Group, Flatten, visão 2D e path tracer aplicam a máscara. Merge Down e Merge Group gravam a máscara na opacidade das camadas antes de juntar: a camada resultante não tem máscara geométrica e o resultado fica igual.
- No export **Per Object**, uma camada cuja máscara deixa o objeto de fora não entra na textura dele.
- Os objetos e as faces são guardados pelo nome do objeto: renomear um objeto atualiza as máscaras, e um objeto apagado deixa de contar.
- Duplicar, copiar e colar, apagar e desfazer, agrupar e o merge levam as faces junto.

## Script

```c
slot_layer_t *l = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
object_t     *o = script_get_object("corpo");
script_layer_set_geometry_object(l, o, true);  // marca o objeto
script_layer_set_geometry_tile(l, 1002, true); // marca o tile 1002
script_layer_set_geometry_mode(l, GEOMETRY_MASK_EXCLUDE);
bool vale = script_layer_geometry_applies(l, o); // a camada vale nesse objeto?
int  modo = script_layer_get_geometry_mode(l);
bool tem  = script_layer_get_geometry_object(l, o) && script_layer_get_geometry_tile(l, 1002);
script_layer_clear_geometry_mask(l);
script_undo();

// Faces: um clique da ferramenta Select num ponto do mundo, devolve o triângulo ou -1
int t = script_layer_select_faces_world(l, 0.2, 0.0, -0.1, GEOMETRY_SELECT_POLYGON, true);
int n = script_layer_select_faces_drag_world(l, -1.0, 0.0, 0.0, 1.0, 0.0, 0.0, GEOMETRY_SELECT_TRIANGLE, false); // faces tiradas
script_layer_set_geometry_faces(l, o, 0, 4, true);              // triângulos 0 a 3
bool sel  = script_layer_get_geometry_face(l, o, 2);             // bit da máscara da camada
bool vale = script_layer_geometry_face_applies(l, o, 2);         // com modo, objetos, tiles e grupo
int  qtd  = script_layer_get_geometry_face_count(l, o);          // 0 se a malha mudou
int  tri  = script_pick_face_world(o, 0.2, 0.0, -0.1);            // -1 sem acerto ou com outro objeto na frente
int  tot  = script_object_triangle_count(o);
script_layer_clear_geometry_faces(l);
```

Cada função de escrita é um passo de undo. Em máscaras e filtros elas editam a máscara da camada pai. Os enums `geometry_mask_mode_t` (`GEOMETRY_MASK_INCLUDE`, `GEOMETRY_MASK_EXCLUDE`) e `geometry_select_mode_t` (`GEOMETRY_SELECT_TRIANGLE`, `_POLYGON`, `_CONNECTED`, `_UV_ISLAND`, `_OBJECT`) estão disponíveis nos scripts, e `script_get_context()->select_faces` liga o alvo Faces.

## Limitações

- Nas texturas a máscara é feita no espaço UV da camada. Objetos que dividem a mesma área de UV numa camada compartilhada não se separam ali: no export, no merge e na pintura a camada vale onde algum objeto incluído usa o texel. O viewport separa por objeto.
- O mesmo vale para faces: faces que se sobrepõem no UV (UVs espelhados ou empilhados) dividem o resultado, também no viewport, porque compartilham os texels. A borda entre faces selecionadas e não selecionadas dentro de uma ilha é nítida, a menos de cerca de um texel.
- **Polygon** reconstrói o polígono a partir do leque de triângulos que os importadores criam (até 64 triângulos). Malhas trianguladas de outro jeito, ou polígonos vizinhos que por acaso continuam o leque, podem selecionar um triângulo só ou um polígono a mais.
- Se a malha de um objeto muda de número de triângulos (reimport com outra triangulação, Merge Geometry, nós de malha, decimate/subdivide/bevel), as faces dele deixam de valer: ficam no arquivo, o menu mostra `(mesh changed)`, o console avisa ao abrir ou reimportar, e Clear Faces limpa. Reimportar com a mesma topologia mantém a seleção.
- A seleção de faces é só no viewport 3D; a visão 2D e o path tracer aplicam a máscara mas não mostram o destaque das faces.
- Objetos com material próprio bloqueiam o raio mas não podem ser selecionados, e objetos escondidos não podem ser clicados.
- Tiles valem por objeto (sufixo `.10xx`): um objeto único com UVs em vários tiles conta inteiro como tile 1001.
- Fill layers e fill masks são preenchidas inteiras e mascaradas só na composição.
- Os nós Layer e Layer Mask do editor de materiais leem a camada sem a máscara geométrica.
- A máscara cobre também a margem das ilhas de UV (cerca de **Dilate Radius** texels das preferências, mínimo 1) para não deixar costura; ilhas de objetos diferentes mais próximas que isso podem receber um pouco da camada.
- **Merge Geometry** (aba Meshes) junta a geometria no objeto principal; uma máscara que listava só os objetos mesclados deixa de valer neles.
