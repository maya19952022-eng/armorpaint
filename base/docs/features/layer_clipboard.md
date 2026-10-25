# Copiar e colar camadas e máscaras

IDs da matriz: LAY-15 · Fase: P1

Copia uma camada, grupo ou máscara e cola em outro lugar da pilha. Uma máscara copiada pode ser colada como máscara nova de outra camada.

## Onde fica

Menu de contexto da camada (clique direito) → **Copy**, **Paste** e **Paste as Mask**.

## Controles

| Controle | O que faz |
|---|---|
| Copy | Guarda a camada, o grupo ou a máscara clicada |
| Paste | Cria uma cópia do que foi guardado logo acima da camada clicada (com as máscaras, os efeitos e, num grupo, as camadas dele). Com um efeito guardado, a cópia do efeito vai para o topo da pilha de efeitos da camada ou máscara clicada |
| Paste as Mask | Com uma máscara guardada: cria uma máscara nova na camada clicada com os pixels e a opacidade da máscara copiada |

O que foi copiado é uma referência: se a camada original for apagada, Paste fica desabilitado. Paste fica desabilitado sobre uma máscara; Paste as Mask só aparece habilitado quando o que foi copiado é uma máscara e a camada clicada aceita máscara (não é máscara nem filtro).

## Atalhos

| Atalho | Ação |
|---|---|
| Ctrl+C | Copia a camada ativa (com a aba Layers em foco) |
| Ctrl+V | Cola acima da camada ativa; se o que foi copiado é uma máscara, cola como máscara |

Colar é desfeito com Ctrl+Z: Paste são dois passos (duplicar e mover), Paste as Mask são dois passos (máscara branca nova e os pixels colados).

## Projeto e export

Nada novo no `.arm`; o resultado é uma camada ou máscara comum.

## Script

```c
script_layer_paste(origem, alvo);                  // true se colou
slot_layer_t *m = script_layer_paste_mask(mascara, alvo); // NULL se não deu
```

## Limitações

- A área de transferência vive só dentro do projeto aberto; não copia entre projetos nem para outros programas.
