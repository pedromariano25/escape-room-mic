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
