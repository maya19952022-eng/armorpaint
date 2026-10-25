# Smoke test headless do ArmorPaint

Teste de ponta a ponta que roda dentro do app via `--background --script`:

1. abre o projeto `fixtures/p0_project.arm`;
2. confere as camadas (ordem, blending, opacidade, visibilidade, grupo/máscara) contra `fixtures/p0_project.json`;
3. exporta as texturas (preset `generic`) e compara a média de cada canal (`_base`, `_occ`, `_rough`, `_metal`, `_nor`) com os valores gravados, tolerância de 3/255;
4. cria uma camada, salva como `paint/build/smoke/smoke_resaved.arm`, abre um projeto novo, reabre o salvo e repete 2 e 3.

Imprime `SMOKE PASS` ou `SMOKE FAIL`; o launcher sai com 0 (passou), 1 (falhou) ou 2 (erro de ambiente). Log em `paint/build/smoke/run.log`.

## Como rodar

Compile antes (`cd paint && ../base/make --compile`).

Linux (máquina com GPU ou cloud sem GPU):

```bash
base/tests/paint_headless/run.sh
```

Sem `DISPLAY`, o script usa `xvfb-run`; sem GPU, o Vulkan roda no llvmpipe (Mesa). Pacotes: `xvfb mesa-vulkan-drivers` (veja `base/docs/building_cloud.md`). Leva cerca de 15 s no llvmpipe.

Windows (Direct3D12, GPU real):

```bat
base\tests\paint_headless\run.bat
```

macOS: não testado. O app roda de dentro do bundle `.app`, então os caminhos relativos de `run.c` (que assumem `paint/build/out` como diretório de trabalho) precisam de ajuste.

Os launchers guardam `paint/build/out/data/config.json` como `config.json.smoke_backup`, rodam com a config padrão e restauram a original no fim. Uma config em modo Sculpt, por exemplo, cria uma camada extra e muda o resultado.

## O fixture

`fixtures/p0_project.arm` foi salvo com o formato de camadas da fase P0 (antes do blend por canal da P1). Conteúdo:

| Camada | Blending | Opacidade | Observação |
|---|---|---|---|
| `Layer 1` | Mix | 1.0 | camada padrão do projeto novo |
| `p0_gray` | Mix | 1.0 | cinza 0x808080, roughness 0.5 |
| `p0_multiply` | Multiply | 0.5 | 0xcc3311, metallic 1.0 |
| `p0_group` | — | 0.8 | grupo |
| `p0_add` (no grupo) | Add | 0.75 | 0x1144cc, occlusion 0.5 |
| `p0_masked` | Overlay | 1.0 | 0x22cc22, escondida pela máscara preta `p0_mask` |
| `p0_hidden` | Mix | 1.0 | branca, invisível |

Base color esperado: (126, 118, 190), igual à conta manual: cinza 128 → multiply 50% → add 0.75 × 0.8.

Fases futuras **não regeneram** o fixture: ele existe para provar que projetos antigos continuam abrindo com o mesmo resultado (regra 7 da seção 0 do plano). Se uma fase muda o resultado de propósito, ela atualiza `p0_project.json` e explica no PR. Para criar um fixture novo de uma fase (por exemplo `p1_project.arm`), copie `make_fixture.c`, mude os nomes e rode `run.sh make_fixture_p1.c`.

Regenerar o fixture da P0 (só se ele se perder):

```bash
base/tests/paint_headless/run.sh make_fixture.c
```

## Fixture da versão 22

`fixtures/p1_v22_project.arm` é um projeto da versão 22 salvo antes da máscara geométrica (LAY-11) acrescentar campos às camadas, com seis camadas (uma travada, uma máscara preta a 50%, um grupo). `p1_geometry_mask.c` abre o arquivo e compara com `fixtures/p1_v22_project.json`: propriedades das camadas, nenhuma máscara geométrica e a média do `_base` exportado. Foi gerado por `make_fixture_p1_v22.c` com o build do commit `013316c`; um build mais novo grava os campos novos e o fixture deixaria de testar esse caso, então não o regenere.

## Fixture da máscara geométrica sem faces

`fixtures/p1_geometry_project.arm` é um projeto da versão 22 salvo com a máscara geométrica de objetos e tiles (44 campos por camada), antes da seleção de faces acrescentar os seus campos às camadas. Tem os quads `geo_a` e `geo_b` e quatro camadas sobre a base: `fx_red` inclui `geo_a`, `fx_blue` (opacidade 0.5) exclui o tile 1002, `fx_green` fica no grupo `fx_group`, que inclui `geo_b`. `p1_geometry_faces.c` abre o arquivo e compara com `fixtures/p1_geometry_project.json`: máscaras das camadas, nenhuma face e a média do `_base` exportado. Foi gerado por `make_fixture_p1_geometry.c` com o build do commit `8a03c95`; um build mais novo grava os campos de faces e o fixture deixaria de testar esse caso, então não o regenere.

## Fixture das camadas Skew e Cage Map da P2

`fixtures/p2_roles_project.arm` é um projeto da versão 23 salvo pelos builds da P2 de antes de os campos do efeito Paint e da seleção de faces (P1) entrarem nas camadas: 45 campos por camada, com `bake_role` na posição 45, onde hoje fica `effect_paint`. Tem a camada da base, um Skew Map preenchido de branco e um Cage Map. `p2_role_layers.c` abre o arquivo e confere que os papéis voltam e que nenhuma camada vira efeito Paint. Foi gerado por `make_fixture_p2_roles.c` com o build do commit `da6d9b9` (`ARMORPAINT_APP=<esse build> run.sh make_fixture_p2_roles.c`); um build mais novo grava todos os campos e o fixture deixaria de testar esse caso, então não o regenere.

## Teste de integração existente

`paint/assets/plugins/dev/test.c` é um teste bem maior (82 passos, 220 checks) do upstream. Ele também roda com `--script`, mas grava em `/tmp/armorpaint`, deixa a config em modo Sculpt e é lento no llvmpipe. Veja o resultado de referência em `base/docs/building_cloud.md`.
