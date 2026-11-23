# EXCALIBUR-KANGAROO369

Solver de ECDLP (Pollard's Kangaroo) para secp256k1 — o **terceiro** projeto da
família, complementar ao EXCALIBUR-LOTTERY369 (brute/volante). Alvo: puzzles de
Bitcoin **com chave pública exposta**, onde o Kangaroo resolve o log discreto em
`≈ K·√W` passos (`K≈1,15` com simetria), em vez de forçar bruta `2^N`.

> Projeto de estudo/engenharia. Veja o design completo em
> `../EXCALIBUR-LOTTERY369/docs/KANGAROO_DESIGN.md` (análise volante × kangaroo).
> **Clean-room:** matemática EC reimplementada das fórmulas padrão, sem código GPL.

## Estado atual

| Fase | Descrição | Status |
|---|---|---|
| **0** | Matemática EC em CPU (`EcInt`, add/double afim, `MulModP` com `2^256 mod p`, `InvModP` Fermat, `MultiplyG`, compress/decompress) | ✅ **feito, 10/10 testes** |
| **1** | Inversão em lote (Montgomery) + `point_add_batch` | ✅ **feito, 64× — ~4M saltos/s/thread (~16M em 4 threads)** |
| **2** | Kangaroo 2-manadas (vOW, starts espalhados) + DP + recuperação | ✅ **feito — resolve 32/40/48 bits; K→3,35 no 48; 48 bits em ~15s/1 thread** |
| **3a** | Multithread (tabela de DP compartilhada entre workers) | ✅ **feito — 48 bits em ~7,9s/4 threads (~1,9×); 11 M saltos/s agregado** |
| **3b** | Simetria `±P` (negação) + escape de *fruitless cycles* | ✅ **feito — K=1,03 no 48 bits; 2,95× menos saltos; chave correta** |

> **3b (simetria `±P`) — implementado em `solve_sym`:** passo de negação bem-definido
> nas classes `{Q,−Q}` (`step(Q)=Q+sinal(y)·s·G`, `step(−Q)=−step(Q)`); intervalo
> centralizado em `[−N/2,N/2)`; distância **sinalizada**; recuperação testa os dois
> candidatos `k'=val_T−val_W` e `k'=−val_T−val_W`; 2-ciclos detectados (ponto == 2
> passos atrás) e escapados com salto determinístico por `x`. O `solve` original (sem
> simetria) foi **mantido intacto** para comparação. Resultado medido: K≈1,0–1,8
> (vs ~2–3,7 plain), no alvo SOTA (RCKangaroo K=1,15).
>
> ⚠️ **Medido só até 48 bits.** Acima de ~52 bits esse desenho travava (caminhadas presas
> em laços de auto-interseção) — **corrigido na Fase 6** (ver abaixo).

### Alcance (256-bit scalar tracking)
Distâncias/expoentes são rastreados em **inteiro de 256 bits** (`src/scalar.h`), então o
solver é **correto de 2 a 160 bits** — o limite restante é só **tempo** (hardware), não
largura de inteiro. É a *reference implementation* de CPU pronta pra espelhar num kernel
CUDA. Tempo é `≈ K·√(2^range)`; só vale a pena em CPU moderna/múltipla até ~70 bits.

**Prova dos 65–160 bits (`test_wide`):** 31 verificações multi-limb — ops de 256 bits
(carry/borrow/sinal entre os 4 limbs), `mul_g` de 96 e 160 bits conferido contra uma
implementação secp256k1 **independente** (Python), e a aritmética de recuperação da chave
acima de 2⁶⁴ (caminho plano e simetria centrada com valor negativo). Os demais testes do
solve só exercitam ≤48 bits (um único limb); este fecha o restante. `ctest` → 6/6 verde.

### Resumo de desempenho (48 bits, i5-3470)
| Config | Tempo | Ganho acumulado |
|---|---|---|
| Fase 2: 1 thread, plain | 15,15 s | — |
| Fase 3a: 4 threads, plain | 7,89 s | 1,9× |
| Fase 3b: 4 threads + simetria | **1,85 s** | **~8×** |
| 4 | Dispatcher + front-end de volante (`--first-allow` → sub-intervalos) | ✅ **feito** |
| 5 | Checkpoint/resume da tabela de DP | ✅ **feito** (`test_ckpt`, 14/14) |
| 6 | Simetria refeita como Gaudry–Schost (a 3b travava >52 bits) | ✅ **feito — 52 a 60 bits resolvem; `symmetry_54bit` no ctest** |

### Fase 5 — checkpoint/resume (à prova de reboot)

Corridas longas agora sobrevivem a pausa/reboot. O que persiste é a **tabela de pontos
distinguíveis** (o progresso acumulado) + contador de saltos + config, em formato binário
com **escrita atômica** (`tmp`+`fsync`+`rename`, modo `0600`) e **digest SHA-256** no fim
(detecta corrupção/adulteração). Em `--resume`, recarrega a tabela e a mesma tabela de
saltos (via seed salvo) e continua com um **salt de sessão novo** (caminhadas inéditas) até
a colisão. `Ctrl+C` grava um snapshot final e sai.

```bash
# grava progresso a cada 30s
./build/excalibur --puzzle 71 --pubkey <PUB> --address <ADDR> --threads 3 \
    --checkpoint p71.ckpt --checkpoint-interval 30
# retoma de onde parou (mesmo após reboot)
./build/excalibur --puzzle 71 --pubkey <PUB> --address <ADDR> --threads 3 --resume p71.ckpt
```

> Checkpoint vale no modo simetria (padrão). O arquivo guarda o escalar/valores em claro
> — mantenha-o fora do controle de versão (já ignorado via `*.ckpt` não versionado).

### Fase 6 — simetria confiável em qualquer range (Gaudry–Schost)

**Bug encontrado:** a simetria da Fase 3b só funcionava até ~52 bits. Escolher o sinal do
salto pela paridade de y torna o passo uma função da classe `{P,−P}` — mas transforma o
canguru num **passeio aleatório 1-D simétrico** em |dlog|. Esse passeio é *recorrente*:
volta a pontos **exatos** depois de ~σ^(2/3) passos (σ ≈ tamanho do salto), fechando
laços longos que re-batem DPs já conhecidos para sempre; e esse tempo cresce mais devagar
que o intervalo entre DPs (2^dp), então quanto maior o range, pior. Medido no 54 bits:
mais *re-hits* da própria manada do que DPs novos, **zero** colisões tame↔wild, e nenhuma
solução nem com 16×√W. É o que fez a corrida de 71 bits passar a noite com só 28 DPs.

**Correção — o jeito padrão de usar o mapa de negação num intervalo (Gaudry–Schost):**
1. depois de **cada** DP a caminhada recomeça num ponto aleatório uniforme da região da
   sua manada — um "teleporte" `t·G` pré-computado somado no próprio lote (custa uma
   adição, nenhuma multiplicação escalar);
2. trilha `2^dp` bem abaixo do tempo de auto-interseção (~1% das trilhas fecham laço);
3. ciclos de 2 e 4 escapados por um salto determinístico e invariante à classe, de uma
   tabela separada de saltos maiores (o escape não consegue reentrar no ciclo);
4. *backstop*: caminhada sem DP por `8·2^dp` passos também teleporta.

| range | Fase 3b | Fase 6 | K | re-hits | tempo (2 threads) |
|---|---|---|---|---|---|
| 52 | instável (K até 7) | ✅ chave correta | 1,99 | 1 | 20 s |
| 54 | ❌ nem com 16×√W | ✅ chave correta | 1,91 | 3 | 38 s |
| 56 | ❌ | ✅ chave correta | 1,22 | 0 | 49 s |
| 58 | ❌ | ✅ chave correta | 3,95 | 7 | 312 s |
| 60 | ❌ | ✅ chave correta | 1,41 | 3 | 224 s |

K médio medido ~2 (corridas únicas têm variância alta) — hoje a simetria empata com o
modo plano em saltos, mas é a única com checkpoint/resume; aproximar do K≈1,36 teórico do
Gaudry–Schost com negação é ajuste fino das regiões (trabalho futuro). O teste
`symmetry_54bit` (ctest) resolve o caso de 54 bits que a caminhada antiga nunca resolvia
e falha se a assinatura da armadilha (re-hits) voltar.

Checkpoints agora são **versão 2**: DPs da caminhada antiga nunca colidem com a nova, então
arquivos v1 são recusados — e o `--resume` avisa logo no início se não conseguir carregar.

## Build & teste

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
./build/test_ec        # ou: ctest --test-dir build
```

O teste confere `mul_g(k)` contra as pubkeys conhecidas dos puzzles 95–135 e
descomprime a pubkey do 140. Se passar, todo o stack de corpo+curva está correto.

**Validação com dados reais (`test_real` + `tests/puzzles.csv`):** o fixture tem os 83
puzzles resolvidos (1–135) com priv/endereço/pubkey. O teste (a) confere
`mul_g(priv)==pubkey` em TODAS as linhas e (b) roda o kangaroo com simetria nos puzzles
reais 40/44/48 a partir das pubkeys reais, recuperando as chaves privadas genuínas.
Fonte: `hex_puzzle_resolvido.md`. Família: **excalibur369** (key hunter base, `kh71`) →
**EXCALIBUR-LOTTERY369** (volante) → **EXCALIBUR-KANGAROO369** (este).

## CLI unificado (`excalibur`) — dispatcher

Front-end único que roteia pelo tipo de alvo:

```bash
# pubkey exposta -> KANGAROO nativo (intervalo exato do puzzle [2^(N-1),2^N))
./build/excalibur --puzzle 48 --pubkey 0291bee5...12debb --address 1DFYha... --threads 4
#   -> acha a chave, deriva WIF + endereço e AUTO-VERIFICA (matches --address: YES)

# só endereço (sem pubkey) -> BRUTE. Dois engines:
./build/excalibur --puzzle 71 --address 1AVJ... --first-allow 4a,5a --allow 00,0c   # exlottery (default)
./build/excalibur --puzzle 71 --address 1AVJ... --engine kh71 --mode reverse        # kh71 (base)
./build/excalibur --puzzle 71 --address 1AVJ... --engine kh71 --brute-args "--fixed on --fixed-count 3"
```

Roteia os **três** tools: pubkey→kangaroo nativo; endereço+`--engine exlottery`→volante
(`--first-allow`/`--allow`); endereço+`--engine kh71`→key hunter base (`--fixed*` via
`--brute-args`). `--found` vira `--output` no kh71. Paths overrideáveis com `--brute-bin`
/ `--kh71-bin`. Outras opções: `--range N`, `--threads`, `--seed`, `--no-sym`, `--dry-run`.
O kangaroo aceita **ranges de 2 a 160 bits** (distâncias em inteiro de 256 bits). Acima de
~64 bits imprime uma **estimativa de tempo honesta** e avisa que é inviável neste i5 —
pensado pra CPU mais rápida ou futura GPU (Ctrl-C a qualquer hora). Saída do kangaroo:
chave hex/decimal, **WIF comprimida**, endereço derivado e verificação contra `--address`.

## Decisões de design (resumo)

- **Kangaroo opera no nível do ponto EC** (alvo = pubkey). **Não** precisa de
  SHA256/RIPEMD160 no núcleo — compara pontos, não hash160.
- `--first-allow` (bits altos) **transfere** para kangaroo como `-start/-range`
  (sub-intervalos). `--allow` dos bytes do meio **não** transfere (erro de
  categoria — kangaroo não enumera candidatos). Detalhes no doc de design.
- **Hardware:** sem GPU NVIDIA aqui (i5-3470 + Intel iGPU). Este projeto valida a
  matemática/algoritmo em tamanho de brinquedo; puzzle real exige GPU no futuro.

## Licença
A definir. Como NÃO copiamos código do RCKangaroo (GPLv3), ficamos livres para
escolher a licença — ver nota no doc de design.
