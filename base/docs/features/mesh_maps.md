# Mesh maps

IDs da matriz: BAKE-01, BAKE-02, BAKE-03 · Fase: P2

Texturas calculadas uma vez a partir da geometria do projeto (oclusão, curvatura, posição, espessura...) e lidas por qualquer material pelo nó **Mesh Map**. São a base para materiais que reagem à forma do modelo: sujeira nas cavidades, desgaste nas quinas, cor por objeto. O projeto tem um slot para cada mapa; assar de novo substitui o mapa do slot.

## Slots

| Slot | O que guarda | Sem bake |
|---|---|---|
| **Normal** | Normal map em espaço tangente do high poly, convenção OpenGL (x ao longo de u, y subindo pela imagem). Plano (0.5, 0.5, 1) sem high poly | Plano |
| **World Space Normal** | Normal da superfície em espaço de mundo, `n * 0.5 + 0.5` | Preto |
| **ID** | Uma cor por objeto (ou a cor de vértice, em `id_source`) para usar com Color Selection | Preto |
| **Ambient Occlusion** | 1 onde nada tapa a superfície, mais escuro nas cavidades e perto de outros objetos | Branco |
| **Curvature** | 0.5 em superfície plana, mais claro no convexo, mais escuro no côncavo | Cinza |
| **Position** | Posição no mundo normalizada pela caixa que envolve todos os objetos (0 a 1 por eixo) | Preto |
| **Thickness** | Distância média até o outro lado do objeto: branco é grosso, escuro é fino | Preto |
| **Bent Normals** | Direção média por onde a superfície vê o céu, em espaço de mundo | Preto |
| **Opacity** | Branco onde o raio acha o high poly (sem high poly, onde há geometria); preto fora das ilhas UV | Preto |

Normal, World Space Normal, Position e Bent Normals guardam 16 bits por canal; os outros, 8.

## Onde fica

Editor de materiais → **Input** → **Mesh Map**. O nó tem:

| Controle | O que faz |
|---|---|
| **Map** (mapa) | Slot lido pelo nó |
| **Bake** (assar) / **Rebake** | Assa o slot escolhido com os ajustes atuais do projeto. Durante o bake vira uma barra de progresso; clicar nela cancela só esse mapa. Se outro bake está rodando, o botão vira **Queued** (na fila) e um clique tira o mapa da fila |
| Saída **Color** | RGB do mapa |
| Saída **Value** | Canal vermelho, para os mapas em tons de cinza |

Ao terminar um bake, as fill layers e os efeitos são refeitos e passam a usar o mapa novo. Para assar vários mapas de uma vez e mudar os ajustes, use a aba **Baking**.

## Aba Baking

File → **Bake Mesh Maps...** abre a aba (também em Window → Tabs → Baking). Ela fica fechada por padrão, na barra lateral direita.

| Controle | O que faz |
|---|---|
| **Bake Selected** (assar selecionados) | Assa, em fila, os mapas marcados na lista |
| **Bake All** (assar todos) | Assa os nove mapas |
| Barra de progresso | Aparece no lugar dos botões durante o bake, com o nome do mapa; clicar nela cancela a fila |
| **Common Settings** (ajustes comuns) | **Resolution** (128 a 16384) e **Dilation** (texels em volta das ilhas UV), iguais para todos os mapas |
| Lista de mapas | Uma caixa por mapa: marcada entra no Bake Selected. "(not baked)" indica slot vazio |
| **Settings** de cada mapa | Ajustes só daquele mapa (Samples e Max Distance para Ambient Occlusion, Bent Normals e Thickness; Strength para Curvature; Color Source para ID), a miniatura do mapa assado e os botões **Bake** e **Clear** daquele slot |

Ambient Occlusion e Bent Normals usam os mesmos raios, então dividem Samples e Max Distance. A seleção de mapas e todos os ajustes ficam salvos no projeto.

## High poly

Um ou mais objetos do projeto podem servir de high poly: os mapas dos outros objetos (o low poly) passam a vir deles. Para cada amostra, um raio sai de um ponto acima do low poly (a "cage"), desce pela normal e para no primeiro high poly que encontra; dali saem a normal, a posição, o ID, a opacidade, e a oclusão, a espessura e as bent normals são calculadas sobre o high poly. Onde o raio não acha nada, o low poly fica no lugar (e Opacity fica preta, a Normal fica plana).

Na aba Baking, seção **High Poly**:

| Controle | O que faz |
|---|---|
| Caixa por objeto | Marca o objeto como high poly. High polys não recebem mapas |
| **Match by Name** (casar pelo nome) | `nome_low` só acha `nome_high`, e todo objeto terminado em `_high` vira high poly sem precisar marcar ("by name" ao lado do nome) |
| **Max Frontal Distance** | Altura da cage acima do low poly, relativa à diagonal da cena |
| **Max Rear Distance** | Até onde abaixo do low poly o raio procura, relativa à diagonal da cena |
| **Average Normals** | Os raios seguem a média das normais dos vértices no mesmo lugar: sem buracos nas quinas vivas do low poly |
| **Ignore Backface** | O raio atravessa triângulos do high poly vistos por trás |

Em Common Settings, **Antialiasing** (None, 2x2, 4x4, 8x8) faz várias amostras por texel e tira a média; os raios de oclusão e espessura se dividem entre as amostras. Vale também sem high poly.

High polys continuam visíveis no viewport; esconda-os na aba Meshes ao pintar.

## Como o bake funciona

- Todos os objetos do projeto são rasterizados no espaço UV (uma amostra no centro de cada texel); cada texel recebe posição e normal de mundo.
- Oclusão, Bent Normals e Thickness disparam raios a partir de cada texel contra todos os objetos (raios na CPU, com uma BVH). Oclusão e Bent Normals usam o hemisfério acima da superfície; Thickness, o de dentro.
- Curvature compara a normal de cada texel com a dos vizinhos na mesma ilha.
- No fim, as ilhas UV crescem alguns texels (**dilation**) para não aparecer costura no filtro da textura.
- O bake roda numa fila: cada frame processa algumas linhas em várias threads, então a interface continua respondendo. O nó Bake Texture usa a mesma fila, então um bake de nó e um de mesh map nunca rodam juntos; enquanto espera, o nó mostra **Queued**. Um mapa que já está na fila não entra duas vezes.
- Ao terminar um bake, um undo ou um Clear, todas as fill layers, máscaras e efeitos cujo material lê um mesh map são refeitos (mesmo com outro material selecionado), assim como os ícones desses materiais.

## Ajustes

Ficam no projeto (`bake_settings` no `.arm`) e valem para os próximos bakes. Na interface ficam na aba Baking; no script, em `script_bake_get_settings()`.

| Campo | Padrão | O que faz |
|---|---|---|
| `res` | 2048 | Largura e altura dos mapas |
| `dilation` | 16 | Texels acrescentados em volta das ilhas UV |
| `occlusion_samples` | 64 | Raios por texel de Ambient Occlusion e Bent Normals |
| `occlusion_distance` | 0.25 | Distância máxima de um oclusor, relativa à diagonal da cena |
| `thickness_samples` | 32 | Raios por texel de Thickness |
| `thickness_distance` | 0.5 | Distância máxima medida, relativa à diagonal da cena |
| `curvature_strength` | 1.0 | Multiplica o contraste da curvatura |
| `id_source` | `MESH_MAP_ID_OBJECT` | Cor por objeto, ou `MESH_MAP_ID_VERTEX_COLOR` para a cor de vértice |
| `maps` | todos | Um bit por slot (`1 << MESH_MAP_OCCLUSION`...): os mapas que o Bake Selected assa |
| `aa` | 1 | Amostras por lado do texel: 1, 2, 4 ou 8 |
| `front_distance` | 0.01 | Altura da cage acima do low poly, relativa à diagonal da cena |
| `rear_distance` | 0.01 | Até onde abaixo do low poly o high poly é procurado |
| `average_normals` | true | Raios pela média das normais nos vértices no mesmo lugar |
| `ignore_backface` | true | Raios atravessam o verso dos triângulos do high poly |
| `match_by_name` | false | `nome_low` só acha `nome_high`; objetos terminados em `_high` são high poly |
| `high_polys` | vazio | Nomes dos objetos high poly, um por linha; segue renomeações |

## Atalhos

Nenhum atalho novo. Ctrl+Z/Ctrl+Y desfazem e refazem cada bake e cada limpeza de slot; no histórico o passo se chama "Bake Mesh Map" ou "Clear Mesh Map".

## Projeto e export

- O formato do `.arm` sobe para a versão 23, com dois campos no fim do projeto: `mesh_maps` (por slot: `slot`, `set`, `res`, `bpp` e os pixels RGBA comprimidos com LZ4) e `bake_settings` (JSON com os ajustes acima).
- Projetos da versão 22 ou anterior abrem sem mesh maps e com os ajustes padrão.
- Export as .arm de um grupo (LAY-14) não leva os mesh maps.
- Os mesh maps não entram no export de texturas; o preset "Mesh maps" é a BAKE-10.

## Script

```c
bake_settings_t *s = script_bake_get_settings();
s->res               = 1024;
s->occlusion_samples = 128;
script_bake_mesh_map(MESH_MAP_OCCLUSION); // ou script_bake_mesh_maps() para os nove
s->maps = (1 << MESH_MAP_OCCLUSION) | (1 << MESH_MAP_CURVATURE);
script_bake_selected();                   // os mapas marcados em s->maps
script_show_tab(TAB_BAKING);              // abre a aba Baking
script_bake_set_high_poly(script_get_object("corpo_high"), true);
s->match_by_name  = true;  // corpo_low só acha corpo_high
s->front_distance = 0.02;  // relativas à diagonal da cena
s->rear_distance  = 0.02;
s->aa             = 4;     // 4x4 amostras por texel
// A cada frame:
if (!script_bake_busy()) {
	f32_array_t *px = script_mesh_map_pixels(MESH_MAP_OCCLUSION); // rgba 0-1, linha a linha
	gpu_texture_t *t = script_mesh_map_get(MESH_MAP_OCCLUSION);  // NULL sem bake
}
float progresso = script_bake_progress();
int   estado   = script_bake_state(MESH_MAP_THICKNESS); // BAKE_STATE_IDLE, _QUEUED ou _RUNNING
script_bake_cancel_slot(MESH_MAP_THICKNESS);           // tira um mapa da fila
script_bake_cancel();                                   // esvazia a fila
script_mesh_map_clear(MESH_MAP_ID);
```

Os enums `mesh_map_slot_t` (`MESH_MAP_NORMAL` ... `MESH_MAP_OPACITY`, `MESH_MAP_COUNT`), `mesh_map_id_source_t`, `bake_state_t` e `tab_id_t` estão disponíveis nos scripts.

## Limitações

- A cage é a superfície do low poly deslocada pela Max Frontal Distance; cage automática suavizada, skew map e cage pintada chegam com a BAKE-04, BAKE-06 e BAKE-07.
- Não há mapa de altura (Height) vindo do high poly.
- Um slot por projeto: todos os objetos compartilham o mesmo espaço UV, como as camadas. Texture sets separados são a PRJ-05.
- UVs fora de 0 a 1 (tiles UDIM além do 1001) não entram nos mapas.
- A curvatura vem da normal interpolada dentro de cada ilha UV; quinas que caem numa costura de UV não aparecem. A curvatura por geometria é a BAKE-09.
- O bake roda na CPU. Oclusão e espessura em 4K com muitos raios levam minutos; os bakers por GPU (RT) continuam disponíveis pelo nó Bake Texture.
- O nó Mesh Map amostra o mapa na primeira UV da malha, mesmo em fill layers com escala, triplanar ou decal. Em camadas de sculpt (sem UV da malha no shader) ele não acompanha a malha.
