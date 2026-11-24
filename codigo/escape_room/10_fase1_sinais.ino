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
