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
