# Pilha de efeitos em camadas e máscaras

IDs da matriz: LAY-05, LAY-07, LAY-09 (e LAY-16 para o efeito Fill) · Fase: P1

Cada camada e cada máscara pode ter uma pilha de efeitos não destrutivos: ajustes de cor, desfoque, nitidez, preenchimentos e geradores de máscara. Cada efeito lê o resultado do que está abaixo dele na pilha; a pintura da camada ou da máscara continua editável e os efeitos se atualizam.

## Onde fica

- **Menu de contexto da camada ou da máscara** (clique direito) → **Add Effect** → escolha o efeito. O efeito novo vai para o topo da pilha.
- Na aba Layers os efeitos de uma camada aparecem logo abaixo dela; os de uma máscara, logo acima da máscara.
- Selecione um efeito e edite os parâmetros no editor de nós: cada efeito tem um material próprio, chamado "<camada> <efeito>", com o nó **Effect Input** (o que está abaixo na pilha) ligado ao nó do efeito e à saída.

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

Em camadas, os efeitos mudam só a cor base; normal, oclusão, roughness, metallic, altura, emissão e subsurface passam pela pilha sem mudança. O olho de cada efeito liga e desliga o efeito: um efeito escondido é pulado e o próximo lê o que estava abaixo dele. Esconder a máscara esconde os efeitos dela. O blend, a opacidade e as máscaras da camada valem para o resultado da pilha; a opacidade da máscara vale para o resultado da pilha da máscara.

## Atalhos

Nenhum novo. Ctrl+Z/Ctrl+Y desfazem e refazem criar, apagar e esconder efeitos.

## Projeto e export

- Efeitos de camada são salvos como filtros da camada (como antes); efeitos de máscara são máscaras com o campo opcional `effect`. Projetos antigos abrem iguais.
- O viewport, o export de texturas e o Flatten usam o topo de cada pilha.
- Apagar uma camada ou máscara apaga os efeitos dela; duplicar copia os efeitos.
- **Merge Down** fica desabilitado numa camada com efeitos; **Merge Group** e **Flatten** aplicam os efeitos antes de juntar.
- Efeitos são preenchimentos: só cobrem as ilhas UV da malha (como camadas de preenchimento).

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
```

Tipos: `EFFECT_TYPE_FILTER`, `EFFECT_TYPE_FILL`, `EFFECT_TYPE_LEVELS`, `EFFECT_TYPE_HSV`, `EFFECT_TYPE_COLOR_BALANCE`, `EFFECT_TYPE_SHARPEN`, `EFFECT_TYPE_BLUR`, `EFFECT_TYPE_COMPARE_MASK`, `EFFECT_TYPE_COLOR_SELECTION`, `EFFECT_TYPE_ANCHOR`.

## Limitações

- Cada efeito cria um material na lista de materiais; apagar o efeito não apaga o material.
- Não existe efeito "Paint" (camada pintável dentro da pilha): pinte na própria camada ou máscara, que é a base da pilha, ou use um grupo.
- Mover uma máscara para outro lugar pode separar ela dos seus efeitos; os efeitos passam a mudar a máscara que ficar logo abaixo deles.
- Reordenar efeitos é feito arrastando, como camadas.
