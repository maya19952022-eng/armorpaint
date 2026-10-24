# Grupos pass-through

IDs da matriz: LAY-02 · Fase: P1

Os grupos do ArmorPaint são pass-through: cada camada de um grupo se mistura direto com as camadas abaixo do grupo, com o próprio modo de blend, como se o grupo não existisse. O grupo só organiza as camadas e acrescenta a opacidade e as máscaras dele.

## Onde fica

Aba Layers: a linha do grupo mostra **Pass Through** no lugar do modo de blend.

## Controles

| Controle | O que faz |
|---|---|
| Opacidade do grupo | Multiplica a opacidade de cada camada do grupo |
| Máscaras do grupo | Valem para cada camada do grupo |
| Blend das camadas do grupo | Mistura com tudo o que está abaixo do grupo. Ex.: uma camada em Multiply dentro do grupo escurece as camadas de fora, abaixo dele |

## Atalhos

Nenhum.

## Projeto e export

Nada novo no `.arm`. O viewport, o export de texturas e o Flatten compõem os grupos do mesmo jeito.

## Script

Grupos se criam com `script_layer_new_group(camada)` e camadas entram com `script_layer_set_group(camada, grupo)`; não há opção de modo.

## Limitações

- Não existe grupo isolado (compor o grupo sozinho e depois misturar o resultado com um modo de blend do grupo). Para esse efeito use **Flatten** no grupo (veja `group_flatten.md`) e escolha o blend da camada resultante.
