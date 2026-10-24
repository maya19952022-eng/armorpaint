# Lock de camada

IDs da matriz: LAY-04 · Fase: P1

Trava uma camada para que nenhum pincel, preenchimento ou ferramenta de pintura altere seus pixels. Útil para proteger uma camada pronta enquanto se pinta nas vizinhas.

## Onde fica

- **Menu de contexto da camada** (clique direito) → caixa **Locked**.
- Uma camada travada mostra um cadeado no fim do nome, na aba Layers.

## Controles

| Controle | O que faz |
|---|---|
| Locked | Liga ou desliga o lock da camada ou do grupo |

Máscaras e filtros seguem o lock da camada a que pertencem: com a camada travada, também não dá para pintar na máscara dela. O lock não impede mover, renomear, apagar, mudar opacidade ou blend.

## Atalhos

Nenhum. Ctrl+Z/Ctrl+Y desfazem e refazem a mudança de lock.

## Projeto e export

Salvo no `.arm` no campo opcional `locked` de cada camada. Projetos antigos abrem com todas as camadas destravadas. Não muda o export de texturas.

## Script

```c
slot_layer_t *l = script_layer_get("Layer 1");
script_layer_set_locked(l, true); // passo de undo
bool travada = l->locked;
```

`script_paint()` não pinta numa camada travada, igual à interface.

## Limitações

- Não existe lock parcial (só cor, só máscara); o lock vale para a camada inteira.
