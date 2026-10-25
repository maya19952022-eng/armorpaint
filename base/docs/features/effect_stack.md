# Pilha de efeitos em camadas e máscaras

IDs da matriz: LAY-05, LAY-07, LAY-09 (e LAY-16 para o efeito Fill) · Fase: P1

Cada camada e cada máscara pode ter uma pilha de efeitos não destrutivos: ajustes de cor, desfoque, nitidez, preenchimentos, traços de pintura e geradores. Cada efeito lê o resultado do que está abaixo dele na pilha; a pintura da camada ou da máscara continua editável e os efeitos se atualizam.

## Onde fica

- **Menu de contexto da camada ou da máscara** (clique direito) → **Add Effect** → escolha o efeito. O efeito novo vai para o topo da pilha. **Generator** abre um segundo menu com os presets.
- Na aba Layers os efeitos de uma camada aparecem logo abaixo dela; os de uma máscara, logo acima da máscara.
- Selecione um efeito e edite os parâmetros no editor de nós: cada efeito tem um material próprio, chamado "<camada> <efeito>", com o nó **Effect Input** (o que está abaixo na pilha) ligado ao nó do efeito e à saída. O efeito Paint não tem material: selecione-o e pinte com o material e a ferramenta do momento.

## Controles

| Efeito | Em camadas | Em máscaras | O que faz |
|---|---|---|---|
| Filter | sim | sim | Grafo livre: começa passando a entrada direto; monte qualquer cadeia de nós entre **Effect Input** e a saída |
| Fill | sim | sim | Nó Mix Color com Factor 1 e cor branca por cima da entrada. Numa máscara começa branco: a camada fica visível |
| Levels | sim | sim | Nó Levels na cor base (ou no valor da máscara) |
| Hue/Saturation/Value | sim | não | Nó Hue/Saturation/Value na cor base |
| Color Balance | sim | não | Nó Color Balance na cor base |
| Sharpen | sim | sim | Nó Sharpen |
| Blur | sim | sim | Nó Blur |
| Compare Mask | não | sim | Nó Compare Mask: A é a máscara abaixo, B começa em 0.5; ligue outro valor (por exemplo um nó **Layer** apontando para um Anchor Point) em B |
| Color Selection | não | sim | Nó Color Selection; ligue o mapa de ID (nó Image Texture) na entrada Color |
| Anchor Point | sim | sim | Passa a entrada sem mudar. Guarda o estado da pilha naquele ponto para ser lido por outras camadas com o nó **Layer** ou **Layer Mask** |
| Paint | sim | sim | Traços pintados por cima do que está abaixo na pilha; o que fica abaixo continua editável. Veja [Paint](#paint) |
| Generator | sim | sim | Nó Mix Color com branco por cima da entrada; o fator vem de um gerador (Edges, 3D Gradient, Facing Up ou Noise). Veja [Generator](#generator) |

Em camadas, os efeitos de material mudam só a cor base; normal, oclusão, roughness, metallic, altura, emissão, subsurface e o id de material passam pela pilha sem mudança. O olho de cada efeito liga e desliga o efeito: um efeito escondido é pulado e o próximo lê o que estava abaixo dele. Esconder a máscara esconde os efeitos dela, e esconder um grupo esconde os efeitos das camadas dele. Apagar ou arrastar um efeito refaz os de cima na hora. O blend, a opacidade e as máscaras da camada valem para o resultado da pilha; a opacidade da máscara vale para o resultado da pilha da máscara.

## Paint

O efeito Paint é uma camada (ou máscara) de traços dentro da pilha. Ele começa vazio e mostra o que está abaixo dele; o que você pinta nele fica por cima, e mudar o que está abaixo (pintar a camada, esconder ou editar um efeito de baixo) mantém os traços por cima.

- Pincel, borracha, fill, decal, texto, clone, blur, partículas e simetria pintam nos traços. A **borracha** apaga os traços e mostra de novo o que está abaixo.
- Numa camada, os traços cobrem todos os canais com os valores do material do pincel. No menu de contexto do efeito, em **Channels**, o blend e a opacidade de cada canal dizem como os traços se misturam com o que está abaixo: por exemplo, opacidade 0 em Roughness e Metallic para traços que só mudam a cor. O blend e a opacidade da camada continuam valendo para o resultado da pilha.
- Numa máscara, os traços guardam o valor (vermelho) e a cobertura (alfa): pintar de branco mostra a camada, de preto esconde, e a borracha volta ao valor de baixo.
- **Clear** apaga os traços. **To Fill** e **Invert** não aparecem num efeito Paint.
- Um efeito Paint arrastado para fora da pilha (por exemplo para o topo) vira uma camada ou máscara comum com os seus traços.
- O efeito acima de um Paint, os nós **Layer** e **Layer Mask**, o viewport, o export, o Flatten, a vista 2D e o conta-gotas leem o resultado (entrada com os traços). O ícone da linha mostra só os traços.

## Generator

Presets do Generator, cada um uma cadeia de nós que termina num **Map Range** com Clamp ligado ao fator do Mix Color:

| Preset | Cadeia | Começa com |
|---|---|---|
| Edges (padrão) | Curvature Texture → Map Range | Strength 1, Radius 1, Offset 0; From 0.2 a 0.5 |
| 3D Gradient | Geometry (Position) → Separate XYZ (Z) → Map Range | From −1 a 1 (altura no mundo, z para cima) |
| Facing Up | Geometry (Normal) → Separate XYZ (Z) → Map Range | From 0.5 a 1 |
| Noise | Noise Texture → Map Range | Scale 8, Detail 4; From 0.4 a 0.6 |

Numa máscara o gerador decide onde a camada aparece; numa camada ele pinta branco por cima da entrada onde o fator é alto (troque a Color 2 do Mix Color para outra cor). Edite o Map Range e os outros nós no editor de nós; a pilha se atualiza.

## Atalhos

Nenhum novo. Ctrl+Z/Ctrl+Y desfazem e refazem criar, apagar e esconder efeitos e os traços de um efeito Paint.

## Projeto e export

- Efeitos de camada são salvos como filtros da camada (como antes); efeitos de máscara são máscaras com o campo opcional `effect`. O efeito Paint salva os traços nas texturas da camada ou máscara e o campo opcional `effect_paint`; o resultado é refeito ao abrir. Projetos antigos abrem iguais.
- O viewport, o export de texturas e o Flatten usam o topo de cada pilha.
- Apagar uma camada ou máscara apaga os efeitos dela; duplicar copia os efeitos. Duplicar um efeito põe a cópia logo acima dele, lendo o original. Copiar e colar um efeito Paint cola os traços (dois passos de undo: o efeito e os traços).
- **Merge Down** fica desabilitado numa camada com efeitos, num efeito e sobre uma camada com efeitos; **Merge Group** e **Flatten** aplicam os efeitos antes de juntar. Um efeito Paint de máscara no topo da pilha vira uma máscara com o resultado antes do Merge Down.
- **Apply** de máscara fica desabilitado numa máscara com efeitos e num efeito de máscara.
- Projetos antigos com vários filtros na mesma camada agora mostram só o filtro de cima (os filtros viraram uma pilha), e os filtros passam a respeitar as máscaras da camada.
- Efeitos de material são preenchimentos: só cobrem as ilhas UV da malha (como camadas de preenchimento). O Paint cobre o que a entrada cobre e os traços.

## Script

```c
slot_layer_t *lv = script_layer_add_effect(l, EFFECT_TYPE_LEVELS);
script_material_set(lv->fill_material);
ui_node_t *n = script_material_get_node("LEVELS");
script_material_set_float(n, 1, 4, 1.0); // Out Black
script_material_set_float(n, 1, 5, 0.0); // Out White: inverte
script_material_update();                // atualiza as camadas e efeitos do material
script_layer_set_visible(lv, false);     // como o olho, passo de undo
slot_layer_t *fill = script_layer_add_effect(mascara, EFFECT_TYPE_FILL);
bool e = fill->effect;                   // true para efeitos de máscara

slot_layer_t *p = script_layer_add_effect(l, EFFECT_TYPE_PAINT);
script_layer_set(p);                     // pinte com script_paint() e script_paint_end()
script_layer_set_channel(p, LAYER_CH_ROUGH, BLEND_TYPE_MIX, 0.0); // os traços não mudam a roughness
bool tracos = p->effect_paint;           // p->texpaint são os traços, p->texpaint_result o resultado

slot_layer_t *g = script_layer_add_generator(mascara, GENERATOR_TYPE_FACING_UP);
script_material_set(g->fill_material);   // edite o Map Range como qualquer nó
```

Tipos: `EFFECT_TYPE_FILTER`, `EFFECT_TYPE_FILL`, `EFFECT_TYPE_LEVELS`, `EFFECT_TYPE_HSV`, `EFFECT_TYPE_COLOR_BALANCE`, `EFFECT_TYPE_SHARPEN`, `EFFECT_TYPE_BLUR`, `EFFECT_TYPE_COMPARE_MASK`, `EFFECT_TYPE_COLOR_SELECTION`, `EFFECT_TYPE_ANCHOR`, `EFFECT_TYPE_PAINT`, `EFFECT_TYPE_GENERATOR` (com o preset Edges). Presets: `GENERATOR_TYPE_EDGES`, `GENERATOR_TYPE_GRADIENT`, `GENERATOR_TYPE_FACING_UP`, `GENERATOR_TYPE_NOISE`.

## Limitações

- Cada efeito cria um material na lista de materiais; apagar o efeito não apaga o material.
- Paint: sem ajuste de opacidade por canal, um traço cobre todos os canais com os valores do material (zere a opacidade dos canais que não devem mudar). Sob os traços, o id de material segue a regra do Merge Down (fica o maior), e a emissão e o subsurface são os do material do pincel.
- Paint: a pré-visualização ao vivo do pincel e a do Material tool ficam desligadas no efeito; o resultado aparece a cada quadro do traço. O efeito não pode ser movido na vista 2D.
- Generator: Edges usa a curvatura calculada em espaço UV (`ddx` da normal), sem sinal (convexo e côncavo juntos) e sensível à densidade de UV e às costuras. Dirt, Metal Edge Wear, Thickness e normal/posição assadas esperam os mesh maps (BAKE-01).
- Mover uma máscara para outro lugar pode separar ela dos seus efeitos; os efeitos passam a mudar a máscara que ficar logo abaixo deles.
- Reordenar efeitos é feito arrastando, como camadas.
- **Merge Group** e **Flatten** usam o resultado do efeito de cima sem a opacidade própria desse efeito.
- Ao pintar, só os efeitos da camada pintada (e o que lê outras camadas por nós Layer) são refeitos a cada traço.
