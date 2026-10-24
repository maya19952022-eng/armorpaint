# Máscara geométrica

IDs da matriz: LAY-11 · Fase: P1

Restringe onde uma camada ou um grupo vale pela geometria: objetos do projeto e tiles UDIM. Em **Include** a camada só aparece na geometria selecionada; em **Exclude** aparece em toda a geometria menos nela. Fora da máscara a camada não contribui no viewport, no merge, no flatten e no export de texturas, e pintar ou preencher ali não muda nada.

## Onde fica

Aba Layers → menu de contexto da camada ou do grupo (clique direito) → seção **Geometry Mask**, no fim do menu. Máscaras e filtros seguem a máscara geométrica da camada a que pertencem. Camadas de sculpt não mostram a seção.

## Controles

| Controle | O que faz |
|---|---|
| **Include** (incluir) / **Exclude** (excluir) | Include: a camada vale só na geometria selecionada. Exclude: vale em toda a geometria menos na selecionada |
| **Objects** (objetos) | Uma caixa por objeto do projeto, com os nomes da aba Meshes |
| **UV Tiles** (tiles UV) | Aparece quando há objetos com sufixo de tile UDIM (`.1001`, `.1002`..., como o import com split por UDIM cria). Uma caixa por tile |
| **Clear Geometry Mask** | Desmarca tudo e volta para Include: a camada volta a valer em toda a geometria |

- Sem nenhuma caixa marcada não há máscara, qualquer que seja o modo.
- Com objetos e tiles marcados, a seleção é a geometria que atende às duas listas (por exemplo, o objeto `corpo` só no tile 1002). Objetos sem sufixo de tile contam como tile 1001.
- A máscara do grupo vale para as camadas dele, junto com a máscara de cada camada: a camada aparece onde as duas deixam.
- Pintar numa camada (ou numa máscara dela) só muda os texels dentro da máscara geométrica; o Bake também não limpa os texels de fora.

## Atalhos

Nenhum atalho novo. Ctrl+Z/Ctrl+Y desfazem e refazem cada mudança; no histórico o passo se chama "Geometry Mask".

## Projeto e export

- O `.arm` guarda três campos opcionais por camada: `geometry_mask_mode` (0 include, 1 exclude), `geometry_mask_tiles` (lista de tiles) e `geometry_mask_objects` (nomes dos objetos). Listas vazias são gravadas como null.
- Projetos da versão 21 ou anterior e projetos da versão 22 salvos antes desta mudança abrem sem máscara e com o mesmo resultado. A versão do formato continua 22.
- Export de texturas, Merge Down, Merge Group, Flatten, visão 2D e path tracer aplicam a máscara. Merge Down e Merge Group gravam a máscara na opacidade das camadas antes de juntar: a camada resultante não tem máscara geométrica e o resultado fica igual.
- No export **Per Object**, uma camada cuja máscara deixa o objeto de fora não entra na textura dele.
- Os objetos são guardados pelo nome: renomear um objeto atualiza as máscaras, e um objeto apagado deixa de contar.

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
```

Cada função de escrita é um passo de undo. Em máscaras e filtros elas editam a máscara da camada pai. O enum `geometry_mask_mode_t` (`GEOMETRY_MASK_INCLUDE`, `GEOMETRY_MASK_EXCLUDE`) está disponível nos scripts.

## Limitações

- Ainda não há seleção por polígonos ou faces, só por objetos e tiles.
- Nas texturas a máscara é feita no espaço UV da camada. Objetos que dividem a mesma área de UV numa camada compartilhada não se separam ali: no export, no merge e na pintura a camada vale onde algum objeto incluído usa o texel. O viewport separa por objeto.
- Tiles valem por objeto (sufixo `.10xx`): um objeto único com UVs em vários tiles conta inteiro como tile 1001.
- Fill layers e fill masks são preenchidas inteiras e mascaradas só na composição.
- Os nós Layer e Layer Mask do editor de materiais leem a camada sem a máscara geométrica.
- A máscara cobre também a margem das ilhas de UV (cerca de **Dilate Radius** texels das preferências, mínimo 1) para não deixar costura; ilhas de objetos diferentes mais próximas que isso podem receber um pouco da camada.
- **Merge Geometry** (aba Meshes) junta a geometria no objeto principal; uma máscara que listava só os objetos mesclados deixa de valer neles.
