// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Leonardo Roman da Rosa
#ifndef PONG_CONFIG_H
#define PONG_CONFIG_H

// ===== Pinos =====
// Video composto: DAC resistivo de 2 bits.
//   SYNC_PIN  --[470 ohm]--+
//   VIDEO_PIN --[270 ohm]--+---- RCA center (composite out)
//                          +---- 75 ohm  ---- GND (terminacao da TV)
#define NTSC_SYNC_PIN     16
#define NTSC_VIDEO_PIN    17

// Audio: PWM em um GPIO, filtro RC, depois amplificador
//   AUDIO_PIN --[1k]--+---- entrada do amp
//                     |
//                    100nF
//                     |
//                    GND
#define AUDIO_PIN         18

// Botao SELETOR (push-button para GND, pull-up interno habilitado).
// Abre o menu no attract, escolhe o modo e confirma as iniciais.
#define SELETOR_BUTTON_PIN  22

// Potenciometros 10K (ADC). Wiper para o GPIO, extremos para 3V3 e GND.
//   GPIO 26 = ADC0 = P1
//   GPIO 27 = ADC1 = P2
// (10K esta folgado: o ADC do RP2040 tem impedancia de entrada > 100k e
//  dispensa buffer para sinais DC -- datasheet RP2040 secao 4.9.2.)
#define POT_P1_GPIO       26
#define POT_P2_GPIO       27
#define POT_P1_ADC        0
#define POT_P2_ADC        1

// Pino de controle do SMPS da placa Pico (GPIO23 = "PS"). Colocar em nivel
// alto forca o regulador em modo PWM, reduzindo o ripple no supply do ADC e
// deixando as leituras dos pots mais estaveis (datasheet do Pico, secao 4.3).
// Num arcade alimentado pela tomada/USB a perda de eficiencia e irrelevante.
#define PICO_SMPS_PS_PIN  23

// ===== Video =====
#define FB_WIDTH          256
#define FB_HEIGHT         192
#define FB_STRIDE_WORDS   (FB_WIDTH / 32)   // 8 words por linha
#define FB_WORDS          (FB_STRIDE_WORDS * FB_HEIGHT)

#define LINES_PER_FRAME   262
#define LINES_VSYNC       3
// LINES_TOP_BLANK posiciona a imagem verticalmente (mais = imagem mais baixa).
// 35 centraliza numa TV real (padrao NTSC/240p). No simulador Wokwi a "janela
// visivel" do wokwi-tv e deslocada para cima -- la o valor que centraliza e
// ~55. A TV real e o alvo do projeto; no Wokwi a imagem fica um pouco alta.
#define LINES_TOP_BLANK   35
#define LINES_ACTIVE      FB_HEIGHT          // 192
#define LINES_BOT_BLANK   (LINES_PER_FRAME - LINES_VSYNC - LINES_TOP_BLANK - LINES_ACTIVE)

// ===== Audio =====
#define AUDIO_SAMPLE_RATE 22050
// O PWM do audio e usado como gerador de tom (onda quadrada de 50%), entao a
// resolucao de duty nao importa -- o que importa e ate onde ele desce em
// frequencia. A nota sai em sysclk/(clkdiv * (TOP+1)) e o clkdiv para em 255,
// entao com TOP de 10 bits o mais grave possivel eram 478 Hz: a raquete
// (226 Hz) e a parede (246 Hz), os dois sons mais tocados do jogo, saiam ambos
// grudados nesse fundo, iguais entre si e emendando um no outro. Com 12 bits o
// piso cai para 120 Hz e cada som volta para a nota pedida.
#define PWM_TOP           4095               // 12-bit: alcanca as notas graves

// ===== Jogo =====
#define PHASE_WIN_SCORE   9                  // pontos para vencer UMA fase
#define PADDLE_W          3
#define PADDLE_H          24
#define BALL_SIZE         3
#define PADDLE_MARGIN     6                  // distancia das raquetes ate as bordas
#define BALL_SPEED_INIT_Q 0x180              // velocidade inicial (1.5 px/frame em Q8)
#define BALL_SPEED_MAX_Q  0x500              // velocidade max (5 px/frame em Q8)
#define BALL_SPEED_STEP_Q 0x020              // incremento por rebatida
#define ATTRACT_TIMEOUT_S 20                 // segundos antes de voltar a attract
#define MENU_TIMEOUT_S    15                 // menu sem input volta pro attract
// Nenhuma tela de espera pode ficar parada para sempre num arcade: passados
// esses segundos, a de iniciais grava o que foi digitado e a pausa aplica a
// opcao destacada.
// A das iniciais e generosa de proposito: digitar 3 letras girando um pot leva
// tempo, e na maquina real 30 s cortavam o jogador no meio do nome.
#define INITIALS_TIMEOUT_S 60
// A letra segue a posicao do pot, mas com freio. O alfabeto inteiro cabe no
// curso do pot, o que da ~10 graus por letra: sem freio, um esbarrao no botao
// passa cinco letras de uma vez e a letra desejada fica para tras. A banda da
// letra atual e alargada em INITIALS_HIST contagens para cada lado (histerese,
// que tambem impede o pisca-pisca na fronteira entre duas) e a mudanca anda no
// maximo uma letra a cada INITIALS_STEP_FRAMES, o que transforma um giro rapido
// numa rolagem legivel em vez de um borrao.
#define INITIALS_STEP_FRAMES 6
#define INITIALS_HIST        78                  // metade da banda de uma letra
#define PAUSE_TIMEOUT_S    30
// Na pausa a escolha anda pelo MOVIMENTO do pot, nao pela posicao dele: o item
// inicial tem que ser sempre CONTINUAR, senao um pot parado embaixo abriria a
// pausa ja com SAIR DO JOGO destacado.
#define PAUSE_POT_STEP    400                // contagens de ADC para trocar de item
// Ao voltar da pausa a raquete NAO pula para onde o pot esta: ela fica parada
// (piscando) ate o pot voltar a menos de PADDLE_TAKEOVER_TOL px da posicao em
// que a partida parou. Sem isso, pausar e girar o pot recuperava uma bola que
// ja estava perdida.
#define PADDLE_TAKEOVER_TOL 6                // px de tolerancia para retomar

// ===== Fases =====
// Tijolos: colunas verticais de tijolos de BRICK_W x BRICK_H. BRICK_H tem que
// dividir FB_HEIGHT (192) e o numero de linhas tem que caber num uint32_t.
#define BRICK_W           4
#define BRICK_H           8
#define BRICK_ROWS        (FB_HEIGHT / BRICK_H)   // 24 linhas

// MURALHA: a parede nasce com alguns vaos ja abertos -- MURALHA_CHEIOS fileiras
// de tijolo para cada MURALHA_VAZIOS de passagem. Fechada de todo a fase ficava
// arrastada (medido: 336 s contra 258 s assim), e vaos de 16 px sao folgados
// para a bola de 3 px sem deixar de ser uma parede: sobram 60% dela de pe.
#define MURALHA_CHEIOS    3
#define MURALHA_VAZIOS    2

// Fase TRIPLO: cada raquete vira 3 pedacos de PADDLE_H/3 separados por um vao.
#define TRIPLE_SEG_H      (PADDLE_H / 3)          // 8 px por pedaco
#define TRIPLE_GAP        8                       // vao entre os pedacos

// BONUS: o mascote da RetroSC (16x16) cruza a quadra na diagonal de tempos em
// tempos nas fases marcadas com PF_TEM_BONUS -- as vezes de cima para baixo, as
// vezes de baixo para cima -- com a palavra BONUS piscando junto. Acerta-lo da
// BONUS_POINTS pontos, so no total geral, a quem rebateu a bola por ultimo.
#define BONUS_W            16                     // = RETROSC_MASCOTE_W
#define BONUS_H            16
// Devagar de proposito: o mascote e alvo, nao obstaculo. Cada ponto marcado o
// tira da tela (phase_round_reset -> bonus_sleep), entao quem limita a chance
// de acerta-lo e a duracao do ponto, nao a travessia inteira -- medido no
// simulador, cair de 0,75 para 0,375 px/frame levou a passagem de 3,2 s para
// 4,6 s na tela e o acerto de 18% para 26% das passagens.
#define BONUS_VY_Q         0x060                   // 0,375 px/frame na vertical
#define BONUS_VX_MIN_Q     0x020                   // inclinacao minima (0,125 px/frame)
#define BONUS_VX_MAX_Q     0x060                   // inclinacao maxima (0,375 px/frame)
#define BONUS_X_MIN        40                      // faixa horizontal onde ela anda
#define BONUS_X_MAX        (FB_WIDTH - 40 - BONUS_W)
#define BONUS_WAIT_MIN     (4 * 60)                // 4 s de intervalo, no minimo
#define BONUS_WAIT_RANGE   (8 * 60)                // ate +8 s sorteados
#define BONUS_PASSES_MAX   2                       // aparicoes por fase
#define BONUS_POINTS        3                       // pontos, so no total geral
#define TOTAL_FLASH_FRAMES 90                     // total piscando apos o bonus

// Acertar o mascote sorteia um dos cinco bonus de bonus_tipo_t. O de pontos e
// instantaneo; os outros quatro duram BONUS_EFEITO_FRAMES e vao para quem
// rebateu a bola por ultimo -- inclusive a CPU, que joga com as mesmas armas.
#define BONUS_EFEITO_FRAMES (10 * 60)             // 10 s de efeito
#define BONUS_AVISO_FRAMES  240                   // 4 s com o nome do bonus na tela
#define PADDLE_H_BIG       (PADDLE_H * 3 / 2)     // raquete aumentada: 36 px
#define TRIPLE_SEG_H_BIG   (TRIPLE_SEG_H * 3 / 2) // no TRIPLO cada pedaco cresce igual
// Escudo: coluna de tijolos quebraveis na frente do proprio gol, com vaos por
// onde a bola ainda passa -- ESCUDO_CHEIO fileiras de tijolo a cada
// ESCUDO_PERIODO, ou seja, 24 px de muro e 8 px de vao.
#define ESCUDO_PERIODO      4
#define ESCUDO_CHEIO        3
// Turbo: enquanto o bonus durar, cada toque de quem o pegou lanca a bola
// acelerada por TURBO_HOLD_FRAMES; passado esse tempo ela volta sozinha a
// velocidade normal e so acelera de novo no proximo toque dele.
#define TURBO_HOLD_FRAMES   90                    // 1,5 s de bola rapida por toque
#define TURBO_EXTRA_Q      0x180                  // +1,5 px/frame
// TETO DURO, nao estetico: a colisao com a raquete e com o tijolo e por
// sobreposicao no instante, sem varredura. Com bola e raquete de 3 px, a bola
// atravessa a raquete sem tocar nela a partir de 6 px/frame; o tijolo de 4 px,
// a partir de 7. A bola turbinada tem que ficar abaixo dos 6.
#define TURBO_MAX_Q        0x580                  // 5,5 px/frame

// Fase NAVE: a nave sobe e desce no meio da quadra atirando; o tiro que pega a
// raquete deixa ela pela metade por SHRINK_FRAMES. A bola tambem rebate nela,
// entao e obstaculo movel e atirador ao mesmo tempo. O desenho e o mesmo
// sprite do bonus antigo, em dobro, para virar um alvo de verdade.
#define NAVE_SCALE       2
#define NAVE_W           (13 * NAVE_SCALE)
#define NAVE_H           (8 * NAVE_SCALE)
#define NAVE_X           (FB_WIDTH / 2 - NAVE_W / 2)
#define NAVE_SPEED       1
#define NAVE_SHOT_PERIOD 100                     // frames entre tiros
#define NAVE_SHOT_MAX    4                       // tiros simultaneos
#define SHOT_W            4
#define SHOT_H            2
#define SHOT_SPEED        3
#define SHRINK_FRAMES     300                     // 5 s de raquete pequena

// Giro sorteado a cada rebote nos postes do PINBALL/COLUNA: sem ele a bola
// entra em orbita perfeita entre dois postes e fica ali indo e voltando. E uma
// rotacao (nao um empurrao), entao a velocidade da bola nao muda -- somar um
// desvio direto no eixo deixava a bola cada vez mais lenta e vertical.
#define BUMPER_SPIN_SHIFT 4                       // ~1/16 rad = 3,6 graus
#define BALL_VX_MIN_Q     0x40                    // vx minimo apos o giro

// Fase PINBALL: 5 obstaculos fixos no meio da quadra.
#define BUMPER_W          12
#define BUMPER_H          12

// Fase COLUNA: os mesmos obstaculos, empilhados no meio e subindo/descendo.
#define COL_BUMPERS       5
#define COL_GAP           18                      // vao entre eles
#define COL_SPEED         1                       // px/frame

// Fase REBOUND (volei): raquetes deitadas andando na horizontal dentro da
// propria meia-quadra, bola com gravidade e ponto quando ela toca o chao.
#define VOLLEY_PADDLE_W   24
#define VOLLEY_PADDLE_H   4
#define VOLLEY_PADDLE_Y   8                       // altura do chao ate a raquete
#define VOLLEY_MARGIN     4                       // folga nas bordas/rede
#define NET_W             4
#define NET_TOP           112                     // rede vai daqui ate o chao
#define GRAVITY_Q         0x1E                    // aceleracao por frame (Q8)
#define BALL_VY_MAX_Q     0x500                   // velocidade de queda maxima
#define VOLLEY_VY_Q       0x4C0                   // impulso para cima ao rebater
// O toque SEMPRE empurra a bola para o campo adversario (VOLLEY_VX_BASE_Q); a
// borda em que ela bateu so soma ou tira alcance (VOLLEY_VX_SPREAD_Q). Com vx
// saindo puro do offset, bater no meio da raquete devolvia a bola em cima do
// proprio jogador e a fase virava um saco de pancadas.
#define VOLLEY_VX_BASE_Q   0x1C0                  // empurrao minimo para frente
#define VOLLEY_VX_SPREAD_Q 0x140                  // alcance a mais/menos pela borda

// ===== CPU (modo arcade) =====
// A CPU melhora fase a fase, em linha reta entre os dois extremos (ai_curva()
// em game.c): na primeira ela e lenta e mira mal de proposito, que e a fase que
// ensina o jogo, e na ultima ela e rapida e erra pouco.
#define AI_SPEED_MIN      3                  // px/frame na primeira fase
#define AI_SPEED_MAX      7                  // px/frame na ultima
// O erro precisa ser MAIOR que a meia-raquete (PADDLE_H/2), senao a CPU so
// erra a mira e ainda assim rebate de quina -- ou seja, nunca perde um ponto.
// O erro de mira e sorteado a cada rebatida. O extremo dificil NAO pode chegar
// perto de meia raquete (PADDLE_H/2 = 12 px): abaixo disso a bola cai sempre em
// cima da raquete e a CPU simplesmente nao erra mais -- medido no simulador,
// com 12 px uma fase passava de 10 minutos, e no arcade isso significa o
// jogador perdendo a primeira fase que a CPU jogar bem.
#define AI_ERROR_MAX_PX   26                 // erro na primeira fase
#define AI_ERROR_MIN_PX   15                 // ... e na ultima

// ===== Highscores =====
#define HISCORE_COUNT     5
#define HISCORE_MAGIC     0x50524F4Bu        // 'PROK'
#define HISCORE_VERSION   3                  // v3: guarda o modo (arcade/versus)
#define INITIALS_LEN      3                  // letras por entrada

#endif // PONG_CONFIG_H
