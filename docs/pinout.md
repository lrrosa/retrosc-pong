# Pinout — módulos RP2040 suportados

O projeto funciona com três módulos RP2040. Eles têm a **mesma furação**
(2 fileiras de 20 pinos, 2,54 mm), mas **pinagens diferentes** — por isso o
repositório tem **duas placas** (ver [../kicad/README.md](../kicad/README.md)):

| Módulo | Botões | Placa a fabricar |
| --- | --- | --- |
| Raspberry Pi Pico (oficial) | 1 (BOOTSEL) | **oficial** (`retrosc-pong`) |
| YD-RP2040 / VCC-GND Studio (preta, USB-C) | 3 (BOOT/USR/RESET) | **oficial** (`retrosc-pong`) — pinagem ≈ Pico |
| RP2040 "roxa" (USB-C, 16 MB) | 1 (BOOTSEL) | **variante YD** (`retrosc-pong-yd`) |

> **Confira antes de soldar:** cada placa tem marcas **GP17/GP18** no silk ao
> lado dos furos correspondentes. Compare com os rótulos do seu módulo — se
> não baterem, o módulo é da outra variante.

**Posições físicas** na numeração padrão do Pico: pino 1 no canto do USB,
1–20 descem a coluna esquerda, 21–40 sobem a direita.

## Sinais usados pelo projeto (posição física em cada módulo)

| Função | GPIO | Dir | Pico oficial | YD 3 botões | Roxa 1 botão | Observações |
| --- | --- | --- | --- | --- | --- | --- |
| NTSC SYNC | GP16 | OUT | 21 | 21 | **19** | resistor 470 Ω → composto |
| NTSC VIDEO | GP17 | OUT | 22 | 22 | **20** | resistor 270 Ω → composto |
| AUDIO PWM | GP18 | OUT | 24 | 24 | **21** | filtro RC + amp |
| SELETOR | GP22 | IN | 29 | 29 | **25** | pull-up interno + filtro na placa (R8/R9/C5) |
| POT P1 | GP26/ADC0 | AIN | 31 | 31 | **30** | wiper do pot esquerdo |
| POT P2 | GP27/ADC1 | AIN | 32 | 32 | **31** | wiper do pot direito |
| AGND | — | — | 33 | 33 | **29** | retorno dos pots |
| 3,3 V | — | — | 36 | 36 | 36 | extremos dos potenciômetros |
| GND | — | — | 3,8,13,18,23,28,38 | 3,8,13,18,23,28,38 | **6,15,35,38** | massa comum |
| +5 V | — | — | 40 (VBUS) | 40 (Vout) | 40 (VBUS) | alimenta o PAM8403 |

Os GPIOs estão fixados em [src/config.h](../src/config.h) — o **firmware é o
mesmo** para os três módulos (o RP2040 é idêntico; só muda em qual furo da
placa cada GPIO aparece, e isso é resolvido pelo roteamento de cada variante).

## Pinagem completa dos três módulos

| Pos | Pico oficial | YD 3 botões | Roxa 1 botão |
| --- | --- | --- | --- |
| 1 | GP0 | GP0 | GP0 |
| 2 | GP1 | GP1 | GP1 |
| 3 | GND | GND | GP2 |
| 4 | GP2 | GP2 | GP3 |
| 5 | GP3 | GP3 | GP4 |
| 6 | GP4 | GP4 | GND |
| 7 | GP5 | GP5 | GP5 |
| 8 | GND | GND | GP6 |
| 9 | GP6 | GP6 | GP7 |
| 10 | GP7 | GP7 | GP8 |
| 11 | GP8 | GP8 | GP9 |
| 12 | GP9 | GP9 | GP10 |
| 13 | GND | GND | GP11 |
| 14 | GP10 | GP10 | GP12 |
| 15 | GP11 | GP11 | GND |
| 16 | GP12 | GP12 | GP13 |
| 17 | GP13 | GP13 | GP14 |
| 18 | GND | GND | GP15 |
| 19 | GP14 | GP14 | **GP16** |
| 20 | GP15 | GP15 | **GP17** |
| 21 | **GP16** | **GP16** | **GP18** |
| 22 | **GP17** | **GP17** | GP19 |
| 23 | GND | GND | GP20 |
| 24 | **GP18** | **GP18** | GP21 |
| 25 | GP19 | GP19 | **GP22** |
| 26 | GP20 | GP20 | GP23 |
| 27 | GP21 | GP21 | GP24 |
| 28 | GND | GND | GP25 |
| 29 | **GP22** | **GP22** | **AGND** |
| 30 | RUN | RUN | **GP26**/A0 |
| 31 | **GP26**/ADC0 | **GP26**/ADC0 | **GP27**/A1 |
| 32 | **GP27**/ADC1 | **GP27**/ADC1 | GP28/A2 |
| 33 | **AGND** | **AGND** (GND) | GP29/A3 |
| 34 | GP28/ADC2 | GP28/ADC2 | RUN |
| 35 | ADC_VREF | GP29/ADC3 | GND |
| 36 | **3V3** | **3V3** | **3V3** |
| 37 | 3V3_EN | GP23 (WS2812) | 3V3_EN |
| 38 | **GND** | **GND** | **GND** |
| 39 | VSYS | Vin | VIN |
| 40 | **VBUS (+5V)** | **Vout (+5V)** | **VBUS (+5V)** |

Em **negrito**, as posições que o projeto conecta. Pinos extras fora das
fileiras (SWD na ponta oposta ao USB) não são usados: a gravação é pelo USB.

### YD de 3 botões na placa oficial — ressalvas (inofensivas)

- Posições **35, 37 e 39** diferem do Pico, mas ficam **sem conexão** na
  placa — nenhum conflito.
- O firmware sobe o **GPIO23** (modo PWM do regulador do Pico oficial). No
  YD de 3 botões, GP23 é o dado do LED endereçável (WS2812): nível constante
  não aciona o LED — sem efeito e sem dano.
- O pino 40 é **Vout** (5 V do USB via proteção da placa) em vez de VBUS
  direto — alimenta o PAM8403 normalmente.

## Por que estes GPIOs?

- **GP16/17** ficam adjacentes e permitem usar 2 pinos consecutivos como
  saída do PIO (sync + dados), o que simplifica o programa PIO.
- **GP26/27** são os únicos ADC com baixo ruído (GP28 também serve mas
  costuma ser usado para outros fins). Os capacitores de 100 nF entre wiper
  e GND filtram ruído de comutação do conversor.
- **GP18** está livre de DMA crítico e é PWM canal A do slice 1.
- **GP22** é o pino físico imediatamente após GP21 (não usado), boa
  ergonomia de cabeamento. O botão **não chega direto nele**: entre o header
  J7 e o GP22 a placa tem 220 Ω em série (R9), e o pino leva um pull-up
  externo de 4,7 kΩ (R8) e 100 nF para o terra (C5) — ver
  [schematic.md](schematic.md#botão-seletor).
