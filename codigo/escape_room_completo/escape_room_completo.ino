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
 * versão em arquivo único: primeiro configurações, variáveis, setup e loop,
 * depois cada parte (interrupções, UART, buzzer, LCD, teclado, START, ADC, regras,
 * fase 1, bônus do LDR, fase 2 e senha final)
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


/* interrupções: as ISRs só levantam flag ou guardam valor, quem faz o trabalho é o loop */

ISR(TIMER1_COMPA_vect)                  /* a cada 1 ms */
{
    ms_no_segundo++;                            /* mais 1 ms */
    if (ms_no_segundo >= 1000) {
        ms_no_segundo = 0;
        flag_1s = 1;                            /* passou 1 s, avisa o loop */
    }
    flag_1ms = 1;                               /* só avisa o loop */
}

ISR(INT0_vect)                          /* START: borda de descida no D2 */
{
    flag_start = 1;                             /* só avisa o loop */
}

ISR(ADC_vect)                           /* fim de uma conversao */
{
    uint8_t baixo = ADCL;                       /* ADCL primeiro: trava o ADCH até ele ser lido */
    uint8_t alto  = ADCH;                       /* só os 2 bits de cima (o resultado tem 10 bits) */
    adc_valor = ((uint16_t)alto << 8) | baixo;  /* junta: alto vai 8 casas pra esquerda */
    adc_pronto = 1;                             /* avisa que tem leitura nova */
}

ISR(USART_RX_vect)                      /* chegou um byte pela serial */
{
    char c = UDR0;                              /* byte que chegou */
    uint8_t prox = (uart_rx_fim + 1) & (RX_TAM - 1); /* próxima posição, voltando pro 0 no fim */
    if (prox != uart_rx_ini) {                  /* se o buffer não tá cheio */
        uart_rx[uart_rx_fim] = c;
        uart_rx_fim = prox;
    }
}

ISR(USART_UDRE_vect)                    /* registrador de envio livre */
{
    if (uart_tx_ini != uart_tx_fim) {
        UDR0 = uart_tx[uart_tx_ini];            /* manda o próximo byte */
        uart_tx_ini = (uart_tx_ini + 1) & (TX_TAM - 1);
    } else {
        BIT_CLR(UCSR0B, UDRIE0);                /* acabou o que mandar: desliga essa interrupção */
    }
}


/* UART (9600 8N1) com buffer circular e os comandos da serial: ARMAR, RESET, STATUS, AJUDA */

void uart_iniciar(void)
{
    UBRR0  = UBRR_9600;                         /* 9600 bps */
    UCSR0A = 0;                                 /* velocidade normal (sem U2X0) */
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0); /* liga recepção, envio e interrupção de recepção */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);     /* 8 bits, sem paridade, 1 stop (8N1) */
}

uint8_t uart_ler(char *c)
{
    if (uart_rx_ini == uart_rx_fim) return 0;   /* buffer vazio */
    *c = uart_rx[uart_rx_ini];
    uart_rx_ini = (uart_rx_ini + 1) & (RX_TAM - 1);
    return 1;
}

void uart_enviar_char(char c)
{
    uint8_t prox = (uart_tx_fim + 1) & (TX_TAM - 1);
    while (prox == uart_tx_ini) {
        /* buffer cheio: espera a ISR esvaziar */
    }
    uart_tx[uart_tx_fim] = c;
    uart_tx_fim = prox;
    BIT_SET(UCSR0B, UDRIE0);                    /* liga a interrupção de envio, a ISR manda o resto */
}

void uart_enviar(const char *texto)
{
    while (*texto) uart_enviar_char(*texto++);
}

void uart_enviar_linha(const char *texto)
{
    uart_enviar(texto);
    uart_enviar("\r\n");
}

void uart_enviar_num(uint16_t numero)
{
    char buf[6];
    utoa(numero, buf, 10);
    uart_enviar(buf);
}


/* ---------- protocolo serial: ARMAR, RESET, STATUS, AJUDA ---------- */

void serial_executar(void)
{
    if (strcmp(serial_linha, "ARMAR") == 0) {
        if (estado == JOGO_ESPERANDO) {
            armar_bomba();
            uart_enviar_linha("OK ARMADA");
        } else {
            uart_enviar_linha("ERRO JA ARMADA");
        }
    } else if (strcmp(serial_linha, "RESET") == 0) {
        jogo_reiniciar();                       /* volta pro começo sem reset físico */
        uart_enviar_linha("OK RESET");
    } else if (strcmp(serial_linha, "STATUS") == 0) {
        uart_enviar("OK ESTADO ");
        uart_enviar(NOME_ESTADO[estado]);
        uart_enviar(" TEMPO ");
        uart_enviar_num(tempo_restante_s);
        uart_enviar(" ERROS ");
        uart_enviar_num(erros);
        uart_enviar_linha("");
    } else if (strcmp(serial_linha, "AJUDA") == 0) {
        uart_enviar_linha("COMANDOS: ARMAR, RESET, STATUS, AJUDA");
    } else {
        uart_enviar_linha("ERRO COMANDO DESCONHECIDO");
    }
}

void serial_processar(void)
{
    char c;
    while (uart_ler(&c)) {
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A'; /* minúscula vira maiúscula */

        if (c == '\r' || c == '\n') {
            if (serial_linha_longa) {
                uart_enviar_linha("ERRO LINHA LONGA");
            } else if (serial_linha_tam > 0) {
                serial_linha[serial_linha_tam] = '\0';
                serial_executar();
            }
            serial_linha_tam = 0;
            serial_linha_longa = 0;
            continue;
        }

        if (serial_linha_tam < LINHA_SERIAL_MAX) serial_linha[serial_linha_tam++] = c;
        else                                     serial_linha_longa = 1;
    }
}


/* buzzer no Timer2 (CTC invertendo o D11) e as melodias de erro, bônus, vitória e boom */

void buzzer_desligar(void)
{
    TCCR2B = 0;                                 /* para o Timer2 */
    TCCR2A = (1 << WGM21);                      /* COM2A0 = 0: o timer solta o D11 */
    BIT_CLR(BUZZER_PORT, BUZZER_BIT);           /* deixa o D11 em 0 */
    buzzer_ligado = 0;
}

/* f = 16 MHz / (2 * N * (OCR2A + 1)), testa os prescalers do menor pro maior até o OCR2A caber em 8 bits */
void buzzer_tocar(uint16_t freq_hz)
{
    if (freq_hz == 0) {
        buzzer_desligar();
        return;
    }
    for (uint8_t i = 0; i < 7; i++) {
        uint32_t divisor = 2UL * PRESCALERS_T2[i] * freq_hz; /* 2 * N * f */
        uint32_t passos  = (F_CPU + divisor / 2) / divisor; /* OCR2A + 1, já arredondado */

        if (passos >= 1 && passos <= 256) {     /* cabe nos 8 bits do Timer2 */
            uint8_t ocr = (uint8_t)(passos - 1);
            uint8_t cs  = i + 1;                /* CS22:20 de 1 a 7 escolhe o prescaler */

            if (buzzer_ligado && ocr == buzzer_ocr && cs == buzzer_cs) return; /* já tá tocando essa nota */

            OCR2A  = ocr;                       /* define a frequência */
            TCCR2A = (1 << COM2A0) | (1 << WGM21); /* CTC + inverte o D11 a cada volta */
            if (TCNT2 > ocr) TCNT2 = 0;         /* se já passou do novo OCR, recomeça */
            TCCR2B = cs;                        /* prescaler: o som começa */

            buzzer_ligado = 1;
            buzzer_ocr = ocr;
            buzzer_cs = cs;
            return;
        }
    }
    buzzer_desligar();
}

/* ---------- melodias (erro, bonus, vitoria, boom) ---------- */

void melodia_tocar(const Nota *melodia, uint8_t tamanho)
{
    melodia_atual = melodia;
    melodia_tamanho = tamanho;
    melodia_indice = 0;
    bip_tocando = 0;
    buzzer_tocar(melodia[0].freq_hz);
    melodia_ms = melodia[0].duracao_ms;
}

uint8_t melodia_tocando(void)
{
    return melodia_atual != 0;
}

void melodia_atualizar(void)            /* chamada a cada 1 ms */
{
    if (!melodia_atual) return;

    if (melodia_ms > 0) melodia_ms--;
    if (melodia_ms > 0) return;

    melodia_indice++;
    if (melodia_indice >= melodia_tamanho) {
        melodia_atual = 0;
        buzzer_desligar();
        return;
    }
    buzzer_tocar(melodia_atual[melodia_indice].freq_hz);
    melodia_ms = melodia_atual[melodia_indice].duracao_ms;
}


/* telas do LCD 16x2 */

void mostrar_tempo(void)
{
    uint8_t minutos  = tempo_restante_s / 60;   /* divisão inteira: minutos */
    uint8_t segundos = tempo_restante_s % 60;   /* resto: segundos */

    lcd_1.setCursor(0, 1);                      /* linha de baixo */
    if (minutos < 10)  lcd_1.print('0');
    lcd_1.print(minutos);
    lcd_1.print(':');
    if (segundos < 10) lcd_1.print('0');
    lcd_1.print(segundos);
}

void mostrar_letra(void)
{
    lcd_1.setCursor(7, 1);
    lcd_1.print("LETRA:");
    if (fase1_digitada) lcd_1.print(fase1_digitada);
    else                lcd_1.print('_');
}

void mostrar_erros(void)
{
    if (estado == JOGO_FASE1) lcd_1.setCursor(14, 0);
    else                      lcd_1.setCursor(15, 1);
    lcd_1.print(erros);
}

void lcd_linha(uint8_t linha, const char *texto)    /* reescreve a linha toda, sem clear() (clear pisca a tela) */
{
    lcd_1.setCursor(0, linha);
    uint8_t i = 0;
    while (texto[i] != '\0' && i < 16) {
        lcd_1.print(texto[i]);
        i++;
    }
    while (i < 16) {
        lcd_1.print(' ');
        i++;
    }
}

void lcd_linha0(const char *texto)
{
    lcd_linha(0, texto);
}

void mostrar_linha1_tempo_erros(void)   /* "mm:ss    ERROS:n" (fase 2 e senha) */
{
    mostrar_tempo();
    lcd_1.setCursor(5, 1);
    lcd_1.print("    ERROS:");
    lcd_1.print(erros);
}

void mostrar_linha0_fase1(void)
{
    char texto[17] = "FASE 1  ERROS:";
    texto[14] = '0' + erros;
    texto[15] = '\0';
    lcd_linha0(texto);
}


/* teclado 3x4 pelo PCF8574 no I2C por software (D5/D6) */

void pcf_escrever(uint8_t valor)
{
    i2c_teclado.beginTransmission(ENDERECO_TECLADO);
    i2c_teclado.write(valor);
    i2c_teclado.endTransmission();
}

uint8_t pcf_ler(void)
{
    i2c_teclado.requestFrom((uint8_t)ENDERECO_TECLADO, (uint8_t)1);
    if (i2c_teclado.available()) return i2c_teclado.read();
    return 0xFF;
}

void teclado_selecionar_linha(uint8_t linha)
{
    pcf_escrever(0xFF & ~(1 << BIT_LINHA[linha])); /* só a linha escolhida em 0, o resto em 1 */
}

int8_t teclado_coluna_apertada(void)
{
    uint8_t leitura = pcf_ler();
    for (uint8_t coluna = 0; coluna < 3; coluna++) {
        if (!BIT_TST(leitura, BIT_COLUNA[coluna])) return coluna; /* coluna em 0 = tecla apertada */
    }
    return -1;
}

void teclado_atualizar(void)            /* uma linha a cada 5 ms, tecla vale depois de 2 varreduras iguais */
{
    teclado_div++;
    if (teclado_div < TECLADO_PERIODO_MS) return;
    teclado_div = 0;

    int8_t coluna = teclado_coluna_apertada();
    if (coluna >= 0) teclado_tecla_varredura = TECLAS[teclado_linha][coluna];

    teclado_linha++;
    if (teclado_linha >= 4) {
        teclado_linha = 0;

        if (teclado_tecla_varredura == teclado_candidata) {
            if (teclado_contagem < 255) teclado_contagem++;
        } else {
            teclado_candidata = teclado_tecla_varredura;
            teclado_contagem = 0;
        }

        if (teclado_contagem >= TECLADO_ESTAVEL && teclado_candidata != teclado_reportada) {
            teclado_reportada = teclado_candidata;
            if (teclado_candidata != 0) teclado_evento = teclado_candidata;
        }

        teclado_tecla_varredura = 0;
    }

    teclado_selecionar_linha(teclado_linha);    /* prepara a próxima linha */
}

char teclado_ler(void)
{
    char tecla = teclado_evento;
    teclado_evento = 0;
    return tecla;
}


/* botão START no INT0 (D2) com debounce */

void start_atualizar(void)
{
    if (!BIT_TST(START_PIN, START_BIT)) {       /* pino em 0 = botão apertado (pull-up) */
        start_solto_ms = 0;                         /* apertado ou repicando */
    } else if (start_solto_ms < SOLTO_MIN_MS) {
        start_solto_ms++;
    }

    if (!start_pronto && start_solto_ms >= SOLTO_MIN_MS) {
        start_pronto = 1;                           /* ficou 50 ms solto */
        flag_start = 0;                             /* bordas guardadas eram repique */
    }
}

uint8_t start_apertado(void)
{
    if (flag_start) {
        flag_start = 0;
        if (start_pronto) {
            start_pronto = 0;
            return 1;
        }
    }
    return 0;
}


/* ADC por interrupção: potenciômetro (A0) e LDR (A1) */

uint8_t adc_ocupado(void)
{
    return BIT_TST(ADCSRA, ADSC);               /* ADSC fica em 1 enquanto converte */
}

void adc_iniciar_conversao(uint8_t canal)
{
    ADMUX = (1 << REFS0) | (canal & 0x0F);      /* AVcc + canal (0 = pot, 1 = LDR) */
    BIT_SET(ADCSRA, ADSC);                      /* começa a conversão */
}

uint8_t adc_ler_resultado(uint16_t *valor)
{
    uint8_t havia = 0;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {         /* variável de 16 bits: a ISR não pode mudar no meio da leitura */
        if (adc_pronto) {
            *valor = adc_valor;
            adc_pronto = 0;
            havia = 1;
        }
    }
    return havia;
}

uint16_t ler_ms_no_segundo(void)
{
    uint16_t ms;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        ms = ms_no_segundo;
    }
    return ms;
}


/* regras: erro, derrota, vitória, armar e reiniciar */

uint8_t jogo_em_andamento(void)
{
    return estado == JOGO_FASE1 || estado == JOGO_ACRESCIMOS ||
           estado == JOGO_FASE2 || estado == JOGO_SENHA;
}

void derrota(const char *motivo)
{
    estado = JOGO_DERROTA;                      /* BOOM */
    fase1_leds_apagar();
    mensagem_ms = 0;
    melodia_tocar(MELODIA_BOOM, TAMANHO(MELODIA_BOOM));
    lcd_1.clear();
    lcd_1.print("BOOM!");
    lcd_1.setCursor(0, 1);                      /* linha de baixo */
    lcd_1.print(motivo);
    uart_enviar("EVENTO BOOM ");
    uart_enviar_linha(motivo);
}

void registrar_erro(void)
{
    erros++;                                    /* mais um erro */
    if (tempo_restante_s > PENALIDADE_S) tempo_restante_s -= PENALIDADE_S; /* tira 30 s */
    else                                 tempo_restante_s = 0; /* não deixa ficar negativo */
    mostrar_erros();
    mostrar_tempo();

    if (erros >= MAX_ERROS) {
        derrota("3 ERROS");
    } else if (tempo_restante_s == 0) {
        derrota("TEMPO ESGOTADO");
    } else {
        melodia_tocar(MELODIA_ERRO, TAMANHO(MELODIA_ERRO));
        lcd_linha0("ERRO!  -30s");
        mensagem_ms = MENSAGEM_MS;
        uart_enviar("EVENTO ERRO ");
        uart_enviar_num(erros);
        uart_enviar_linha("/3");
    }
}

void vitoria(void)
{
    estado = JOGO_VITORIA;                      /* desarmou */
    mensagem_ms = 0;
    melodia_tocar(MELODIA_VITORIA, TAMANHO(MELODIA_VITORIA));
    lcd_linha(0, "PARABENS! VOCE");
    lcd_linha(1, "ENCONTROU: AVAI");
    uart_enviar_linha("EVENTO VITORIA PALAVRA AVAI");
}


/* ---------- inicio e reinicio ---------- */

void jogo_reiniciar(void)
{
    estado = JOGO_ESPERANDO;
    melodia_atual = 0;
    bip_tocando = 0;
    mensagem_ms = 0;
    buzzer_desligar();
    fase1_leds_apagar();

    tempo_restante_s = TEMPO_INICIAL_S;
    erros = 0;
    freq_bipe_atual = FREQ_BIPE_NORMAL;

    lcd_1.clear();
    lcd_1.print("APERTE START");
    mostrar_tempo();
}

void armar_bomba(void)
{
    estado = JOGO_FASE1;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {         /* o 1º segundo conta inteiro */
        TCNT1 = 0;                              /* zera o Timer1 */
        ms_no_segundo = 0;
        flag_1s = 0;
    }

    tempo_restante_s = TEMPO_INICIAL_S;
    erros = 0;
    fase1_digitada = 0;

    lcd_1.clear();
    freq_bipe_atual = FREQ_BIPE_NORMAL;
    lcd_1.print("FASE 1  ERROS:0 ");
    mostrar_tempo();
    mostrar_letra();

    fase1_iniciar_letra(0);                     /* começa pelo A */
    uart_enviar_linha("EVENTO ARMADA");
}


/* FASE 1: os LEDs piscam a letra em binário (2 piscadas = 1, 1 piscada = 0) e o jogador digita */

uint8_t fase1_valor_do_bit(void)
{
    return BIT_TST(fase1_codigo, fase1_bit);    /* bit que tá sendo mostrado agora */
}

void fase1_led_acender(void)
{
    if (fase1_valor_do_bit()) BIT_SET(LED_PORT, LED_BIT1); /* bit 1 -> LED do D3 */
    else                      BIT_SET(LED_PORT, LED_BIT0); /* bit 0 -> LED do D4 */
}

void fase1_leds_apagar(void)
{
    BIT_CLR(LED_PORT, LED_BIT1);
    BIT_CLR(LED_PORT, LED_BIT0);
}

uint8_t fase1_piscadas_do_bit(void)
{
    if (fase1_valor_do_bit()) return 2;         /* bit 1 = 2 piscadas */
    return 1;
}

void fase1_marcador_acender(void)      /* sinal de início: os dois LEDs juntos */
{
    BIT_SET(LED_PORT, LED_BIT1);
    BIT_SET(LED_PORT, LED_BIT0);
    fase1_no_marcador = 1;
    fase1_led_aceso = 1;
    fase1_espera_ms = MARCADOR_MS;
}

void fase1_iniciar_letra(uint8_t indice)
{
    fase1_indice = indice;
    fase1_codigo = FASE1_TABELA[indice].codigo;
    fase1_bit = 3;                              /* começa do bit mais significativo */
    fase1_piscadas = fase1_piscadas_do_bit();
    fase1_marcador_pendente = 0;
    fase1_marcador_acender();
}

void fase1_atualizar(void)              /* chamada a cada 1 ms */
{
    if (fase1_espera_ms > 0) fase1_espera_ms--; /* conta o tempo sem travar o loop */
    if (fase1_espera_ms > 0) return;            /* ainda não deu o tempo */

    if (fase1_no_marcador) {                        /* fim do sinal de inicio */
        fase1_leds_apagar();
        fase1_no_marcador = 0;
        fase1_led_aceso = 0;
        fase1_espera_ms = PAUSA_BIT_MS;
        return;
    }

    if (fase1_led_aceso) {
        fase1_leds_apagar();
        fase1_led_aceso = 0;
        fase1_piscadas--;

        if (fase1_piscadas > 0) {               /* falta a 2ª piscada */
            fase1_espera_ms = PISCA_ENTRE_MS;
        } else if (fase1_bit > 0) {             /* vai pro próximo bit */
            fase1_bit--;
            fase1_piscadas = fase1_piscadas_do_bit();
            fase1_espera_ms = PAUSA_BIT_MS;
        } else {
            fase1_bit = 3;                      /* começa do bit mais significativo */
            fase1_piscadas = fase1_piscadas_do_bit();
            fase1_espera_ms = PAUSA_LETRA_MS;
            fase1_marcador_pendente = 1;            /* a palavra recomeca com o sinal */
        }
    } else if (fase1_marcador_pendente) {
        fase1_marcador_pendente = 0;
        fase1_marcador_acender();
    } else {
        fase1_led_acender();
        fase1_led_aceso = 1;
        fase1_espera_ms = PISCA_ACESO_MS;
    }
}


/* ---------- fase 1: teclado ---------- */

void fase1_tratar_tecla(char tecla)
{
    if (tecla == '#') {                         /* # apaga */
        fase1_digitada = 0;
        mostrar_letra();
        return;
    }

    if (tecla == '*') {                         /* * confirma */
        if (fase1_digitada == 0) return;

        if (fase1_digitada == FASE1_TABELA[fase1_indice].letra) {
            if (fase1_indice + 1 < FASE1_LETRAS) {
                fase1_iniciar_letra(fase1_indice + 1);
            } else {
                acrescimos_entrar();            /* acertou as 2 letras: vai pro bônus do LDR */
                return;
            }
        } else {
            registrar_erro();                   /* errou: -30 s */
            if (estado == JOGO_DERROTA) return;
        }

        fase1_digitada = 0;
        mostrar_letra();
        return;
    }

    fase1_digitada = tecla;
    mostrar_letra();
}


/* BÔNUS: "apague as luzes", 5 s com o LDR no escuro = +60 s */

void acrescimos_desenhar(void)
{
    mostrar_tempo();
    lcd_1.setCursor(5, 1);
    lcd_1.print(" L:");
    if (ldr_valor < 1000) lcd_1.print('0');
    if (ldr_valor < 100)  lcd_1.print('0');
    if (ldr_valor < 10)   lcd_1.print('0');
    lcd_1.print(ldr_valor);
    lcd_1.print("  ");
    lcd_1.print((uint8_t)(acr_escuro_ms / 1000));
    lcd_1.print('s');
}

void acrescimos_entrar(void)
{
    estado = JOGO_ACRESCIMOS;
    fase1_leds_apagar();

    ldr_valor = 1023;
    ldr_escuro = 0;
    acr_adc_div = 0;
    acr_tela_div = 0;
    acr_escuro_ms = 0;                          /* acendeu: zera a contagem */
    acr_janela_s = ACR_JANELA_S;

    lcd_linha0("APAGUE AS LUZES");
    acrescimos_desenhar();
}

void acrescimos_sair(uint8_t ganhou)
{
    fase2_entrar();                             /* vai pra fase 2 de qualquer jeito */
    if (ganhou) {
        tempo_restante_s += ACR_BONUS_S;        /* +60 s */
        mostrar_linha1_tempo_erros();
        melodia_tocar(MELODIA_BONUS, TAMANHO(MELODIA_BONUS));
        lcd_linha0("BONUS!  +60s");
        uart_enviar_linha("EVENTO BONUS +60s");
    } else {
        lcd_linha0("SEM BONUS");
        uart_enviar_linha("EVENTO SEM BONUS");
    }
    mensagem_ms = MENSAGEM_MS;
}

void acrescimos_atualizar(void)         /* chamada a cada 1 ms */
{
    uint16_t valor;
    if (adc_ler_resultado(&valor)) ldr_valor = valor; /* pega a leitura nova do LDR */

    acr_adc_div++;
    if (acr_adc_div >= LDR_PERIODO_MS && !adc_ocupado()) {
        acr_adc_div = 0;
        adc_iniciar_conversao(LDR_CANAL);       /* pede outra leitura */
    }

    if (ldr_escuro) {                           /* histerese: entra abaixo de 40, sai acima de 60 */
        if (ldr_valor > LDR_ESCURO_SAI) ldr_escuro = 0;
    } else {
        if (ldr_valor < LDR_ESCURO_ENTRA) ldr_escuro = 1;
    }

    if (ldr_escuro) {
        acr_escuro_ms++;                        /* mais 1 ms no escuro */
        if (acr_escuro_ms >= ACR_ESCURO_MS) {
            acrescimos_sair(1);                 /* ficou 5 s no escuro: ganhou */
            return;
        }
    } else {
        acr_escuro_ms = 0;                      /* acendeu: zera a contagem */
    }

    acr_tela_div++;
    if (acr_tela_div >= LDR_TELA_MS) {
        acr_tela_div = 0;
        acrescimos_desenhar();
    }
}


/* FASE 2 (Vinícius): o rádio, gira o pot até o buzzer calar e descobre a letra pela frequência */

uint16_t distancia(uint16_t a, uint16_t b)
{
    if (a > b) return a - b;
    return b - a;
}

void fase2_comecar_rodada(void)
{
    f2_estado = F2_PROCURANDO;
    f2_alvo_definido = 0;
    f2_na_zona = 0;
    f2_letra = 0;
    f2_som_div = 0;
    f2_tela_mudou = 1;
}

void fase2_sortear_alvo(void)
{
    uint16_t alvo = ALVO_MIN;
    for (uint8_t tentativa = 0; tentativa < 20; tentativa++) {
        alvo = ALVO_MIN + (uint16_t)(rand() % (ALVO_MAX - ALVO_MIN + 1)); /* sorteia o ponto de silêncio */
        if (distancia(alvo, f2_pot) >= DIST_MIN_SORTEIO) break;
    }
    f2_alvo = alvo;
    f2_alvo_definido = 1;
}

/* o período cresce junto com a distância, então perto do alvo o tom fica bem agudo */
uint16_t fase2_freq_quente_frio(uint16_t dist)
{
    if (dist > 1023) dist = 1023;
    uint32_t periodo_us = PERIODO_PERTO_US
                        + (((uint32_t)(PERIODO_LONGE_US - PERIODO_PERTO_US) * dist) >> 10); /* >> 10 = dividir por 1024 */
    return (uint16_t)(1000000UL / periodo_us);  /* período -> frequência */
}

uint8_t fase2_usando_buzzer(void)
{
    return estado == JOGO_FASE2 && f2_estado == F2_PROCURANDO;
}

void fase2_desenhar(void)
{
    char texto[17];

    if (f2_estado == F2_PROCURANDO) {
        strcpy(texto, "RADIO 1/2: GIRE");
        texto[6] = '1' + f2_rodada;
    } else {
        char numero[6];
        utoa(F2_FREQ_RODADA[f2_rodada], numero, 10);
        strcpy(texto, numero);
        strcat(texto, "Hz LETRA:");
        uint8_t n = strlen(texto);
        texto[n] = f2_letra ? f2_letra : '_';
        texto[n + 1] = '\0';
    }
    lcd_linha0(texto);
}

void fase2_entrar(void)
{
    srand(TCNT1 ^ (ler_ms_no_segundo() << 6) ^ (tempo_restante_s << 3)); /* semente aleatória: depende de quando o jogador chegou aqui */

    estado = JOGO_FASE2;
    fase1_leds_apagar();

    for (uint8_t i = 0; i < MEDIA_N; i++) f2_amostras[i] = 0;
    f2_soma = 0;
    f2_indice = 0;
    f2_qtd_amostras = 0;
    f2_adc_div = 0;
    f2_rodada = 0;
    fase2_comecar_rodada();

    mostrar_linha1_tempo_erros();
    fase2_desenhar();
    f2_tela_mudou = 0;
}

void fase2_ler_potenciometro(void)
{
    uint16_t valor;
    if (adc_ler_resultado(&valor)) {            /* média móvel das 8 últimas leituras */
        f2_soma -= f2_amostras[f2_indice];      /* tira a leitura mais velha */
        f2_amostras[f2_indice] = valor;
        f2_soma += valor;                       /* soma a nova */
        f2_indice = (f2_indice + 1) & (MEDIA_N - 1); /* & 7: volta pro 0 depois do 7 */
        if (f2_qtd_amostras < MEDIA_N) f2_qtd_amostras++;
        if (f2_qtd_amostras == MEDIA_N) f2_pot = f2_soma >> MEDIA_SHIFT; /* >> 3 = dividir por 8 */
    }

    f2_adc_div++;
    if (f2_adc_div >= ADC_PERIODO_MS && !adc_ocupado()) {
        f2_adc_div = 0;
        adc_iniciar_conversao(ADC_CANAL_POT);   /* pede outra leitura do pot */
    }
}

void fase2_atualizar(void)              /* chamada a cada 1 ms */
{
    fase2_ler_potenciometro();

    if (f2_qtd_amostras < MEDIA_N) return;
    if (!f2_alvo_definido) fase2_sortear_alvo();

    uint16_t d = distancia(f2_pot, f2_alvo);    /* quão longe do alvo */
    if (f2_na_zona) {                           /* histerese: entra a 40, sai a 55 */
        if (d > ZONA_SAI) f2_na_zona = 0;
    } else {
        if (d <= ZONA_ENTRA) f2_na_zona = 1;
    }

    if (f2_estado == F2_PROCURANDO) {
        f2_som_div++;
        if (f2_som_div >= SOM_PERIODO_MS) {
            f2_som_div = 0;
            if (melodia_tocando()) return;      /* melodia tem prioridade */
            if (f2_na_zona) buzzer_desligar();  /* no alvo: silêncio */
            else            buzzer_tocar(fase2_freq_quente_frio(d)); /* fora: tom mais agudo quanto mais perto */
        }
    }
}

void fase2_start(void)
{
    if (f2_estado != F2_PROCURANDO || !f2_alvo_definido) return;

    if (f2_na_zona) {
        buzzer_desligar();
        f2_estado = F2_DIGITANDO;               /* achou: mostra a frequência e espera a letra */
        f2_letra = 0;
        f2_tela_mudou = 1;
    } else {
        registrar_erro();                       /* START fora da zona é erro */
    }
}

void fase2_tratar_tecla(char tecla)
{
    if (f2_estado != F2_DIGITANDO) return;

    if (tecla >= 'A' && tecla <= 'Z') {
        f2_letra = tecla;
        f2_tela_mudou = 1;
    } else if (tecla == '#') {
        f2_letra = 0;
        f2_tela_mudou = 1;
    } else if (tecla == '*' && f2_letra != 0) {
        if (f2_letra == F2_LETRA_CERTA[f2_rodada]) {
            f2_rodada++;
            if (f2_rodada >= F2_RODADAS) senha_entrar(); /* acabaram as 2 rodadas: senha final */
            else                         fase2_comecar_rodada();
        } else {
            registrar_erro();                   /* errou: -30 s */
            if (estado == JOGO_DERROTA) return;
            fase2_comecar_rodada();             /* repete a rodada com outro alvo */
        }
    }
}


/* SENHA FINAL: digitar as 4 letras na ordem */

void senha_desenhar(void)
{
    char texto[17] = "SENHA: ";
    for (uint8_t i = 0; i < SENHA_TAM; i++) {
        texto[7 + i] = (i < senha_tam) ? senha_digitada[i] : '_';
    }
    texto[7 + SENHA_TAM] = '\0';
    lcd_linha0(texto);
}

void senha_limpar(void)
{
    senha_tam = 0;
    senha_digitada[0] = '\0';
}

void senha_entrar(void)
{
    estado = JOGO_SENHA;
    senha_limpar();
    lcd_linha(1, "");
    mostrar_linha1_tempo_erros();
    senha_desenhar();
    uart_enviar_linha("EVENTO SENHA");
}

void senha_tratar_tecla(char tecla)
{
    if (tecla >= 'A' && tecla <= 'Z') {
        if (senha_tam < SENHA_TAM) {
            senha_digitada[senha_tam++] = tecla;
            senha_digitada[senha_tam] = '\0';
        }
        senha_desenhar();
    } else if (tecla == '#') {
        if (senha_tam > 0) {                    /* # apaga a última letra */
            senha_tam--;
            senha_digitada[senha_tam] = '\0';
        }
        senha_desenhar();
    } else if (tecla == '*' && senha_tam > 0) { /* * confirma */
        if (senha_tam == SENHA_TAM && strcmp(senha_digitada, SENHA_CERTA) == 0) { /* 4 letras e igual a AVAI */
            vitoria();                          /* desarmou a bomba */
        } else {
            senha_limpar();
            registrar_erro();                   /* mostra "ERRO!" e depois volta pra senha */
        }
    }
}
