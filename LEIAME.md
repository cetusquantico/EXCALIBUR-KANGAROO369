# EXCALIBUR-KANGAROO369

*[English version: README.md](README.md)*

Solver de logaritmo discreto em intervalo (**Pollard's kangaroo**) para secp256k1, escrito
do zero (clean-room) em C++17. Alvo: o desafio público dos **puzzles de Bitcoin** no caso
em que a **chave pública do puzzle está exposta** — aí o canguru recupera a chave privada
em cerca de `K·√W` operações de grupo, em vez de testar as `W` chaves do intervalo.

É uma implementação de referência em CPU — correta, testada e documentada — pensada para
servir de base a um port para GPU, não para bater recorde de velocidade.

> **Clean-room:** a aritmética de curva elíptica foi implementada a partir das fórmulas
> padrão; nenhum código GPL foi copiado (em particular, nada do RCKangaroo).

## Resultado em dado real

O **puzzle 68** (já resolvido publicamente) foi recuperado a partir da chave pública
exposta, pelo CLI, com checkpoint ativo:

```
private (hex)     : ...0bebb3940cd0fc1491   (confere com a solução publicada)
matches --address : YES
jumps=2.04e10  K=1.68  threads=2  time=2996s       (Intel i5-3470, ~50 min)
```

## Fases

| Fase | Descrição | Status |
|---|---|---|
| 0 | Aritmética secp256k1 do zero: corpo, soma/dobra afim, inversão de Fermat, `mul_g`, (des)compressão | ✅ |
| 1 | Inversão em lote (truque de Montgomery) + `point_add_batch` | ✅ ~64× mais barato que somas isoladas |
| 2 | Canguru de 2 manadas (van Oorschot–Wiener) + pontos distinguidos (DP) | ✅ |
| 3a | Multithread com tabela de DP compartilhada | ✅ |
| 3b | Simetria `±P` (mapa de negação), primeira versão | ⚠️ só funcionava até ~52 bits — substituída pela Fase 6 |
| 4 | CLI unificado `excalibur` (chave pública → canguru; só endereço → motor externo) | ✅ |
| 5 | Checkpoint/resume da tabela de DP | ✅ |
| 6 | Simetria refeita como caminhadas Gaudry–Schost | ✅ confiável em qualquer range |

Distâncias e expoentes são inteiros de **256 bits** (`src/scalar.h`), então o solver é
correto de **2 a 160 bits** — o limite é só o tempo. Velocidade medida: ~3,4 M saltos/s
por núcleo num i5-3470 (Ivy Bridge).

## Fase 6 — por que a simetria travava, e o conserto

A primeira versão simétrica escolhia o sinal do salto pela paridade de `y`. Isso torna o
passo uma função da classe `{P, −P}`, como o mapa de negação exige — mas transforma o
canguru num **passeio aleatório 1-D simétrico** em `|dlog|`. Esse passeio é *recorrente*:
depois de ~`σ^(2/3)` passos (`σ` ≈ tamanho do salto) ele volta a um ponto **exato** já
visitado e fecha um laço longo que re-bate DPs já conhecidos para sempre. Esse tempo
cresce mais devagar que o intervalo entre DPs (`2^dp`), então quanto maior o range, pior.

Medido no 54 bits, com instrumentação: mais *re-hits* da própria manada do que DPs novos,
**zero** colisões tame↔wild e nenhuma solução nem com `16·√W` saltos.

O conserto usa o mapa de negação do jeito padrão para um intervalo (Gaudry–Schost):

1. depois de **cada** DP a caminhada recomeça num ponto aleatório uniforme da região da
   sua manada — um "teleporte" `t·G` pré-computado, somado no próprio lote (custa uma soma
   em lote, nenhuma multiplicação escalar);
2. trilha de `2^dp` passos bem abaixo do tempo de auto-interseção (~1% das trilhas fecham
   laço);
3. ciclos de 2 e 4 escapados por um salto determinístico e invariante à classe, de uma
   tabela separada de saltos maiores (o escape não consegue cair de volta no ciclo);
4. *backstop*: caminhada sem DP por `8·2^dp` passos também teleporta.

| range (bits) | primeira versão simétrica | Gaudry–Schost | K | re-hits | tempo (2 threads) |
|---|---|---|---|---|---|
| 52 | instável (K até 7) | ✅ chave correta | 1,99 | 1 | 20 s |
| 54 | ❌ nem com 16·√W | ✅ chave correta | 1,91 | 3 | 38 s |
| 56 | ❌ | ✅ chave correta | 1,22 | 0 | 49 s |
| 58 | ❌ | ✅ chave correta | 3,95 | 7 | 312 s |
| 60 | ❌ | ✅ chave correta | 1,41 | 3 | 224 s |
| 67 (puzzle 68) | — | ✅ chave correta | 1,68 | — | ~50 min |

K médio medido ≈ 2 (corridas únicas variam bastante). Hoje a simetria empata com o modo
plano em saltos, mas é a que tem checkpoint/resume; chegar perto do ~1,36 teórico do
Gaudry–Schost com negação é ajuste fino das regiões (trabalho futuro). O teste
`symmetry_54bit` protege contra a regressão.

## Fase 5 — checkpoint/resume (à prova de reboot)

O que persiste é a **tabela de pontos distinguidos** (o progresso acumulado), o contador de
saltos e a configuração, em formato binário com **escrita atômica** (`tmp` + `fsync` +
`rename`, modo `0600`) e **digest SHA-256** no fim (detecta corrupção). No `--resume`, a
tabela é recarregada com as mesmas tabelas de salto (via seed salvo) e a busca continua com
caminhadas novas. `Ctrl+C` grava um snapshot final e sai. O formato atual é a **versão 2**;
arquivos da versão 1 vêm da caminhada antiga e são recusados com aviso logo no início.

## Build e testes

Precisa de um compilador C++17 (g++ ou clang++), CMake ≥ 3.10 e pthreads — nenhuma outra
dependência.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

O build Release usa `-march=native`: o binário fica afinado para a máquina que compilou (e
só é garantido nela).

| teste | o que confere |
|---|---|
| `ec_vectors` | aritmética da curva; `mul_g(k)` contra as chaves públicas dos puzzles 95–135; descompressão |
| `batch_correctness` | somas em lote iguais às somas isoladas |
| `kangaroo_solve` | canguru plano em ranges sintéticos |
| `kangaroo_symmetry` | plano × simétrico em 40/44/48 bits |
| `real_puzzles` | todas as linhas de `tests/puzzles.csv` (`mul_g(priv) == pubkey`, endereço) e solução dos puzzles reais 40/44/48 a partir das chaves públicas |
| `wide_160bit` | aritmética multi-limb e recuperação de chave acima de 2^64, até 160 bits |
| `checkpoint_resume` | ida e volta save/load, detecção de adulteração, pausa → resume → chave correta |
| `symmetry_54bit` | resolve um caso de 54 bits que a primeira versão simétrica nunca resolvia, e falha se a assinatura da armadilha (re-hits) voltar |

`tests/puzzles.csv` lista os puzzles **já resolvidos** (as chaves já são públicas) e serve
de dado de resposta conhecida.

## Uso

```bash
# chave pública exposta -> canguru no intervalo exato do puzzle [2^(N-1), 2^N)
./build/excalibur --puzzle 68 --pubkey <PUBKEY_COMPRIMIDA_HEX> --address <ENDERECO_P2PKH> \
    --threads 2 --checkpoint p68.ckpt --checkpoint-interval 30

# retomar depois de pausa ou reboot
./build/excalibur --puzzle 68 --pubkey <...> --address <...> --threads 2 --resume p68.ckpt

# só mostrar o plano e a estimativa de tempo
./build/excalibur --puzzle 68 --pubkey <...> --address <...> --dry-run
```

Outras opções: `--range N` (range próprio em bits), `--threads N`, `--seed N` e `--no-sym`
(canguru plano; nesse modo não há checkpoint). O CLI imprime uma estimativa de tempo
honesta e avisa quando a corrida está fora do alcance da máquina.

**Alvos só com endereço.** Sem chave pública o canguru não se aplica, então o CLI entrega o
alvo a um motor externo de força bruta (`exlottery` por padrão, ou `--engine kh71`). Esses
motores são **ferramentas separadas e não fazem parte deste repositório**; o CLI procura
por eles ao lado do próprio binário, depois no `PATH`, ou recebe `--brute-bin` /
`--kh71-bin`. `--first-allow`, `--allow`, `--mode`, `--found` e `--brute-args` são
repassados ao motor.

## Limites

Isto roda em CPU. Os puzzles ainda abertos com chave pública exposta estão muito além do
alcance de CPU, e mesmo uma GPU sozinha concorre com grandes pools. O valor aqui é uma
referência correta, testada e documentada que um kernel de GPU pode espelhar.

## Uso pretendido

Para o desafio público dos puzzles de Bitcoin e para o estudo de algoritmos de logaritmo
discreto em intervalo. Use apenas em chaves suas ou em alvos publicados para serem
resolvidos. Arquivos de checkpoint guardam escalares em claro — mantenha-os privados. Ver
[SECURITY.md](SECURITY.md).

## Apoie

Trabalho de tempo livre, bancado por mim. O próximo passo grande — a versão GPU do
kangaroo — precisa de hardware. Se isto te for útil e quiser ajudar, doações em Bitcoin são
bem-vindas, sem promessa de retorno:

`bc1qhsq406jt6ghkzmhpnqe4r2s639t6mtrhj6zx03`

## Licença

[MIT](LICENSE).
