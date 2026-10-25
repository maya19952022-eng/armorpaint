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

Máscaras e efeitos seguem o lock da camada a que pertencem, e as camadas seguem o lock do grupo: com o grupo travado, nenhuma camada dele recebe pintura. O lock bloqueia pincéis, preenchimentos, bakes e partículas; os conta-gotas de cor, de material e de Color ID continuam funcionando. **Merge Down** fica desabilitado quando mudaria uma camada travada. O lock não impede mover, renomear, apagar, mudar opacidade ou blend.

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
