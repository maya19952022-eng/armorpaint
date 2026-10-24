# Compilar e testar no cloud (Linux sem GPU)

Registro da fase P0 do plano de paridade. Ambiente onde os comandos foram executados: Ubuntu 24.04.5 LTS, x86_64, 4 vCPU, 15 GB de RAM, clang 18.1.3, sem GPU.

## Pacotes

Os mesmos da CI (`.github/workflows/linux_vulkan.yml`):

```bash
sudo apt-get update
sudo apt-get install -y make clang libvulkan-dev libgtk-3-dev libasound2-dev
```

Para **rodar** o app sem monitor e sem GPU (smoke test, scripts):

```bash
sudo apt-get install -y xvfb mesa-vulkan-drivers
```

`mesa-vulkan-drivers` traz o llvmpipe (Vulkan por software, Mesa 25.2 no Ubuntu 24.04). `vulkan-tools` (`vulkaninfo`) é opcional, só para diagnóstico. `base/docs/linux_deps.md` lista também `libssl-dev libxi-dev libxrandr-dev libxcursor-dev`; no Ubuntu 24.04 eles já vêm como dependência dos pacotes acima.

## Compilar

```bash
cd paint
../base/make --compile
```

- `base/make` chama o binário pré-compilado `base/tools/bin/linux_x64/amake`, que gera o makefile, compila os shaders `.shader` para SPIR-V e os assets, e roda `make -j`.
- Resultado: `paint/build/out/ArmorPaint` com os dados em `paint/build/out/data/`. `paint/build` está no `.gitignore`.
- Tempo: cerca de 35 s num build limpo; incremental recompila só o que mudou (o app é um unity build: `paint/sources/main.c` inclui todos os `.c` de `paint/sources`).
- Warnings num build limpo: **1**, pré-existente: `paint/sources/functions.h:868: declaration of 'struct ray' will not be visible outside of this function [-Wvisibility]` (vindo de `minic_api.c`). Regra 3 do plano: nenhum warning novo, então a contagem deve continuar 1.

Atalho que faz o build e conta os warnings:

```bash
scripts/ci_compile.sh --clean --max-warnings 1 --smoke
```

`scripts/ci_compile.sh` compila o alvo que o sistema atual consegue compilar (Linux → Vulkan, macOS → Metal, Windows com Git Bash → Direct3D12) e avisa que pulou os outros dois. Os três alvos só são compilados juntos na CI (`.github/workflows/`). Opções: `--clean` apaga `paint/build` antes; `--max-warnings N` falha acima de N warnings; `--smoke` roda o smoke test depois do build (Linux e Windows). O log fica em `paint/build_logs/<sistema>.log`.

## Rodar o app sem tela

```bash
export XDG_RUNTIME_DIR=$(mktemp -d)   # o Mesa reclama se não existir
cd paint/build/out
xvfb-run -a ./ArmorPaint --background --api | head   # imprime a API de scripts
```

O log mostra `Chosen Vulkan device: llvmpipe (LLVM 20.1.2, 256 bits)`. Com isso dá para rodar scripts C (`--script arquivo.c`), abrir projetos, pintar via `script_paint`, salvar e exportar. O llvmpipe anuncia `VK_KHR_acceleration_structure` e `VK_KHR_ray_tracing_pipeline`, mas path tracer e bakes por ray tracing não foram testados aqui.

## Smoke test

```bash
base/tests/paint_headless/run.sh
```

Abre `base/tests/paint_headless/fixtures/p0_project.arm`, confere 8 camadas e as médias dos canais exportados, cria uma camada, salva, reabre e exporta de novo. Resultado na P0: `38 checks, 0 failures`, cerca de 15 s, estável em 4 execuções seguidas. Detalhes em `base/tests/paint_headless/readme.md`.

O teste encontrou um bug real na P0: a opacidade de grupos se perdia ao reabrir o projeto (corrigido em `paint/sources/io/import_arm.c`).

## Teste de integração do upstream

`paint/assets/plugins/dev/test.c` (82 passos, 220 checks) também roda aqui:

```bash
cd paint/build/out
xvfb-run -a -s "-screen 0 1920x1080x24" stdbuf -oL ./ArmorPaint --script ../../assets/plugins/dev/test.c
rm -f data/config.json   # o teste deixa a config em modo Sculpt
```

Ele grava em `/tmp/armorpaint`, leva vários minutos no llvmpipe e deixa `data/config.json` em modo Sculpt, o que muda o comportamento das execuções seguintes. Por isso o smoke test do plano é separado e isola a config.

### Linha de base do test.c (P0, llvmpipe)

Duas execuções completas, mesmo resultado:

- passos 1 a 75 rodam; **198 checks ok**;
- 2 falhas: `self-reading stroke inverts the whole stroke (0.0000 of 0.0170 cyan)` e `duplicate has the content (0.001 of 0.001 blue, 1.00 red)`;
- o teste **trava no fim do passo 75** (`step_layer_mask_ops`): a thread principal fica no sleep de ociosidade do loop (`IDLE_SLEEP`) e o próximo frame do script não chega. Só termina pelo `timeout`.

Não foi investigado na P0 (é anterior às mudanças do plano e pode ser específico do llvmpipe). Use estes números como referência: uma fase que piora este resultado introduziu regressão.

## O que o cloud não valida

- Compilação para Windows (Direct3D12, HLSL via `d3dcompiler`) e macOS (Metal): o `amake` escolhe o backend de shader pelo sistema em que roda, então no Linux todo shader vira SPIR-V. A CI compila os três.
- Aparência na tela, desempenho e qualquer coisa que dependa de uma GPU real. Cada PR traz um roteiro de teste manual no Windows.

## Windows (dono do projeto)

Visual Studio 2022 com a carga "Desenvolvimento para desktop com C++" e o Windows SDK:

```bat
cd paint
..\base\make --compile
base\tests\paint_headless\run.bat
```

O executável fica em `paint\build\out\ArmorPaint.exe`.
