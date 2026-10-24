# Seleção de várias camadas

IDs da matriz: LAY-03 · Fase: P1

Seleciona várias camadas ao mesmo tempo para mover, apagar, agrupar ou achatar todas de uma vez.

## Onde fica

Aba Layers. A camada ativa tem o contorno normal de seleção; as outras selecionadas ganham um contorno mais claro. Com mais de uma camada selecionada, o menu de contexto de uma delas mostra no topo quantas estão selecionadas e as ações em lote.

## Controles

| Controle | O que faz |
|---|---|
| Ctrl+clique na camada | Adiciona ou tira a camada da seleção |
| Shift+clique na camada | Seleciona todas as camadas entre a ativa e a clicada |
| Clique simples | Seleciona só aquela camada (num item já selecionado, mantém a seleção para arrastar) |
| Arrastar uma camada selecionada | Move todas as selecionadas juntas, mantendo a ordem entre elas |
| Delete Selected | Apaga todas as selecionadas |
| Group Selected | Põe as selecionadas que não estão em grupo num grupo novo, na mesma ordem |
| Flatten Selected | Agrupa as selecionadas e faz o flatten não destrutivo do grupo (veja `group_flatten.md`) |

Máscaras não entram na seleção; elas acompanham a camada. Selecionar um grupo e uma camada dele apaga e move o grupo inteiro uma vez só.

## Atalhos

| Atalho | Ação |
|---|---|
| Delete | Com mais de uma camada selecionada, apaga todas |

Mover várias camadas é um passo de undo só. Apagar é um passo por camada; agrupar é um passo para o grupo e um por camada que precisa mudar de lugar para entrar nele.

## Projeto e export

A seleção não é salva no projeto.

## Script

```c
script_layer_select(a, false, false); // só a
script_layer_select(b, true, false);  // ctrl: soma b
script_layer_select(d, false, true);  // shift: de b até d
int n = script_layer_selected_count();
script_layer_move_selected(1);        // índice de destino
slot_layer_t *grupo = script_layer_group_selected();
slot_layer_t *flat  = script_layer_flatten_selected();
script_layer_delete_selected();
```

## Limitações

- Mudar blend, opacidade ou visibilidade de todas as selecionadas de uma vez ainda não existe.
- Group Selected ignora camadas que já estão dentro de um grupo (não há grupos aninhados no ArmorPaint).
