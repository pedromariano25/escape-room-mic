/*
 * Escape Room - Operação Blecaute
 * DAS5332 Microcontrolados - UFSC 2026.2 - Pedro Mariano e Vinícius
 *
 * ordem do jogo: ESPERANDO -> FASE1 -> ACRESCIMOS (LDR) -> FASE2 -> SENHA -> VITORIA
 * 3 erros ou tempo zerado em qualquer fase = DERROTA (boom)
 *
 * pinos: D2 START (INT0) | D3 e D4 LEDs | D5/D6 teclado (SoftWire) | D11 buzzer (OC2A)
 *        A0 potenciômetro | A1 LDR | A4/A5 LCD I2C
 *
 * bibliotecas só pro LCD I2C e pro I2C do teclado (LiquidCrystal I2C, SoftWire, AsyncDelay)
 * timers, INT0, ADC, UART e GPIO foram feitos direto nos registradores
 *
 * cada parte do jogo tá numa aba (01_interrupcoes, ..., 10_fase1_sinais, 11_bonus_ldr,
 * 12_fase2_radio, 13_senha_final); aqui ficam as configurações, variáveis, setup e loop
 */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftWire.h>
#include <AsyncDelay.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <stdlib.h>
#include <string.h>

/* ---------- macros de bits ---------- */
#define BIT_SET(reg, bit)   ((reg) |=  (1 << (bit))) /* liga o bit */
#define BIT_CLR(reg, bit)   ((reg) &= ~(1 << (bit))) /* desliga o bit */
#define BIT_TGL(reg, bit)   ((reg) ^=  (1 << (bit))) /* inverte o bit */
#define BIT_TST(reg, bit)   (((reg) >> (bit)) & 1) /* lê o bit: dá 0 ou 1 */

#define TAMANHO(vetor)      (sizeof(vetor) / sizeof((vetor)[0])) /* quantos itens tem o vetor */

/* ---------- pinos ---------- */
/* START: D2 = PD2 = INT0 */
#define START_DDR           DDRD
#define START_PORT          PORTD
#define START_PIN           PIND
#define START_BIT           PD2

/* LEDs da fase 1: D3 = PD3 (bit 1), D4 = PD4 (bit 0) */
#define LED_DDR             DDRD
#define LED_PORT            PORTD
#define LED_BIT1            PD3
#define LED_BIT0            PD4

/* buzzer: D11 = PB3 = OC2A */
#define BUZZER_DDR          DDRB
#define BUZZER_PORT         PORTB
#define BUZZER_BIT          PB3

/* ---------- regras do jogo ---------- */
#define TEMPO_INICIAL_S     600                 /* 10 min de bomba */
#define AVISO_S             30                  /* a partir daqui o bipe fica agudo */
#define PENALIDADE_S        30                  /* cada erro tira 30 s */
#define MAX_ERROS           3                   /* no 3º erro explode */

/* ---------- sons ---------- */
#define FREQ_BIPE_NORMAL    1000                /* bipe normal em Hz */
#define FREQ_BIPE_AVISO     2000                /* bipe dos últimos 30 s */
#define BIP1_FIM_MS         60                  /* 1º bip: 0 a 60 ms */
#define BIP2_INICIO_MS      120                 /* 2º bip: 120 a 180 ms */
#define BIP2_FIM_MS         180

/* ---------- I2C ---------- */
#define ENDERECO_LCD        0x27                /* LCD no I2C de hardware (A4/A5) */
#define ENDERECO_TECLADO    0x27                /* mesmo endereço, por isso o teclado vai em outro barramento */
#define PINO_SDA_TECLADO    5
#define PINO_SCL_TECLADO    6

/* ---------- UART (9600 8N1) ---------- */
#define UBRR_9600           103                 /* 16 MHz / (16 * 9600) - 1 = 103,17 */
#define RX_TAM              32                  /* potência de 2 pra usar & (TAM - 1) no lugar de % */
#define TX_TAM              128
#define LINHA_SERIAL_MAX    16                  /* maior comando aceito */

/* ---------- tempos de tela e START ---------- */
#define MENSAGEM_MS         1500                /* tempo que "ERRO!" e "BONUS!" ficam na tela */
#define SOLTO_MIN_MS        50                  /* debounce: START tem que ficar 50 ms solto */

/* ---------- timer1 (base de tempo) ---------- */
#define OCR_1MS             249                 /* prescaler 64: 250 contagens de 4 us = 1 ms */

/* ---------- fase 1 ---------- */
#define PISCA_ACESO_MS      300                 /* tempo de LED aceso em cada piscada */
#define PISCA_ENTRE_MS      400                 /* intervalo entre as 2 piscadas do bit 1 */
#define PAUSA_BIT_MS        1200                /* pausa entre um bit e outro */
#define PAUSA_LETRA_MS      3000                /* pausa antes de repetir a palavra */
#define MARCADOR_MS         1000                /* os 2 LEDs juntos = começo da palavra */
#define FASE1_LETRAS        2

#define TECLADO_PERIODO_MS  5                   /* varre uma linha a cada 5 ms */
#define TECLADO_ESTAVEL     2                   /* tecla só vale depois de 2 varreduras iguais */

/* ---------- acrescimos (LDR) ---------- */
#define LDR_CANAL           1                   /* LDR no A1 */
#define LDR_ESCURO_ENTRA    40                  /* abaixo disso tá escuro (calibrar na sala pelo L: do LCD) */
#define LDR_ESCURO_SAI      60                  /* acima disso volta a ser claro (histerese) */
#define LDR_PERIODO_MS      10
#define LDR_TELA_MS         250
#define ACR_ESCURO_MS       5000                /* 5 s no escuro ganha o bônus */
#define ACR_BONUS_S         60                  /* bônus de +60 s */
#define ACR_JANELA_S        20                  /* 20 s pra tentar */

/* ---------- fase 2 ---------- */
#define F2_RODADAS          2
#define ZONA_ENTRA          40                  /* entra na zona de silêncio a 40 do alvo */
#define ZONA_SAI            55                  /* só sai a 55 (histerese, não fica piscando) */
#define ALVO_MIN            60
#define ALVO_MAX            963
#define DIST_MIN_SORTEIO    250                 /* alvo sorteado longe de onde o pot já está */
#define MEDIA_SHIFT         3                   /* média de 2^3 = 8 leituras */
#define MEDIA_N             (1 << MEDIA_SHIFT)
#define ADC_PERIODO_MS      5                   /* lê o pot a cada 5 ms */
#define SOM_PERIODO_MS      40                  /* atualiza o tom a cada 40 ms */
#define PERIODO_PERTO_US    500
#define PERIODO_LONGE_US    5000
#define ADC_CANAL_POT       0                   /* potenciômetro no A0 */

/* ---------- senha final ---------- */
#define SENHA_TAM           4                   /* AVAI tem 4 letras */

/* ---------- tipos ---------- */
typedef enum {
    JOGO_ESPERANDO,
    JOGO_FASE1,
    JOGO_ACRESCIMOS,
    JOGO_FASE2,
    JOGO_SENHA,
    JOGO_VITORIA,
    JOGO_DERROTA
} EstadoJogo;

typedef enum {
    F2_PROCURANDO,
    F2_DIGITANDO
} EstadoFase2;

typedef struct {
    uint16_t freq_hz;
    uint16_t duracao_ms;
} Nota;

typedef struct {
    char    letra;      /* letra que o jogador precisa digitar */
    uint8_t codigo;     /* 4 bits piscados nos LEDs, do bit 3 ao bit 0 */
} LetraFase1;

/* ---------- tabelas ---------- */
const LetraFase1 FASE1_TABELA[FASE1_LETRAS] = {
    {'A', 0b1011},
    {'V', 0b0110}
};

const char TECLAS[4][3] = {
    {'A', 'V', 'I'},
    {'R', 'E', 'T'},
    {'O', 'S', 'N'},
    {'*', 'L', '#'}
};

const uint8_t BIT_LINHA[4]  = {0, 1, 2, 4};     /* pinos do PCF8574 ligados nas linhas */
const uint8_t BIT_COLUNA[3] = {5, 6, 7};        /* pinos do PCF8574 ligados nas colunas */

const char     F2_LETRA_CERTA[F2_RODADAS] = {'A', 'I'}; /* resposta de cada rodada do rádio */
const uint16_t F2_FREQ_RODADA[F2_RODADAS] = {3400, 378}; /* 3400 Hz -> 10 cm -> A | 378 Hz -> 90 cm -> I */

const char SENHA_CERTA[] = "AVAI";

const Nota MELODIA_ERRO[] = {
    {400, 120}, {0, 40}, {250, 120}, {0, 40}, {150, 300}
};

const Nota MELODIA_BONUS[] = {
    {659, 100}, {784, 100}, {1047, 250}
};

const Nota MELODIA_VITORIA[] = {
    {523, 150}, {659, 150}, {784, 150}, {1047, 300}, {0, 100},
    {784, 150}, {1047, 500}, {0, 150},
    {880, 150}, {988, 150}, {1047, 150}, {1319, 700}
};

const Nota MELODIA_BOOM[] = {
    {1200, 40}, {900, 50}, {700, 60}, {550, 70}, {420, 80}, {320, 90},
    {240, 110}, {180, 140}, {130, 180}, {95, 250}, {70, 400}, {50, 900}
};

const char *const NOME_ESTADO[] = {
    "ESPERANDO", "FASE1", "ACRESCIMOS", "FASE2", "SENHA", "VITORIA", "DERROTA"
};

const uint16_t PRESCALERS_T2[7] = {1, 8, 32, 64, 128, 256, 1024}; /* prescalers que o Timer2 tem */

/* ---------- objetos das bibliotecas ---------- */
LiquidCrystal_I2C lcd_1(ENDERECO_LCD, 16, 2);   /* LCD 16x2 */
SoftWire i2c_teclado(PINO_SDA_TECLADO, PINO_SCL_TECLADO); /* I2C por software no D5/D6 só pro teclado */
char i2c_tx[4];
char i2c_rx[4];

/* ---------- variaveis das ISRs (volatile) ---------- */
volatile uint16_t ms_no_segundo = 0;            /* ms dentro do segundo atual (0 a 999) */
volatile uint8_t  flag_1ms = 0;                 /* a ISR do Timer1 levanta a cada 1 ms */
volatile uint8_t  flag_1s = 0;                  /* levanta a cada 1 s */
volatile uint8_t  flag_start = 0;               /* a ISR do INT0 levanta quando aperta o START */
volatile uint16_t adc_valor = 0;                /* última leitura do ADC (0 a 1023) */
volatile uint8_t  adc_pronto = 0;               /* 1 quando tem leitura nova */

volatile char    uart_rx[RX_TAM];
volatile uint8_t uart_rx_ini = 0;
volatile uint8_t uart_rx_fim = 0;
volatile char    uart_tx[TX_TAM];
volatile uint8_t uart_tx_ini = 0;
volatile uint8_t uart_tx_fim = 0;

/* ---------- estado do jogo ---------- */
char    serial_linha[LINHA_SERIAL_MAX + 1];
uint8_t serial_linha_tam = 0;
uint8_t serial_linha_longa = 0;

EstadoJogo estado = JOGO_ESPERANDO;             /* em que parte do jogo a gente está */
uint16_t tempo_restante_s = TEMPO_INICIAL_S;    /* tempo da bomba em segundos */
uint8_t  erros = 0;

uint8_t  buzzer_ligado = 0;
uint8_t  buzzer_ocr = 0;
uint8_t  buzzer_cs = 0;
uint8_t  bip_tocando = 0;
uint16_t freq_bipe_atual = FREQ_BIPE_NORMAL;

const Nota *melodia_atual = 0;
uint8_t  melodia_tamanho = 0;
uint8_t  melodia_indice = 0;
uint16_t melodia_ms = 0;

uint16_t mensagem_ms = 0;

uint8_t start_pronto = 1;
uint8_t start_solto_ms = 0;

uint8_t teclado_div = 0;
uint8_t teclado_linha = 0;
char    teclado_tecla_varredura = 0;
char    teclado_candidata = 0;
uint8_t teclado_contagem = 0;
char    teclado_reportada = 0;
char    teclado_evento = 0;

uint8_t  fase1_indice = 0;          /* posicao na FASE1_TABELA */
char     fase1_digitada = 0;
uint8_t  fase1_codigo = 0;
uint8_t  fase1_bit = 3;
uint8_t  fase1_piscadas = 0;
uint8_t  fase1_led_aceso = 0;
uint16_t fase1_espera_ms = 0;
uint8_t  fase1_no_marcador = 0;     /* 1 enquanto os 2 LEDs estao acesos juntos */
uint8_t  fase1_marcador_pendente = 0;

uint16_t ldr_valor = 1023;
uint8_t  ldr_escuro = 0;
uint8_t  acr_adc_div = 0;
uint16_t acr_tela_div = 0;
uint16_t acr_escuro_ms = 0;
uint8_t  acr_janela_s = 0;

EstadoFase2 f2_estado = F2_PROCURANDO;
uint8_t  f2_rodada = 0;
uint16_t f2_alvo = 0;
uint8_t  f2_alvo_definido = 0;
uint8_t  f2_na_zona = 0;
char     f2_letra = 0;
uint8_t  f2_tela_mudou = 0;
uint16_t f2_amostras[MEDIA_N];
uint16_t f2_soma = 0;
uint8_t  f2_indice = 0;
uint8_t  f2_qtd_amostras = 0;
uint16_t f2_pot = 0;                            /* posição do pot já com a média */
uint8_t  f2_adc_div = 0;
uint8_t  f2_som_div = 0;

char    senha_digitada[SENHA_TAM + 1];
uint8_t senha_tam = 0;

/* ---------- setup e loop ---------- */

/* configura tudo uma vez: pinos, INT0, Timer1, Timer2, ADC e UART */
void setup()
{
    lcd_1.init();
    lcd_1.backlight();
    lcd_1.print("APERTE START");

    i2c_teclado.setTxBuffer(i2c_tx, sizeof(i2c_tx));
    i2c_teclado.setRxBuffer(i2c_rx, sizeof(i2c_rx));
    i2c_teclado.setDelay_us(5);
    i2c_teclado.setTimeout(10);
    i2c_teclado.begin();
    mostrar_tempo();

    cli();                                      /* desliga as interrupções enquanto configura */

    /* START no D2: entrada com pull-up interno */
    BIT_CLR(START_DDR, START_BIT);
    BIT_SET(START_PORT, START_BIT);
    EICRA = (1 << ISC01);                       /* INT0 na borda de descida (botão puxa pro GND) */
    EIFR  = (1 << INTF0);                       /* limpa algum pedido velho do INT0 */
    EIMSK = (1 << INT0);                        /* liga a interrupção INT0 */

    /* buzzer no D11: saída */
    BIT_SET(BUZZER_DDR, BUZZER_BIT);
    BIT_CLR(BUZZER_PORT, BUZZER_BIT);
    TCCR2A = (1 << WGM21);                      /* Timer2 em CTC, ainda sem mexer no D11 */
    TCCR2B = 0;                                 /* Timer2 parado (sem prescaler) */

    /* LEDs da fase 1 no D3 e D4 */
    BIT_SET(LED_DDR, LED_BIT1);
    BIT_SET(LED_DDR, LED_BIT0);
    fase1_leds_apagar();

    /* ADC */
    ADMUX  = (1 << REFS0);                      /* referência = AVcc (5 V) */
    ADCSRA = (1 << ADEN) | (1 << ADIE) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); /* liga o ADC, com interrupção, prescaler 128 = 125 kHz */
    DIDR0  = (1 << ADC0D) | (1 << ADC1D);       /* desliga a parte digital do A0 e A1 */

    /* Timer1: base de tempo de 1 ms */
    TCCR1A = 0;                                 /* zera antes de configurar */
    TCCR1B = 0;
    OCR1A  = OCR_1MS;                           /* conta até 249 e volta pra 0 */
    TIMSK1 = (1 << OCIE1A);                     /* interrupção quando TCNT1 chega no OCR1A */
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10); /* modo CTC + prescaler 64: começa a contar */

    uart_iniciar();

    sei();                                      /* liga as interrupções */

    uart_enviar_linha("ESCAPE ROOM - OPERACAO BLECAUTE");
    uart_enviar_linha("COMANDOS: ARMAR, RESET, STATUS, AJUDA");

    pcf_escrever(0xFF);
    teclado_selecionar_linha(0);
}

/* loop nunca espera: só olha as flags que as ISRs levantam */
void loop()
{
    serial_processar();                         /* trata o que chegou pela serial */

    /* ---------- a cada 1 s: relógio da bomba ---------- */
    if (flag_1s) {
        flag_1s = 0;
        if (jogo_em_andamento() && tempo_restante_s > 0) {
            tempo_restante_s--;                 /* passou 1 s */
            mostrar_tempo();
            if (tempo_restante_s <= AVISO_S) freq_bipe_atual = FREQ_BIPE_AVISO;
            else                             freq_bipe_atual = FREQ_BIPE_NORMAL;
            if (tempo_restante_s == 0) derrota("TEMPO ESGOTADO"); /* acabou o tempo: BOOM */
        }
        if (estado == JOGO_ACRESCIMOS && acr_janela_s > 0) {
            acr_janela_s--;
            if (acr_janela_s == 0) acrescimos_sair(0);
        }
    }

    /* ---------- a cada 1 ms: todo o resto ---------- */
    if (flag_1ms) {
        flag_1ms = 0;

        start_atualizar();
        if (start_apertado()) {
            if (estado == JOGO_ESPERANDO)     armar_bomba();
            else if (estado == JOGO_FASE2)    fase2_start();
            else if (estado == JOGO_VITORIA ||
                     estado == JOGO_DERROTA)  jogo_reiniciar();
        }

        teclado_atualizar();                    /* varre o teclado */

        if (estado == JOGO_FASE1) {
            fase1_atualizar();
        } else if (estado == JOGO_ACRESCIMOS) {
            acrescimos_atualizar();
        } else if (estado == JOGO_FASE2) {
            fase2_atualizar();
            if (f2_tela_mudou && estado == JOGO_FASE2 && mensagem_ms == 0) {
                f2_tela_mudou = 0;
                fase2_desenhar();
            }
        }

        /* quem manda no buzzer: melodia > quente/frio da fase 2 > bip do relógio */
        uint16_t ms = ler_ms_no_segundo();
        if (melodia_tocando() || fase2_usando_buzzer()) {
            bip_tocando = 0;
        } else {
            uint8_t quer_som = 0;
            if (jogo_em_andamento() && tempo_restante_s > 0) {
                if (ms < BIP1_FIM_MS) quer_som = 1;
                if (ms >= BIP2_INICIO_MS && ms < BIP2_FIM_MS) quer_som = 1;
            }
            if (quer_som && !bip_tocando) {
                buzzer_tocar(freq_bipe_atual);
                bip_tocando = 1;
            } else if (!quer_som && bip_tocando) {
                buzzer_desligar();
                bip_tocando = 0;
            }
        }

        melodia_atualizar();                    /* avança a melodia se tiver uma tocando */

        /* "ERRO!" e "BONUS!" somem depois de 1,5 s e a tela da fase volta */
        if (mensagem_ms > 0) {
            mensagem_ms--;
            if (mensagem_ms == 0) {
                if (estado == JOGO_FASE1)      mostrar_linha0_fase1();
                else if (estado == JOGO_FASE2) fase2_desenhar();
                else if (estado == JOGO_SENHA) senha_desenhar();
            }
        }

        char tecla = teclado_ler();             /* pega a tecla nova (0 se não tem) */
        if (tecla) {
            if (estado == JOGO_FASE1)      fase1_tratar_tecla(tecla);
            else if (estado == JOGO_FASE2) fase2_tratar_tecla(tecla);
            else if (estado == JOGO_SENHA) senha_tratar_tecla(tecla);
        }
    }
}
