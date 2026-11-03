# Lista de Materiais (BOM)

Componentes para **uma unidade** do RetroSC Pong. As referências (R1, C2, J4…)
são as mesmas do esquemático, da serigrafia da placa e dos arquivos KiCad —
use-as para conferir cada peça no lugar certo.

> **Qual modelo comprar** de potenciômetro, manopla e demais peças da caixa
> (que duram num arcade, e por quê): [pecas-do-gabinete.md](pecas-do-gabinete.md).
> Aqui ficam as **quantidades e as referências**.

## 1. Componentes da placa

Tudo aqui vai **soldado na PCB** ([kicad/README.md](../kicad/README.md)).

| Ref | Qtd | Componente | Valor / especificação | Função |
| --- | --: | ---------- | --------------------- | ------ |
| R1 | 1 | Resistor 1/4 W | **470 Ω** | DAC de vídeo — sync |
| R2 | 1 | Resistor 1/4 W | **270 Ω** | DAC de vídeo — vídeo |
| R3 | 1 | Resistor 1/4 W | **1 kΩ** | filtro RC do áudio (com C1) |
| R4 | 1 | Resistor 1/4 W | **10 kΩ** | divisor de linha — série |
| R5 | 1 | Resistor 1/4 W | **1 kΩ** | divisor de linha — para GND |
| R6, R7 | 2 | Resistor 1/4 W | **1 kΩ** | entradas L e R do amplificador |
| R8 | 1 | Resistor 1/4 W | **4,7 kΩ** | pull-up externo do SELETOR (GP22 → 3V3) |
| R9 | 1 | Resistor 1/4 W | **220 Ω** | SELETOR em série — limita ESD vinda do painel |
| C1 | 1 | Capacitor cerâmico | **100 nF** | filtro RC do áudio (com R3) |
| C2 | 1 | Capacitor **eletrolítico** | **1 µF** | acoplamento DC — **polarizado, + para o lado do GP18** |
| C3, C4 | 2 | Capacitor cerâmico | **100 nF** | filtro dos ADCs (um por potenciômetro) |
| C5 | 1 | Capacitor cerâmico | **100 nF** | filtro do SELETOR (GP22 → GND), junto ao J7 |
| U1 | 1 | Módulo RP2040 | Pico, YD de 3 botões ou "roxa" de 1 botão | **a variante da PCB depende do módulo** — ver [pinout.md](pinout.md) |
| U2 | 1 | Módulo amplificador | **PAM8403 (HW-012)**, 2×3 W, 5 V | com potenciômetro de volume embutido |
| J1 | 1 | Jack RCA fêmea | de painel, ângulo reto, THT | vídeo composto |
| J8, J9 | 2 | Jack RCA fêmea | de painel, ângulo reto, THT | áudio L e R |
| J4 | 1 | Barra de pinos 1×6 | passo 2,54 mm | chave de áudio A/B |
| J5, J6 | 2 | Barra de pinos 1×3 | passo 2,54 mm | potenciômetros P1 e P2 |
| J7 | 1 | Barra de pinos 1×2 | passo 2,54 mm | botão SELETOR |

> Somando por valor, para a compra: **4 × 1 kΩ** (R3, R5, R6, R7),
> **4 × 100 nF** (C1, C3, C4, C5), 1 × 470 Ω, 1 × 270 Ω, 1 × 220 Ω,
> 1 × 4,7 kΩ, 1 × 10 kΩ e 1 × 1 µF.
>
> **R8, R9 e C5 são o filtro do botão SELETOR** — por que ele existe está em
> [schematic.md](schematic.md#botão-seletor). Não deixe de montá-los: sem o
> pull-up externo o GP22 fica pendurado só no pull-up interno de ~60 kΩ e uma
> fuga qualquer para o terra o põe em cima do limiar, o que o jogo lê como
> pressões que ninguém deu.
>
> **Os jacks RCA e o módulo do amp usam footprints feitos à mão** a partir de
> medidas das peças reais — confira o encaixe antes de mandar fabricar
> (imprima o **desenho de fabricação** em 1:1 e sobreponha as peças).

## 2. Peças de painel

Não vão na placa: ficam no gabinete e chegam à PCB por **fio**, nos headers
J4–J7. A função de cada pino está na serigrafia.

| Ref | Qtd | Componente | Especificação | Liga em |
| --- | --: | ---------- | ------------- | ------- |
| RV1, RV2 | 2 | Potenciômetro | **10 kΩ linear (tipo B), com batente** — [qual comprar](pecas-do-gabinete.md#potenciômetros--escolha-por-durabilidade) | J5 e J6 |
| SW1 | 1 | Botão push NA | momentâneo, 12 mm | J7 |
| SW2 | 1 | Chave **DPDT** ON-ON | 2 polos / 6 pinos (serve uma seletora de tensão) | J4 |
| — | 2 | Manopla (knob) | Ø ~32 mm × ~20 mm, furo 6 mm — [como escolher](pecas-do-gabinete.md#knobs--manoplas) | eixo dos pots |
| — | 1 | Alto-falante | 4–8 Ω, 3 W | plugado num RCA de áudio no modo B |

> A chave **SW2 seleciona o que sai pelos RCAs de áudio**: posição A = nível de
> linha para a TV; posição B = saídas do amplificador para caixa passiva. Ver
> [schematic.md](schematic.md#chave-ab--rcas-de-áudio-linha-p-tv-ou-saídas-do-amp)
> — inclusive os **cuidados obrigatórios** do modo B.

## 3. Montagem e cabos

| Qtd | Item | Especificação | Observação |
| --: | ---- | ------------- | ---------- |
| 1 | **PCB** | 2 camadas, 80 × 66 mm, HASL | envie o ZIP de gerbers da variante certa |
| 1 | **Caixa Patola PB-085/3** | 85 × 73 × 32 mm | a placa foi dimensionada para ela; furos H1/H2 caem nos bossos da tampa |
| 2 | Parafuso auto-atarraxante | ~ø2,9 × 6 mm | prende a placa nos bossos da tampa (furo-guia ø2,5) |
| 1 | Conector fêmea 1×6 | passo 2,54 mm | cabo da chave A/B (J4) |
| 1 | Conector fêmea 1×7 | passo 2,54 mm | **serve os dois pots de uma vez** (J5+J6, com o contato do meio vago) |
| 1 | Conector fêmea 1×2 | passo 2,54 mm | cabo do botão (J7) |
| 1 | Cabo USB | micro-USB (Pico) ou **USB-C** (clones) | alimentação e gravação |
| — | Fio AWG 24–26 flexível | | painel → headers |

> Sem PCB dá para montar tudo em placa universal de 2,54 mm, como foi o
> protótipo — a pinagem é a mesma.

## Substituições aceitáveis

- **Filtro do SELETOR**: o pull-up (R8) aceita qualquer coisa entre 3,3 kΩ e
  10 kΩ, e o resistor série (R9) entre 100 Ω e 470 Ω — são valores de ordem de
  grandeza, não de precisão. O **C5 de 100 nF** é o único a respeitar: é ele
  que define a constante de tempo de ~0,5 ms contra o repique do botão.
- **Resistores DAC**: 220 Ω no lugar do 270 Ω também funciona (branco ~1,1 V,
  um pouco "quente" — o 270 Ω foi validado na TV real com imagem melhor).
  Evite 1 kΩ + 470 Ω: branco despenca para ~0,6 V (imagem escura).
- **Amplificador**: LM386 com 8 componentes externos, TDA2030, ou módulos
  prontos baseados em PAM8302/PAM8403.
- **Módulo RP2040 (U1)**: além do Pico oficial, o projeto tem placa para a
  YD-RP2040 de 3 botões e para a "roxa" de 1 botão. Outros clones funcionam se
  expuserem GP16–18, GP22, GP26 e GP27 — mas confira a **posição física** de
  cada um em [pinout.md](pinout.md): elas variam entre clones, e é isso que
  decide qual variante de PCB fabricar.

## Custo estimado (Brasil, 2026)

- RP2040 board: R$ 35–60
- Pots + manoplas: R$ 15
- Amp + alto-falante: R$ 25
- Resistores / capacitores / botão / RCA / headers: R$ 15
- PCB (5 un. em fábrica china, sem frete): ~R$ 10/un.
- **Total eletrônico: ~R$ 100–125** (sem TV e gabinete)
